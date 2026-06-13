#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include "arv/client/block_render_assets.hpp"
#include "arv/client/chunk_snapshot.hpp"
#include "arv/client/client_session.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/crypto/crypto_init.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/net/transport.hpp"
#include "arv/net/websocket_client_transport.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/protocol_version.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "web_chunk_mesh.hpp"
#include "web_player_movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr arv::usize MaxEntityShaderBones = 128U;

struct BrowserInputState {
  bool forward{};
  bool back{};
  bool left{};
  bool right{};
  bool jump{};
  bool jump_pressed{};
  bool sneak{};
  bool sprint{};
  bool primary_held{};
  bool secondary_held{};
  bool primary_pressed{};
  bool secondary_pressed{};
  bool toggle_fly_pressed{};
  double mouse_dx{};
  double mouse_dy{};
  double last_frame_time{};
  double movement_accumulator_seconds{};
  double movement_send_accumulator_seconds{};
  double movement_send_rate_hz{arv::protocol::DefaultClientMovementSendRateHz};
  arv::u64 client_tick{};
  arv::client::LocalPlayerController player{};
  bool player_initialized{};
};

struct IndexedGpuMesh {
  GLuint vao{};
  GLuint vbo{};
  GLuint ebo{};
  GLsizei index_count{};
};

struct ChunkGpuMesh {
  arv::ChunkSectionCoord coord{};
  arv::u64 content_version{};
  arv::u64 light_version{};
  arv::u64 dependency_signature{};
  IndexedGpuMesh opaque{};
  IndexedGpuMesh transparent{};
};

struct EntityGpuModel {
  GLuint vao{};
  GLuint vbo{};
  GLuint ebo{};
  GLsizei index_count{};
};

struct ChunkRenderer {
  GLuint chunk_program{};
  GLint chunk_mvp_location{-1};
  GLint chunk_origin_location{-1};
  GLint chunk_atlas_location{-1};
  GLint chunk_atlas_inv_size_location{-1};
  GLuint chunk_texture{};
  arv::u32 chunk_texture_width{};
  arv::u32 chunk_texture_height{};
  GLuint color_program{};
  GLint color_mvp_location{-1};
  GLuint entity_program{};
  GLint entity_mvp_location{-1};
  GLint entity_position_location{-1};
  GLint entity_rotation_location{-1};
  GLint entity_scale_location{-1};
  GLint entity_atlas_location{-1};
  GLint entity_atlas_size_location{-1};
  GLint entity_bone_count_location{-1};
  GLint entity_bone_pivots_location{-1};
  GLint entity_bone_rotations_location{-1};
  GLint entity_bone_positions_location{-1};
  GLuint entity_texture{};
  arv::u32 entity_texture_width{};
  arv::u32 entity_texture_height{};
  std::vector<ChunkGpuMesh> meshes{};
  arv::HashMap<arv::render::EntityModelID, EntityGpuModel> entity_models{};
  GLuint entity_vao{};
  GLuint entity_vbo{};
  GLsizei entity_vertex_count{};
  std::array<arv::Vec3f, MaxEntityShaderBones> entity_bone_pivots{};
  std::array<arv::Vec3f, MaxEntityShaderBones> entity_bone_rotations{};
  std::array<arv::Vec3f, MaxEntityShaderBones> entity_bone_positions{};
  bool announced_first_upload{};
  bool announced_first_entity_upload{};
};

struct WebBlockRenderAssets {
  arv::client::ClientBlockRenderAssets loaded{};
  std::vector<std::filesystem::path> pack_cache_paths{};
  std::string registry_snapshot_json{};
  bool ready{};
};

struct WebEntityRenderAssets {
  arv::client::ClientEntityRenderAssets loaded{};
  std::vector<std::filesystem::path> pack_cache_paths{};
  std::string registry_snapshot_json{};
  bool ready{};
};

using ChunkSectionIndex = arv::HashMap<
    arv::ChunkSectionCoord,
    const arv::protocol::ClientboundChunkSectionData*>;

struct WebApplication {
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context{};
  int width{1};
  int height{1};
  double start_time{};
  bool pointer_locked{};
  bool focused{true};
  bool visible{true};
  bool connected{};
  bool connecting{};
  bool flushed_after_pack_ready{};
  arv::net::ConnectionState last_connection_state{arv::net::ConnectionState::Disconnected};
  std::unique_ptr<arv::client::ClientSession> session{};
  BrowserInputState input{};
  ChunkRenderer renderer{};
  WebBlockRenderAssets block_assets{};
  WebEntityRenderAssets entity_assets{};
};

WebApplication g_app;

EM_JS(void, arv_web_set_status, (const char* text), {
  const value = UTF8ToString(text);
  if (typeof window.arvWebSetStatus === 'function') {
    window.arvWebSetStatus(value);
  } else {
    console.log(value);
  }
});

EM_JS(void, arv_web_note_runtime_ready, (), {
  if (typeof window.arvWebRuntimeReady === 'function') {
    window.arvWebRuntimeReady();
  }
});

EM_JS(int, arv_web_storage_ready, (), {
  return Module && Module.arvWebStorageReady ? 1 : 0;
});

EM_JS(void, arv_web_flush_storage, (), {
  if (!Module || !Module.arvWebStorageReady || Module.arvWebStorageFlushing) {
    return;
  }
  if (typeof FS === 'undefined' || typeof FS.syncfs !== 'function') {
    return;
  }
  Module.arvWebStorageFlushing = true;
  FS.syncfs(false, function(error) {
    Module.arvWebStorageFlushing = false;
    if (error) {
      Module.arvWebStorageError = String(error);
      if (typeof window.arvWebSetStatus === 'function') {
        window.arvWebSetStatus('storage sync failed: ' + Module.arvWebStorageError);
      }
      return;
    }
    if (typeof window.arvWebSetStatus === 'function') {
      window.arvWebSetStatus('storage synced');
    }
  });
});

EM_JS(int, arv_web_canvas_width, (), {
  const canvas = document.getElementById('arvane-canvas');
  if (!canvas) {
    return 1;
  }
  return Math.max(1, Math.floor(canvas.clientWidth * (window.devicePixelRatio || 1)));
});

EM_JS(int, arv_web_canvas_height, (), {
  const canvas = document.getElementById('arvane-canvas');
  if (!canvas) {
    return 1;
  }
  return Math.max(1, Math.floor(canvas.clientHeight * (window.devicePixelRatio || 1)));
});

EM_JS(void, arv_web_set_canvas_size, (int width, int height), {
  const canvas = document.getElementById('arvane-canvas');
  if (!canvas) {
    return;
  }
  if (canvas.width !== width) {
    canvas.width = width;
  }
  if (canvas.height !== height) {
    canvas.height = height;
  }
});

void set_status(const char* text) {
  arv_web_set_status(text);
}

void set_status(const std::string& text) {
  arv_web_set_status(text.c_str());
}

void destroy_indexed_mesh(IndexedGpuMesh& mesh) {
  if (mesh.ebo != 0U) {
    glDeleteBuffers(1, &mesh.ebo);
    mesh.ebo = 0U;
  }
  if (mesh.vbo != 0U) {
    glDeleteBuffers(1, &mesh.vbo);
    mesh.vbo = 0U;
  }
  if (mesh.vao != 0U) {
    glDeleteVertexArrays(1, &mesh.vao);
    mesh.vao = 0U;
  }
  mesh.index_count = 0;
}

void destroy_mesh(ChunkGpuMesh& mesh) {
  destroy_indexed_mesh(mesh.opaque);
  destroy_indexed_mesh(mesh.transparent);
}

void destroy_entity_gpu_model(EntityGpuModel& model) {
  if (model.ebo != 0U) {
    glDeleteBuffers(1, &model.ebo);
  }
  if (model.vbo != 0U) {
    glDeleteBuffers(1, &model.vbo);
  }
  if (model.vao != 0U) {
    glDeleteVertexArrays(1, &model.vao);
  }
  model = {};
}

void destroy_entity_gpu_models() {
  for (auto& [id, model] : g_app.renderer.entity_models) {
    (void)id;
    destroy_entity_gpu_model(model);
  }
  g_app.renderer.entity_models.clear();
}

void destroy_entity_mesh() {
  if (g_app.renderer.entity_vbo != 0U) {
    glDeleteBuffers(1, &g_app.renderer.entity_vbo);
    g_app.renderer.entity_vbo = 0U;
  }
  if (g_app.renderer.entity_vao != 0U) {
    glDeleteVertexArrays(1, &g_app.renderer.entity_vao);
    g_app.renderer.entity_vao = 0U;
  }
  g_app.renderer.entity_vertex_count = 0;
}

void clear_chunk_meshes() {
  for (ChunkGpuMesh& mesh : g_app.renderer.meshes) {
    destroy_mesh(mesh);
  }
  g_app.renderer.meshes.clear();
  destroy_entity_mesh();
  g_app.renderer.announced_first_upload = false;
  g_app.renderer.announced_first_entity_upload = false;
}

void destroy_chunk_texture() {
  if (g_app.renderer.chunk_texture != 0U) {
    glDeleteTextures(1, &g_app.renderer.chunk_texture);
  }
  g_app.renderer.chunk_texture = 0U;
  g_app.renderer.chunk_texture_width = 0U;
  g_app.renderer.chunk_texture_height = 0U;
}

void destroy_entity_texture() {
  if (g_app.renderer.entity_texture != 0U) {
    glDeleteTextures(1, &g_app.renderer.entity_texture);
  }
  g_app.renderer.entity_texture = 0U;
  g_app.renderer.entity_texture_width = 0U;
  g_app.renderer.entity_texture_height = 0U;
}

void clear_entity_render_assets() {
  destroy_entity_mesh();
  destroy_entity_gpu_models();
  destroy_entity_texture();
  g_app.entity_assets = {};
  g_app.renderer.announced_first_entity_upload = false;
}

void clear_block_render_assets() {
  clear_chunk_meshes();
  destroy_chunk_texture();
  clear_entity_render_assets();
  g_app.block_assets = {};
}

void reset_input(bool keep_view = false) {
  const float yaw = g_app.input.player.camera().yaw_radians();
  const float pitch = g_app.input.player.camera().pitch_radians();
  g_app.input = {};
  if (keep_view) {
    g_app.input.player.camera().set_rotation(yaw, pitch);
  }
}

[[nodiscard]] GLuint compile_shader(GLenum kind, const char* source) {
  const GLuint shader = glCreateShader(kind);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_TRUE) {
    return shader;
  }

  std::array<char, 512> log{};
  GLsizei length{};
  glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
  set_status(std::string{"shader compile failed: "} + log.data());
  glDeleteShader(shader);
  return 0U;
}

[[nodiscard]] bool initialize_chunk_renderer() {
  static constexpr const char* chunk_vertex_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in uint a_pos_ao;
layout(location = 1) in uint a_uv;
layout(location = 2) in uint a_light_face_material;
layout(location = 3) in uint a_atlas_page;
uniform mat4 u_mvp;
uniform vec3 u_chunk_origin;
uniform vec2 u_atlas_inv_size;
out vec2 v_uv;
out float v_light;
flat out uint v_atlas_page;
void main() {
  uint block_x = a_pos_ao & 31u;
  uint block_y = (a_pos_ao >> 5u) & 31u;
  uint block_z = (a_pos_ao >> 10u) & 31u;
  uint model_x = (a_pos_ao >> 15u) & 31u;
  uint model_y = (a_pos_ao >> 20u) & 31u;
  uint model_z = (a_pos_ao >> 25u) & 31u;
  uint ao = (a_pos_ao >> 30u) & 3u;
  vec3 local_pos = vec3(block_x, block_y, block_z) +
      vec3(model_x, model_y, model_z) * (1.0 / 16.0);
  uint atlas_u = a_uv & 65535u;
  uint atlas_v = (a_uv >> 16u) & 65535u;
  v_uv = (vec2(float(atlas_u), float(atlas_v)) + vec2(0.5)) * u_atlas_inv_size;
  v_atlas_page = a_atlas_page;

  float block_r = float((a_light_face_material >> 0u) & 15u);
  float block_g = float((a_light_face_material >> 4u) & 15u);
  float block_b = float((a_light_face_material >> 8u) & 15u);
  float sky = float((a_light_face_material >> 12u) & 15u);
  float light = max(max(max(block_r, block_g), block_b), sky) / 15.0;
  float ao_factor = mix(0.58, 1.0, float(ao) / 3.0);
  v_light = clamp(light, 0.18, 1.0) * ao_factor;
  gl_Position = u_mvp * vec4(u_chunk_origin + local_pos, 1.0);
}
)";
  static constexpr const char* chunk_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
uniform sampler2DArray u_chunk_atlas;
in vec2 v_uv;
in float v_light;
flat in uint v_atlas_page;
out vec4 out_color;
void main() {
  vec4 texel = texture(u_chunk_atlas, vec3(v_uv, float(v_atlas_page)));
  if (texel.a < 0.1) {
    discard;
  }
  out_color = vec4(texel.rgb * v_light, texel.a);
}
)";
  static constexpr const char* color_vertex_shader_source = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;
uniform mat4 u_mvp;
out vec3 v_color;
void main() {
  v_color = a_color;
  gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";
  static constexpr const char* color_fragment_shader_source = R"(#version 300 es
precision highp float;
in vec3 v_color;
out vec4 out_color;
void main() {
  out_color = vec4(v_color, 1.0);
}
)";
  static constexpr const char* entity_vertex_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_color;
layout(location = 3) in uint a_bone_index;
layout(location = 4) in uint a_flags;
layout(location = 5) in uint a_texture_domain;
layout(location = 6) in uint a_atlas_page;
uniform mat4 u_mvp;
uniform vec3 u_instance_position;
uniform vec4 u_instance_rotation;
uniform float u_instance_scale;
uniform int u_bone_count;
uniform vec3 u_bone_pivots[128];
uniform vec3 u_bone_rotations[128];
uniform vec3 u_bone_positions[128];
out vec2 v_uv;
out vec3 v_color;
flat out uint v_flags;
flat out uint v_texture_domain;
flat out uint v_atlas_page;

vec3 rotate_euler_degrees(vec3 value, vec3 degrees) {
  vec3 r = radians(degrees);
  float cx = cos(r.x);
  float sx = sin(r.x);
  float cy = cos(r.y);
  float sy = sin(r.y);
  float cz = cos(r.z);
  float sz = sin(r.z);

  value = vec3(value.x, value.y * cx - value.z * sx, value.y * sx + value.z * cx);
  value = vec3(value.x * cy + value.z * sy, value.y, -value.x * sy + value.z * cy);
  value = vec3(value.x * cz - value.y * sz, value.x * sz + value.y * cz, value.z);
  return value;
}

vec3 rotate_quat(vec3 value, vec4 rotation) {
  vec3 qv = rotation.xyz;
  vec3 t = cross(qv, value) * 2.0;
  return value + t * rotation.w + cross(qv, t);
}

void main() {
  vec3 model_position = a_position;
  int bone_index = int(a_bone_index);
  if (bone_index >= 0 && bone_index < u_bone_count) {
    vec3 pivot = u_bone_pivots[bone_index];
    model_position =
        pivot +
        rotate_euler_degrees(model_position - pivot, u_bone_rotations[bone_index]) +
        u_bone_positions[bone_index];
  }

  vec3 scaled = model_position * (u_instance_scale / 16.0);
  vec3 world = u_instance_position + rotate_quat(scaled, u_instance_rotation);
  v_uv = a_uv;
  v_color = a_color;
  v_flags = a_flags;
  v_texture_domain = a_texture_domain;
  v_atlas_page = a_atlas_page;
  gl_Position = u_mvp * vec4(world, 1.0);
}
)";
  static constexpr const char* entity_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
uniform sampler2DArray u_entity_atlas;
uniform vec2 u_atlas_size;
in vec2 v_uv;
in vec3 v_color;
flat in uint v_flags;
flat in uint v_texture_domain;
flat in uint v_atlas_page;
out vec4 out_color;
void main() {
  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  vec4 texel = texture(u_entity_atlas, vec3(v_uv / atlas_size, float(v_atlas_page)));
  bool skin_base = (v_flags & 1u) != 0u;
  bool skin_decor = (v_flags & 2u) != 0u;
  if (skin_base && texel.a < 0.1) {
    texel = vec4(0.0, 0.0, 0.0, 1.0);
  } else if ((skin_decor || !skin_base) && texel.a < 0.1) {
    discard;
  }
  vec3 light = vec3(0.86, 0.88, 0.92);
  out_color = vec4(texel.rgb * v_color * light, texel.a);
}
)";

  const auto link_program = [](const char* label, const char* vertex_source, const char* fragment_source) -> GLuint {
    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    if (vertex_shader == 0U) {
      return 0U;
    }
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment_shader == 0U) {
      glDeleteShader(vertex_shader);
      return 0U;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
      std::array<char, 512> log{};
      GLsizei length{};
      glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &length, log.data());
      set_status(std::string{label} + " link failed: " + log.data());
      glDeleteProgram(program);
      return 0U;
    }
    return program;
  };

  g_app.renderer.chunk_program = link_program(
      "chunk renderer",
      chunk_vertex_shader_source,
      chunk_fragment_shader_source);
  if (g_app.renderer.chunk_program == 0U) {
    return false;
  }
  g_app.renderer.color_program = link_program(
      "color renderer",
      color_vertex_shader_source,
      color_fragment_shader_source);
  if (g_app.renderer.color_program == 0U) {
    return false;
  }
  g_app.renderer.entity_program = link_program(
      "entity renderer",
      entity_vertex_shader_source,
      entity_fragment_shader_source);
  if (g_app.renderer.entity_program == 0U) {
    return false;
  }

  g_app.renderer.chunk_mvp_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_mvp");
  g_app.renderer.chunk_origin_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_chunk_origin");
  g_app.renderer.chunk_atlas_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_chunk_atlas");
  g_app.renderer.chunk_atlas_inv_size_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_atlas_inv_size");
  g_app.renderer.color_mvp_location =
      glGetUniformLocation(g_app.renderer.color_program, "u_mvp");
  g_app.renderer.entity_mvp_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_mvp");
  g_app.renderer.entity_position_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_instance_position");
  g_app.renderer.entity_rotation_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_instance_rotation");
  g_app.renderer.entity_scale_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_instance_scale");
  g_app.renderer.entity_atlas_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_entity_atlas");
  g_app.renderer.entity_atlas_size_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_atlas_size");
  g_app.renderer.entity_bone_count_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_bone_count");
  g_app.renderer.entity_bone_pivots_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_bone_pivots[0]");
  g_app.renderer.entity_bone_rotations_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_bone_rotations[0]");
  g_app.renderer.entity_bone_positions_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_bone_positions[0]");
  return true;
}

void upload_vertices(
    GLuint& vao,
    GLuint& vbo,
    GLsizei& vertex_count,
    const std::vector<arv::web::WebChunkVertex>& vertices,
    GLenum usage) {
  vertex_count = static_cast<GLsizei>(vertices.size());
  if (vertices.empty()) {
    return;
  }
  std::vector<float> interleaved;
  interleaved.reserve(vertices.size() * 6U);
  for (const arv::web::WebChunkVertex& vertex : vertices) {
    interleaved.push_back(vertex.position.x);
    interleaved.push_back(vertex.position.y);
    interleaved.push_back(vertex.position.z);
    interleaved.push_back(vertex.color.x);
    interleaved.push_back(vertex.color.y);
    interleaved.push_back(vertex.color.z);
  }

  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)),
      interleaved.data(),
      usage);
  glEnableVertexAttribArray(0U);
  glVertexAttribPointer(0U, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1U);
  glVertexAttribPointer(
      1U,
      3,
      GL_FLOAT,
      GL_FALSE,
      6 * sizeof(float),
      reinterpret_cast<const void*>(3U * sizeof(float)));
  glBindBuffer(GL_ARRAY_BUFFER, 0U);
  glBindVertexArray(0U);
}

void upload_indexed_mesh(IndexedGpuMesh& gpu, const arv::render::CpuMeshBuffer& mesh) {
  destroy_indexed_mesh(gpu);
  if (mesh.vertex_bytes.empty() || mesh.indices.empty()) {
    return;
  }

  glGenVertexArrays(1, &gpu.vao);
  glGenBuffers(1, &gpu.vbo);
  glGenBuffers(1, &gpu.ebo);
  glBindVertexArray(gpu.vao);
  glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.vertex_bytes.size()),
      mesh.vertex_bytes.data(),
      GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(arv::u32)),
      mesh.indices.data(),
      GL_STATIC_DRAW);

  glEnableVertexAttribArray(0U);
  glVertexAttribIPointer(
      0U,
      1,
      GL_UNSIGNED_INT,
      sizeof(arv::render::ChunkVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::ChunkVertex, pos_ao)));
  glEnableVertexAttribArray(1U);
  glVertexAttribIPointer(
      1U,
      1,
      GL_UNSIGNED_INT,
      sizeof(arv::render::ChunkVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::ChunkVertex, uv)));
  glEnableVertexAttribArray(2U);
  glVertexAttribIPointer(
      2U,
      1,
      GL_UNSIGNED_INT,
      sizeof(arv::render::ChunkVertex),
      reinterpret_cast<const void*>(
          offsetof(arv::render::ChunkVertex, light_face_material_hints)));
  glEnableVertexAttribArray(3U);
  glVertexAttribIPointer(
      3U,
      1,
      GL_UNSIGNED_INT,
      sizeof(arv::render::ChunkVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::ChunkVertex, atlas_page)));
  glBindVertexArray(0U);
  gpu.index_count = static_cast<GLsizei>(mesh.indices.size());
}

void upload_entity_model(
    arv::render::EntityModelID id,
    const arv::render::EntityModelAsset& model) {
  auto found = g_app.renderer.entity_models.find(id);
  if (found != g_app.renderer.entity_models.end()) {
    destroy_entity_gpu_model(found->second);
    g_app.renderer.entity_models.erase(found);
  }
  if (model.vertices.empty() || model.indices.empty()) {
    return;
  }

  EntityGpuModel gpu;
  glGenVertexArrays(1, &gpu.vao);
  glGenBuffers(1, &gpu.vbo);
  glGenBuffers(1, &gpu.ebo);
  glBindVertexArray(gpu.vao);
  glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(
          model.vertices.size() * sizeof(arv::render::EntityModelMeshVertex)),
      model.vertices.data(),
      GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(model.indices.size() * sizeof(arv::u32)),
      model.indices.data(),
      GL_STATIC_DRAW);

  glEnableVertexAttribArray(0U);
  glVertexAttribPointer(
      0U,
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(arv::render::EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::EntityModelMeshVertex, position)));
  glEnableVertexAttribArray(1U);
  glVertexAttribPointer(
      1U,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(arv::render::EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::EntityModelMeshVertex, uv)));
  glEnableVertexAttribArray(2U);
  glVertexAttribPointer(
      2U,
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(arv::render::EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::EntityModelMeshVertex, color)));
  glEnableVertexAttribArray(3U);
  glVertexAttribIPointer(
      3U,
      1,
      GL_UNSIGNED_INT,
      sizeof(arv::render::EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::EntityModelMeshVertex, bone_index)));
  glEnableVertexAttribArray(4U);
  glVertexAttribIPointer(
      4U,
      1,
      GL_UNSIGNED_INT,
      sizeof(arv::render::EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::EntityModelMeshVertex, flags)));
  glEnableVertexAttribArray(5U);
  glVertexAttribIPointer(
      5U,
      1,
      GL_UNSIGNED_INT,
      sizeof(arv::render::EntityModelMeshVertex),
      reinterpret_cast<const void*>(
          offsetof(arv::render::EntityModelMeshVertex, texture_domain)));
  glEnableVertexAttribArray(6U);
  glVertexAttribIPointer(
      6U,
      1,
      GL_UNSIGNED_INT,
      sizeof(arv::render::EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(arv::render::EntityModelMeshVertex, atlas_page)));

  glBindVertexArray(0U);
  gpu.index_count = static_cast<GLsizei>(model.indices.size());
  g_app.renderer.entity_models[id] = gpu;
}

void upload_entity_models(const arv::client::EntityRenderAssets& assets) {
  destroy_entity_gpu_models();
  for (const arv::render::EntityModelAsset& model : assets.models) {
    upload_entity_model(model.id, model);
  }
}

[[nodiscard]] arv::u32 full_mipmap_max_level(arv::u32 width, arv::u32 height) noexcept {
  arv::u32 size = std::max(width, height);
  arv::u32 level = 0U;
  while (size > 1U) {
    size /= 2U;
    ++level;
  }
  return level;
}

[[nodiscard]] bool upload_chunk_texture_array(
    const arv::render::TextureArrayUploadData& upload) {
  destroy_chunk_texture();
  if (!upload.valid()) {
    set_status("block texture atlas upload was empty");
    return false;
  }

  GLint max_texture_size = 0;
  GLint max_array_layers = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_array_layers);
  if ((max_texture_size > 0 &&
       (upload.width > static_cast<arv::u32>(max_texture_size) ||
        upload.height > static_cast<arv::u32>(max_texture_size))) ||
      (max_array_layers > 0 && upload.layers > static_cast<arv::u32>(max_array_layers))) {
    set_status("block texture atlas exceeds WebGL texture-array limits");
    return false;
  }

  glGenTextures(1, &g_app.renderer.chunk_texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.chunk_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  const arv::u32 full_max_level = full_mipmap_max_level(upload.width, upload.height);
  const arv::u32 max_level = upload.limit_mipmap_levels
                                 ? std::min(upload.max_mipmap_level, full_max_level)
                                 : full_max_level;
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(max_level));
  glTexImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      GL_RGBA8,
      static_cast<GLsizei>(upload.width),
      static_cast<GLsizei>(upload.height),
      static_cast<GLsizei>(upload.layers),
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      upload.rgba_pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  g_app.renderer.chunk_texture_width = upload.width;
  g_app.renderer.chunk_texture_height = upload.height;
  return true;
}

[[nodiscard]] bool upload_entity_texture_array(
    const arv::render::TextureArrayUploadData& upload) {
  destroy_entity_texture();
  if (!upload.valid()) {
    set_status("entity texture atlas upload was empty");
    return false;
  }

  GLint max_texture_size = 0;
  GLint max_array_layers = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_array_layers);
  if ((max_texture_size > 0 &&
       (upload.width > static_cast<arv::u32>(max_texture_size) ||
        upload.height > static_cast<arv::u32>(max_texture_size))) ||
      (max_array_layers > 0 && upload.layers > static_cast<arv::u32>(max_array_layers))) {
    set_status("entity texture atlas exceeds WebGL texture-array limits");
    return false;
  }

  glGenTextures(1, &g_app.renderer.entity_texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.entity_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  const arv::u32 full_max_level = full_mipmap_max_level(upload.width, upload.height);
  const arv::u32 max_level = upload.limit_mipmap_levels
                                 ? std::min(upload.max_mipmap_level, full_max_level)
                                 : full_max_level;
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(max_level));
  glTexImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      GL_RGBA8,
      static_cast<GLsizei>(upload.width),
      static_cast<GLsizei>(upload.height),
      static_cast<GLsizei>(upload.layers),
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      upload.rgba_pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  g_app.renderer.entity_texture_width = upload.width;
  g_app.renderer.entity_texture_height = upload.height;
  return true;
}

[[nodiscard]] std::vector<std::filesystem::path> copy_pack_paths(
    std::span<const std::filesystem::path> paths) {
  return {paths.begin(), paths.end()};
}

[[nodiscard]] bool same_pack_paths(
    std::span<const std::filesystem::path> left,
    const std::vector<std::filesystem::path>& right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin());
}

[[nodiscard]] ChunkSectionIndex build_chunk_section_index(
    std::span<const arv::protocol::ClientboundChunkSectionData> sections) {
  ChunkSectionIndex index;
  index.reserve(sections.size());
  for (const arv::protocol::ClientboundChunkSectionData& section : sections) {
    index[section.coord] = &section;
  }
  return index;
}

[[nodiscard]] bool ensure_block_render_assets_loaded() {
  if (g_app.session == nullptr ||
      g_app.session->registry_snapshot_json().empty()) {
    return false;
  }
  const std::span<const std::filesystem::path> cache_paths =
      g_app.session->multiplayer_pack_cache_paths();
  if (cache_paths.empty()) {
    return false;
  }
  if (g_app.block_assets.ready &&
      g_app.block_assets.registry_snapshot_json == g_app.session->registry_snapshot_json() &&
      same_pack_paths(cache_paths, g_app.block_assets.pack_cache_paths) &&
      g_app.renderer.chunk_texture != 0U) {
    return true;
  }

  clear_block_render_assets();
  std::vector<std::filesystem::path> paths = copy_pack_paths(cache_paths);
  auto packs_result = arv::asset::PackManager::load_client_bundle_roots(paths);
  if (!packs_result) {
    set_status("failed to mount web block assets: " + packs_result.error().describe());
    return false;
  }

  arv::asset::PackManager packs = std::move(packs_result.value());
  arv::asset::ResourceManager resources(packs);
  auto loaded_assets = arv::client::load_client_block_render_assets(packs, resources);
  if (!loaded_assets) {
    set_status(
        "failed to load web block assets: " +
        loaded_assets.error().category +
        ": " +
        loaded_assets.error().message);
    return false;
  }

  const arv::render::TextureArrayUploadData atlas_upload =
      arv::client::texture_array_upload_from_atlas(
          loaded_assets.value().block_atlas.atlas);
  if (!upload_chunk_texture_array(atlas_upload)) {
    return false;
  }

  g_app.block_assets.loaded = std::move(loaded_assets.value());
  g_app.block_assets.registry_snapshot_json = g_app.session->registry_snapshot_json();
  g_app.block_assets.pack_cache_paths = std::move(paths);
  g_app.block_assets.ready = true;
  set_status("web block textures and models ready");
  return true;
}

[[nodiscard]] bool ensure_entity_render_assets_loaded() {
  if (!ensure_block_render_assets_loaded()) {
    return false;
  }
  if (g_app.session == nullptr ||
      g_app.session->registry_snapshot_json().empty()) {
    return false;
  }
  const std::span<const std::filesystem::path> cache_paths =
      g_app.session->multiplayer_pack_cache_paths();
  if (cache_paths.empty()) {
    return false;
  }
  if (g_app.entity_assets.ready &&
      g_app.entity_assets.registry_snapshot_json == g_app.session->registry_snapshot_json() &&
      same_pack_paths(cache_paths, g_app.entity_assets.pack_cache_paths) &&
      g_app.renderer.entity_texture != 0U &&
      !g_app.renderer.entity_models.empty()) {
    return true;
  }

  clear_entity_render_assets();
  std::vector<std::filesystem::path> paths = copy_pack_paths(cache_paths);
  auto packs_result = arv::asset::PackManager::load_client_bundle_roots(paths);
  if (!packs_result) {
    set_status("failed to mount web entity assets: " + packs_result.error().describe());
    return false;
  }

  arv::asset::PackManager packs = std::move(packs_result.value());
  arv::asset::ResourceManager resources(packs);
  auto loaded_assets = arv::client::load_client_entity_render_assets(
      packs,
      resources,
      g_app.block_assets.loaded.block_registry,
      g_app.block_assets.loaded.block_atlas.render_data,
      g_app.block_assets.loaded.block_models);
  if (!loaded_assets) {
    set_status(
        "failed to load web entity assets: " +
        loaded_assets.error().category +
        ": " +
        loaded_assets.error().message);
    return false;
  }

  const arv::render::TextureArrayUploadData atlas_upload =
      arv::client::texture_array_upload_from_atlas(
          loaded_assets.value().render_assets.texture_atlas);
  if (!upload_entity_texture_array(atlas_upload)) {
    return false;
  }
  upload_entity_models(loaded_assets.value().render_assets);

  g_app.entity_assets.loaded = std::move(loaded_assets.value());
  g_app.entity_assets.registry_snapshot_json = g_app.session->registry_snapshot_json();
  g_app.entity_assets.pack_cache_paths = std::move(paths);
  g_app.entity_assets.ready = true;
  set_status("web entity models, textures, and animations ready");
  return true;
}

void upload_chunk_mesh(
    const arv::protocol::ClientboundChunkSectionData& section,
    arv::client::ChunkSectionDataLookup section_lookup) {
  if (!ensure_block_render_assets_loaded()) {
    return;
  }
  const arv::u64 dependency_signature =
      arv::client::chunk_meshing_dependency_signature(section, section_lookup);
  auto found = std::find_if(
      g_app.renderer.meshes.begin(),
      g_app.renderer.meshes.end(),
      [&](const ChunkGpuMesh& mesh) {
        return arv::web::same_chunk_section_coord(mesh.coord, section.coord);
      });
  if (found == g_app.renderer.meshes.end()) {
    found = g_app.renderer.meshes.insert(
        g_app.renderer.meshes.end(),
        ChunkGpuMesh{
            section.coord,
            section.chunk_content_version,
            section.chunk_light_version,
            dependency_signature});
  } else {
    destroy_mesh(*found);
    found->content_version = section.chunk_content_version;
    found->light_version = section.chunk_light_version;
    found->dependency_signature = dependency_signature;
  }

  const arv::voxel::ChunkSnapshot snapshot =
      arv::client::chunk_meshing_snapshot_from_section_data(section, section_lookup);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(
      section.world_id,
      snapshot,
      g_app.block_assets.loaded.block_registry,
      &g_app.block_assets.loaded.block_atlas.render_data,
      &g_app.block_assets.loaded.fluid_render_data);
  upload_indexed_mesh(found->opaque, mesh.opaque);
  upload_indexed_mesh(found->transparent, mesh.transparent);

  if ((found->opaque.index_count > 0 || found->transparent.index_count > 0) &&
      !g_app.renderer.announced_first_upload) {
    g_app.renderer.announced_first_upload = true;
    set_status("remote chunk mesh uploaded");
  }
}

[[nodiscard]] const arv::render::EntityModelAsset* find_entity_model(
    arv::render::EntityModelID id) {
  for (const arv::render::EntityModelAsset& model :
       g_app.entity_assets.loaded.render_assets.models) {
    if (model.id == id) {
      return &model;
    }
  }
  return nullptr;
}

[[nodiscard]] const arv::render::EntityAnimationAsset* find_entity_animation(
    arv::render::EntityAnimationID id) {
  for (const arv::render::EntityAnimationAsset& animation :
       g_app.entity_assets.loaded.render_assets.animations) {
    if (animation.id == id) {
      return &animation;
    }
  }
  return nullptr;
}

void apply_bone_rotation_overrides(
    const arv::render::EntityModelAsset& model,
    std::span<arv::Vec3f> bone_rotations,
    const std::vector<arv::render::EntityBoneRotationOverride>& overrides) {
  for (const arv::render::EntityBoneRotationOverride& bone_override : overrides) {
    if (bone_override.bone_name.empty()) {
      continue;
    }
    for (arv::usize bone_index = 0;
         bone_index < model.bones.size() && bone_index < bone_rotations.size();
         ++bone_index) {
      if (model.bones[bone_index].name != bone_override.bone_name) {
        continue;
      }
      bone_rotations[bone_index] =
          bone_rotations[bone_index] + bone_override.rotation_degrees;
      break;
    }
  }
}

[[nodiscard]] std::vector<arv::render::EntityDrawItem> build_entity_draw_items() {
  std::vector<arv::render::EntityDrawItem> draws;
  if (g_app.session == nullptr || !g_app.entity_assets.ready) {
    return draws;
  }
  const std::optional<arv::client::ClientSessionWorldJoin>& join =
      g_app.session->world_join();
  draws.reserve(g_app.session->entity_mirrors().size());
  for (const arv::client::ClientSessionEntityMirror& mirror :
       g_app.session->entity_mirrors()) {
    if (join.has_value() && mirror.world_id != join->world_id) {
      continue;
    }
    if (join.has_value()) {
      const bool is_local_player =
          mirror.entity_id == join->local_entity_id ||
          (mirror.owning_player.has_value() &&
           mirror.owning_player.value() == join->player_id);
      if (is_local_player) {
        continue;
      }
    }
    const auto model =
        g_app.entity_assets.loaded.render_assets.model_by_type.find(mirror.entity_type);
    if (model == g_app.entity_assets.loaded.render_assets.model_by_type.end()) {
      continue;
    }

    arv::render::EntityDrawItem draw;
    draw.model_id = model->second;
    if (const auto animation =
            g_app.entity_assets.loaded.render_assets.animation_by_type.find(mirror.entity_type);
        animation != g_app.entity_assets.loaded.render_assets.animation_by_type.end()) {
      draw.animation_id = animation->second;
    }
    draw.position = mirror.position;
    draw.rotation = mirror.rotation;
    draw.scale = 1.0F;
    draw.animations.reserve(mirror.animations.size());
    for (const arv::protocol::EntityAnimationPlayback& animation : mirror.animations) {
      draw.animations.push_back({animation.name, animation.time_seconds});
    }
    if (mirror.owning_player.has_value()) {
      draw.bone_rotation_overrides.push_back({"head", mirror.head_rotation_degrees});
    }
    draws.push_back(std::move(draw));
  }
  return draws;
}

void draw_entity_models(const arv::Mat4f& mvp, bool translucent) {
  if (g_app.renderer.entity_program == 0U ||
      g_app.renderer.entity_texture == 0U ||
      !g_app.entity_assets.ready ||
      g_app.renderer.entity_models.empty()) {
    return;
  }

  std::vector<arv::render::EntityDrawItem> draws = build_entity_draw_items();
  const arv::Vec3f eye = g_app.input.player.camera().position();
  std::sort(
      draws.begin(),
      draws.end(),
      [&](const arv::render::EntityDrawItem& lhs,
          const arv::render::EntityDrawItem& rhs) {
        if (translucent) {
          return arv::distance_squared(lhs.position, eye) >
                 arv::distance_squared(rhs.position, eye);
        }
        return lhs.model_id < rhs.model_id;
      });

  glUseProgram(g_app.renderer.entity_program);
  glUniformMatrix4fv(
      g_app.renderer.entity_mvp_location,
      1,
      GL_FALSE,
      mvp.values.data());
  glUniform1i(g_app.renderer.entity_atlas_location, 0);
  glUniform2f(
      g_app.renderer.entity_atlas_size_location,
      static_cast<float>(std::max(g_app.renderer.entity_texture_width, 1U)),
      static_cast<float>(std::max(g_app.renderer.entity_texture_height, 1U)));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.entity_texture);

  for (const arv::render::EntityDrawItem& draw : draws) {
    const arv::render::EntityModelAsset* model = find_entity_model(draw.model_id);
    const auto gpu = g_app.renderer.entity_models.find(draw.model_id);
    if (model == nullptr ||
        gpu == g_app.renderer.entity_models.end() ||
        gpu->second.index_count == 0 ||
        model->translucent != translucent ||
        model->bones.size() > MaxEntityShaderBones) {
      continue;
    }

    const arv::render::EntityAnimationAsset* animation =
        find_entity_animation(draw.animation_id);
    for (arv::usize bone_index = 0; bone_index < model->bones.size(); ++bone_index) {
      g_app.renderer.entity_bone_pivots[bone_index] = model->bones[bone_index].pivot;
    }
    std::span<arv::Vec3f> bone_rotations{
        g_app.renderer.entity_bone_rotations.data(),
        model->bones.size()};
    std::span<arv::Vec3f> bone_positions{
        g_app.renderer.entity_bone_positions.data(),
        model->bones.size()};
    arv::render::sample_entity_model_animations(
        *model,
        animation,
        draw.animations,
        bone_rotations,
        bone_positions);
    apply_bone_rotation_overrides(*model, bone_rotations, draw.bone_rotation_overrides);

    const GLsizei bone_count = static_cast<GLsizei>(model->bones.size());
    glUniform1i(g_app.renderer.entity_bone_count_location, bone_count);
    if (bone_count > 0) {
      glUniform3fv(
          g_app.renderer.entity_bone_pivots_location,
          bone_count,
          reinterpret_cast<const GLfloat*>(g_app.renderer.entity_bone_pivots.data()));
      glUniform3fv(
          g_app.renderer.entity_bone_rotations_location,
          bone_count,
          reinterpret_cast<const GLfloat*>(g_app.renderer.entity_bone_rotations.data()));
      glUniform3fv(
          g_app.renderer.entity_bone_positions_location,
          bone_count,
          reinterpret_cast<const GLfloat*>(g_app.renderer.entity_bone_positions.data()));
    }
    glUniform3f(
        g_app.renderer.entity_position_location,
        draw.position.x,
        draw.position.y,
        draw.position.z);
    glUniform4f(
        g_app.renderer.entity_rotation_location,
        draw.rotation.x,
        draw.rotation.y,
        draw.rotation.z,
        draw.rotation.w);
    glUniform1f(g_app.renderer.entity_scale_location, draw.scale);
    glBindVertexArray(gpu->second.vao);
    glDrawElements(GL_TRIANGLES, gpu->second.index_count, GL_UNSIGNED_INT, nullptr);
  }

  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
}

void sync_entity_mesh() {
  destroy_entity_mesh();
  if (g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame) {
    return;
  }
  if (ensure_entity_render_assets_loaded()) {
    return;
  }

  const std::vector<arv::web::WebEntityMarker> markers =
      arv::web::build_visible_fallback_entity_markers(
          g_app.session->entity_mirrors(),
          g_app.session->world_join());
  const std::vector<arv::web::WebChunkVertex> vertices =
      arv::web::build_fallback_entity_vertices(markers);
  upload_vertices(
      g_app.renderer.entity_vao,
      g_app.renderer.entity_vbo,
      g_app.renderer.entity_vertex_count,
      vertices,
      GL_DYNAMIC_DRAW);

  if (!vertices.empty() && !g_app.renderer.announced_first_entity_upload) {
    g_app.renderer.announced_first_entity_upload = true;
    set_status("remote entity markers uploaded");
  }
}

void sync_chunk_meshes() {
  if (g_app.session == nullptr) {
    clear_chunk_meshes();
    return;
  }
  if (!ensure_block_render_assets_loaded()) {
    return;
  }

  const std::vector<arv::protocol::ClientboundChunkSectionData>& sections =
      g_app.session->chunk_sections();
  const ChunkSectionIndex section_index = build_chunk_section_index(sections);
  auto section_lookup =
      [&](arv::ChunkSectionCoord coord)
          -> const arv::protocol::ClientboundChunkSectionData* {
    const auto found = section_index.find(coord);
    return found == section_index.end() ? nullptr : found->second;
  };
  const auto stale_begin = std::remove_if(
      g_app.renderer.meshes.begin(),
      g_app.renderer.meshes.end(),
      [&](ChunkGpuMesh& mesh) {
        const bool missing = std::find_if(
            sections.begin(),
            sections.end(),
            [&](const arv::protocol::ClientboundChunkSectionData& section) {
              return arv::web::same_chunk_section_coord(section.coord, mesh.coord);
            }) == sections.end();
        if (missing) {
          destroy_mesh(mesh);
        }
        return missing;
      });
  g_app.renderer.meshes.erase(stale_begin, g_app.renderer.meshes.end());

  int uploads_this_frame = 0;
  for (const arv::protocol::ClientboundChunkSectionData& section : sections) {
    const arv::u64 dependency_signature =
        arv::client::chunk_meshing_dependency_signature(
            section,
            arv::client::ChunkSectionDataLookup{section_lookup});
    const auto found = std::find_if(
        g_app.renderer.meshes.begin(),
        g_app.renderer.meshes.end(),
        [&](const ChunkGpuMesh& mesh) {
          return arv::web::same_chunk_section_coord(mesh.coord, section.coord);
        });
    if (found != g_app.renderer.meshes.end() &&
        found->content_version == section.chunk_content_version &&
        found->light_version == section.chunk_light_version &&
        found->dependency_signature == dependency_signature) {
      continue;
    }
    upload_chunk_mesh(section, arv::client::ChunkSectionDataLookup{section_lookup});
    if (++uploads_this_frame >= 1) {
      break;
    }
  }
}

void render_remote_world() {
  if ((g_app.renderer.chunk_program == 0U || g_app.renderer.color_program == 0U) ||
      g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame) {
    return;
  }
  sync_chunk_meshes();
  sync_entity_mesh();
  if (g_app.renderer.meshes.empty() &&
      g_app.renderer.entity_vertex_count == 0 &&
      (!g_app.entity_assets.ready || g_app.renderer.entity_models.empty())) {
    return;
  }

  const float aspect =
      static_cast<float>(g_app.width) / static_cast<float>(std::max(g_app.height, 1));
  const arv::client::CameraShell& camera = g_app.input.player.camera();
  const arv::Vec3f eye = camera.position();
  const arv::Vec3f center = eye + camera.forward_vector();
  const arv::Mat4f mvp = arv::multiply_mat4f(
      arv::perspective_mat4f(
          arv::degrees_to_radians(camera.fov_y_degrees()),
          aspect,
          arv::client::DefaultCameraNearPlane,
          512.0F),
      arv::look_at_mat4f(eye, center, {0.0F, 1.0F, 0.0F}));

  if (g_app.renderer.chunk_texture != 0U) {
    glUseProgram(g_app.renderer.chunk_program);
    glUniformMatrix4fv(
        g_app.renderer.chunk_mvp_location,
        1,
        GL_FALSE,
        mvp.values.data());
    glUniform1i(g_app.renderer.chunk_atlas_location, 0);
    glUniform2f(
        g_app.renderer.chunk_atlas_inv_size_location,
        1.0F / static_cast<float>(std::max(g_app.renderer.chunk_texture_width, 1U)),
        1.0F / static_cast<float>(std::max(g_app.renderer.chunk_texture_height, 1U)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.chunk_texture);
    const auto draw_chunk_mesh = [](const ChunkGpuMesh& chunk, const IndexedGpuMesh& mesh) {
      if (mesh.index_count == 0 || mesh.vao == 0U) {
        return;
      }
      const arv::Vec3f origin{
          static_cast<float>(chunk.coord.value.x * arv::ChunkSize),
          static_cast<float>(chunk.coord.value.y * arv::ChunkSize),
          static_cast<float>(chunk.coord.value.z * arv::ChunkSize)};
      glUniform3f(
          g_app.renderer.chunk_origin_location,
          origin.x,
          origin.y,
          origin.z);
      glBindVertexArray(mesh.vao);
      glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    };
    for (const ChunkGpuMesh& mesh : g_app.renderer.meshes) {
      draw_chunk_mesh(mesh, mesh.opaque);
    }
    draw_entity_models(mvp, false);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const ChunkGpuMesh& mesh : g_app.renderer.meshes) {
      draw_chunk_mesh(mesh, mesh.transparent);
    }
    draw_entity_models(mvp, true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  }

  if (g_app.renderer.entity_vertex_count > 0 && g_app.renderer.entity_vao != 0U) {
    glUseProgram(g_app.renderer.color_program);
    glUniformMatrix4fv(
        g_app.renderer.color_mvp_location,
        1,
        GL_FALSE,
        mvp.values.data());
    glBindVertexArray(g_app.renderer.entity_vao);
    glDrawArrays(GL_TRIANGLES, 0, g_app.renderer.entity_vertex_count);
  }
  glBindVertexArray(0U);
  glUseProgram(0U);
}

const char* connection_state_label(arv::net::ConnectionState state) {
  switch (state) {
    case arv::net::ConnectionState::Disconnected:
      return "disconnected";
    case arv::net::ConnectionState::Connecting:
      return "connecting WebSocket";
    case arv::net::ConnectionState::TransportConnected:
      return "transport connected";
    case arv::net::ConnectionState::ProtocolHandshake:
      return "protocol handshake";
    case arv::net::ConnectionState::RegistrySync:
      return "registry sync";
    case arv::net::ConnectionState::WorldJoin:
      return "loading world";
    case arv::net::ConnectionState::InGame:
      return "in game";
    case arv::net::ConnectionState::Disconnecting:
      return "disconnecting";
  }
  return "unknown";
}

bool ensure_web_player_initialized(double now);

void poll_session(double now) {
  if (g_app.session == nullptr) {
    return;
  }

  g_app.session->pump();
  const arv::net::ConnectionState state = g_app.session->connection_state();
  if (state == arv::net::ConnectionState::InGame) {
    (void)ensure_web_player_initialized(now);
    if (std::optional<arv::protocol::ClientboundPlayerMoveCorrection> correction =
            g_app.session->take_player_move_correction();
        correction.has_value()) {
      g_app.input.player.apply_move_correction(correction.value());
      set_status("server movement correction applied");
    }
    if (std::optional<arv::protocol::ClientboundPlayerVelocity> velocity =
            g_app.session->take_player_velocity();
        velocity.has_value() && g_app.session->world_join().has_value()) {
      g_app.input.player.apply_player_velocity(velocity.value());
    }
    if (std::optional<arv::protocol::ClientboundSetMovementHz> settings =
            g_app.session->take_movement_settings();
        settings.has_value() && std::isfinite(settings->movement_hz)) {
      g_app.input.movement_send_rate_hz =
          std::max(1.0, static_cast<double>(settings->movement_hz));
    }
    if (std::optional<arv::protocol::ClientboundPlayerPermissions> permissions =
            g_app.session->take_player_permissions();
        permissions.has_value()) {
      g_app.input.player.apply_player_permissions(permissions.value());
    }
    if (std::optional<arv::protocol::ClientboundPlayerMovementTuning> tuning =
            g_app.session->take_player_movement_tuning();
        tuning.has_value()) {
      g_app.input.player.apply_player_movement_tuning(tuning.value());
    }
  }
  g_app.connected = state != arv::net::ConnectionState::Disconnected &&
                    state != arv::net::ConnectionState::Connecting;
  g_app.connecting = state == arv::net::ConnectionState::Connecting ||
                     state == arv::net::ConnectionState::ProtocolHandshake;
  if (state != g_app.last_connection_state) {
    g_app.last_connection_state = state;
    if (state == arv::net::ConnectionState::Disconnected &&
        g_app.session->disconnect_reason().has_value() &&
        !g_app.session->disconnect_reason()->empty()) {
      set_status("disconnected: " + *g_app.session->disconnect_reason());
    } else {
      set_status(connection_state_label(state));
    }
  }
  if (!g_app.flushed_after_pack_ready &&
      (state == arv::net::ConnectionState::WorldJoin ||
       state == arv::net::ConnectionState::InGame) &&
      !g_app.session->registry_snapshot_json().empty()) {
    g_app.flushed_after_pack_ready = true;
    set_status(
        "server registry verified; syncing " +
        std::to_string(g_app.session->multiplayer_pack_cache_paths().size()) +
        " cached pack root(s)");
    arv_web_flush_storage();
  }
  if (state == arv::net::ConnectionState::Disconnected) {
    g_app.flushed_after_pack_ready = false;
    clear_block_render_assets();
    reset_input(true);
  }
}

void resize_canvas() {
  const int width = arv_web_canvas_width();
  const int height = arv_web_canvas_height();
  if (width == g_app.width && height == g_app.height) {
    return;
  }

  g_app.width = width;
  g_app.height = height;
  arv_web_set_canvas_size(width, height);
  glViewport(0, 0, width, height);
}

EM_BOOL on_resize(int, const EmscriptenUiEvent*, void*) {
  resize_canvas();
  return EM_FALSE;
}

EM_BOOL on_focus(int event_type, const EmscriptenFocusEvent*, void*) {
  g_app.focused = event_type == EMSCRIPTEN_EVENT_FOCUS;
  if (!g_app.focused) {
    reset_input(true);
  }
  set_status(g_app.focused ? "focused" : "focus lost");
  return EM_FALSE;
}

EM_BOOL on_visibility(int, const EmscriptenVisibilityChangeEvent* event, void*) {
  g_app.visible = event != nullptr && !event->hidden;
  set_status(g_app.visible ? "visible" : "paused while hidden");
  return EM_FALSE;
}

EM_BOOL on_pointerlock(int, const EmscriptenPointerlockChangeEvent* event, void*) {
  g_app.pointer_locked = event != nullptr && event->isActive;
  g_app.input.mouse_dx = 0.0;
  g_app.input.mouse_dy = 0.0;
  set_status(g_app.pointer_locked ? "pointer locked" : "pointer released");
  return EM_FALSE;
}

EM_BOOL on_canvas_click(int, const EmscriptenMouseEvent*, void*) {
  if (!g_app.pointer_locked) {
    emscripten_request_pointerlock("#arvane-canvas", EM_TRUE);
  }
  return EM_TRUE;
}

bool set_key_state(const char* code, bool down, bool repeat) {
  if (std::strcmp(code, "KeyW") == 0 || std::strcmp(code, "ArrowUp") == 0) {
    g_app.input.forward = down;
    return true;
  }
  if (std::strcmp(code, "KeyS") == 0 || std::strcmp(code, "ArrowDown") == 0) {
    g_app.input.back = down;
    return true;
  }
  if (std::strcmp(code, "KeyA") == 0 || std::strcmp(code, "ArrowLeft") == 0) {
    g_app.input.left = down;
    return true;
  }
  if (std::strcmp(code, "KeyD") == 0 || std::strcmp(code, "ArrowRight") == 0) {
    g_app.input.right = down;
    return true;
  }
  if (std::strcmp(code, "Space") == 0) {
    if (down && !repeat && !g_app.input.jump) {
      g_app.input.jump_pressed = true;
    }
    g_app.input.jump = down;
    return true;
  }
  if (std::strcmp(code, "ShiftLeft") == 0 || std::strcmp(code, "ShiftRight") == 0) {
    g_app.input.sneak = down;
    return true;
  }
  if (std::strcmp(code, "ControlLeft") == 0 || std::strcmp(code, "ControlRight") == 0) {
    g_app.input.sprint = down;
    return true;
  }
  if (std::strcmp(code, "KeyF") == 0) {
    if (down && !repeat) {
      g_app.input.toggle_fly_pressed = true;
    }
    return true;
  }
  return false;
}

EM_BOOL on_key(int event_type, const EmscriptenKeyboardEvent* event, void*) {
  if (event == nullptr) {
    return EM_FALSE;
  }
  const bool down = event_type == EMSCRIPTEN_EVENT_KEYDOWN;
  if (set_key_state(event->code, down, event->repeat)) {
    return EM_TRUE;
  }
  return EM_FALSE;
}

EM_BOOL on_mouse_move(int, const EmscriptenMouseEvent* event, void*) {
  if (event == nullptr || !g_app.pointer_locked) {
    return EM_FALSE;
  }
  g_app.input.mouse_dx += event->movementX;
  g_app.input.mouse_dy += event->movementY;
  return EM_TRUE;
}

EM_BOOL on_mouse_button(int event_type, const EmscriptenMouseEvent* event, void*) {
  if (event == nullptr) {
    return EM_FALSE;
  }
  const bool down = event_type == EMSCRIPTEN_EVENT_MOUSEDOWN;
  if (event->button == 0) {
    if (down && !g_app.input.primary_held) {
      g_app.input.primary_pressed = true;
    }
    g_app.input.primary_held = down;
    return EM_TRUE;
  }
  if (event->button == 2) {
    if (down && !g_app.input.secondary_held) {
      g_app.input.secondary_pressed = true;
    }
    g_app.input.secondary_held = down;
    return EM_TRUE;
  }
  return EM_FALSE;
}

void initialize_web_player(
    const arv::client::ClientSessionWorldJoin& join,
    double now) {
  const float yaw = g_app.input.player.camera().yaw_radians();
  const float pitch = g_app.input.player.camera().pitch_radians();
  g_app.input.player = {};
  g_app.input.player.camera().set_rotation(yaw, pitch);
  g_app.input.player.enter_world(
      join.world_id,
      join.player_id,
      join.local_entity_id,
      join.spawn_position,
      join.initial_tick,
      join.tick_rate == 0U ? arv::DefaultBaseTps : join.tick_rate);
  g_app.input.last_frame_time = now;
  g_app.input.movement_accumulator_seconds = 0.0;
  g_app.input.movement_send_accumulator_seconds = 0.0;
  g_app.input.client_tick = join.initial_tick;
  g_app.input.player_initialized = true;
}

bool ensure_web_player_initialized(double now) {
  if (g_app.session == nullptr || !g_app.session->world_join().has_value()) {
    return false;
  }
  if (!g_app.input.player_initialized) {
    initialize_web_player(*g_app.session->world_join(), now);
  }
  return true;
}

void submit_web_movement(double now) {
  if (g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame ||
      !g_app.session->world_join().has_value()) {
    return;
  }

  const auto& join = *g_app.session->world_join();
  if (!ensure_web_player_initialized(now)) {
    return;
  }

  const double dt_seconds = std::clamp(
      now - g_app.input.last_frame_time,
      0.0,
      0.1);
  g_app.input.last_frame_time = now;

  arv::client::InputFrame input;
  input.move_forward = g_app.input.forward;
  input.move_backward = g_app.input.back;
  input.move_left = g_app.input.left;
  input.move_right = g_app.input.right;
  input.jump_pressed = g_app.input.jump_pressed;
  input.jump_held = g_app.input.jump;
  input.sneak_held = g_app.input.sneak;
  input.sprint_held = g_app.input.sprint;
  input.toggle_fly_pressed = g_app.input.toggle_fly_pressed;
  input.primary_pressed = g_app.input.primary_pressed;
  input.primary_held = g_app.input.primary_held;
  input.secondary_pressed = g_app.input.secondary_pressed;
  input.secondary_held = g_app.input.secondary_held;
  input.mouse_delta_x = static_cast<float>(g_app.input.mouse_dx);
  input.mouse_delta_y = static_cast<float>(g_app.input.mouse_dy);
  g_app.input.mouse_dx = 0.0;
  g_app.input.mouse_dy = 0.0;
  g_app.input.jump_pressed = false;
  g_app.input.toggle_fly_pressed = false;
  g_app.input.player.submit_input(std::move(input));

  const arv::TickRate tick_rate =
      join.tick_rate == 0U ? arv::DefaultBaseTps : join.tick_rate;
  const arv::Seconds tick_seconds = arv::tick_duration_seconds(tick_rate);
  g_app.input.movement_accumulator_seconds = std::min(
      g_app.input.movement_accumulator_seconds + dt_seconds,
      tick_seconds * 5.0);

  int fixed_steps = 0;
  while (g_app.input.movement_accumulator_seconds + 1.0e-9 >= tick_seconds &&
         fixed_steps < 5) {
    ++fixed_steps;
    g_app.input.movement_accumulator_seconds -= tick_seconds;

    const arv::u64 client_tick = g_app.input.client_tick++;
    const arv::player::PlayerCommand command =
        g_app.input.player.consume_command(client_tick);
    arv::web::WebChunkCollisionQuery collision_query(g_app.session->chunk_sections());
    g_app.input.player.update_local_movement(command, &collision_query, tick_seconds);
    g_app.input.movement_send_accumulator_seconds += tick_seconds;

    const arv::protocol::PlayerActionFlags action_flags =
        arv::client::action_flags_from_player_command(command);
    const bool has_action =
        action_flags != arv::protocol::PlayerActionFlags::None;
    const double send_interval =
        1.0 / static_cast<double>(std::max(g_app.input.movement_send_rate_hz, 1.0));
    if (g_app.input.movement_send_accumulator_seconds < send_interval && !has_action) {
      continue;
    }
    g_app.input.movement_send_accumulator_seconds = 0.0;

    arv::protocol::ServerboundPlayerMove move =
        g_app.input.player.build_movement_packet(0, client_tick, action_flags);
    (void)g_app.session->submit_player_move(move);
    g_app.input.primary_pressed = false;
    g_app.input.secondary_pressed = false;
  }
}

bool initialize_webgl() {
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = EM_TRUE;
  attributes.depth = EM_TRUE;
  attributes.stencil = EM_FALSE;
  attributes.antialias = EM_TRUE;
  attributes.preserveDrawingBuffer = EM_FALSE;
  attributes.enableExtensionsByDefault = EM_TRUE;
  attributes.majorVersion = 2;
  attributes.minorVersion = 0;

  g_app.context = emscripten_webgl_create_context("#arvane-canvas", &attributes);
  if (g_app.context <= 0) {
    set_status("failed to create WebGL2 context");
    return false;
  }
  if (emscripten_webgl_make_context_current(g_app.context) != EMSCRIPTEN_RESULT_SUCCESS) {
    set_status("failed to activate WebGL2 context");
    return false;
  }
  resize_canvas();
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearDepthf(1.0F);
  if (!initialize_chunk_renderer()) {
    return false;
  }
  set_status("WebGL2 ready");
  return true;
}

void install_callbacks() {
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_resize);
  emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_focus);
  emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_focus);
  emscripten_set_visibilitychange_callback(nullptr, EM_TRUE, on_visibility);
  emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_pointerlock);
  emscripten_set_click_callback("#arvane-canvas", nullptr, EM_TRUE, on_canvas_click);
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_key);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_key);
  emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_mouse_move);
  emscripten_set_mousedown_callback("#arvane-canvas", nullptr, EM_TRUE, on_mouse_button);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_mouse_button);
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int arv_web_crypto_self_test() {
  if (!arv::crypto::initialize_crypto()) {
    return 1;
  }

  const arv::client::ClientSessionIdentity identity =
      arv::client::make_ephemeral_client_session_identity("WebSelfTest");
  const arv::crypto::Ed25519KeyPair& key_pair = identity.key_pair;
  const std::array<std::byte, 17> short_message{
      std::byte{'a'}, std::byte{'r'}, std::byte{'v'}, std::byte{'a'},
      std::byte{'n'}, std::byte{'e'}, std::byte{'-'}, std::byte{'a'},
      std::byte{'u'}, std::byte{'t'}, std::byte{'h'}, std::byte{'-'},
      std::byte{'s'}, std::byte{'m'}, std::byte{'o'}, std::byte{'k'},
      std::byte{'e'}};
  const arv::crypto::Ed25519Signature short_signature =
      arv::crypto::sign_ed25519(short_message, key_pair.private_key);
  if (!arv::crypto::verify_ed25519(
          short_message,
          short_signature,
          key_pair.public_key)) {
    return 2;
  }

  arv::protocol::AuthServerInstanceID server_instance{};
  arv::protocol::AuthChallengeNonce nonce{};
  for (arv::usize i = 0; i < server_instance.size(); ++i) {
    server_instance[i] = static_cast<std::byte>(0x10U + i);
  }
  for (arv::usize i = 0; i < nonce.size(); ++i) {
    nonce[i] = static_cast<std::byte>(0x40U + i);
  }

  const arv::crypto::PlayerProfileID profile_id =
      arv::crypto::derive_profile_id_from_public_key(key_pair.public_key);
  const std::vector<arv::u8> signed_payload =
      arv::protocol::build_auth_signed_payload({
          arv::protocol::CurrentProtocolVersion,
          server_instance,
          arv::SessionID{42},
          profile_id,
          key_pair.public_key,
          nonce,
          9001});
  const auto* payload_bytes =
      reinterpret_cast<const std::byte*>(signed_payload.data());
  const std::span<const std::byte> payload_span{
      payload_bytes,
      signed_payload.size()};
  const arv::crypto::Ed25519Signature auth_signature =
      arv::crypto::sign_ed25519(payload_span, key_pair.private_key);
  if (!arv::crypto::verify_ed25519(
          payload_span,
          auth_signature,
          key_pair.public_key)) {
    return 3;
  }

  return 0;
}

EMSCRIPTEN_KEEPALIVE void arv_web_frame() {
  if (g_app.context <= 0 || !g_app.visible) {
    return;
  }

  const double now = emscripten_get_now() * 0.001;
  poll_session(now);
  resize_canvas();
  submit_web_movement(now);
  const float pulse = static_cast<float>(0.5 + 0.5 * std::sin((now - g_app.start_time) * 0.85));
  const float base = g_app.connected ? 0.16F : (g_app.connecting ? 0.12F : 0.09F);
  glClearColor(base + pulse * 0.04F, 0.12F + pulse * 0.03F, 0.14F + pulse * 0.02F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  render_remote_world();
}

EMSCRIPTEN_KEEPALIVE void arv_web_connect(const char* url) {
  if (arv_web_storage_ready() == 0) {
    set_status("browser storage is not ready");
    return;
  }
  if (url == nullptr || std::strlen(url) == 0U) {
    set_status("missing WebSocket URL");
    return;
  }

  if (g_app.session != nullptr) {
    g_app.session->disconnect("browser reconnect");
    g_app.session.reset();
    arv_web_flush_storage();
  }
  clear_block_render_assets();
  reset_input(true);
  auto transport = std::make_unique<arv::net::WebSocketClientTransport>();
  arv::client::ClientSessionIdentity identity =
      arv::client::make_ephemeral_client_session_identity("WebPlayer");
  identity.arvane_home = "/arvane_home";
  g_app.session = std::make_unique<arv::client::ClientSession>(
      std::move(transport),
      std::move(identity));
  arv::net::NetworkAddress address;
  address.host = url;
  address.port = 0U;
  auto connected = g_app.session->connect(address);
  if (!connected) {
    g_app.connected = false;
    g_app.connecting = false;
    g_app.session.reset();
    set_status("WebSocket connect failed: " + connected.error().message);
    return;
  }

  g_app.connected = false;
  g_app.connecting = true;
  g_app.flushed_after_pack_ready = false;
  g_app.last_connection_state = arv::net::ConnectionState::Connecting;
  set_status("connecting WebSocket");
}

EMSCRIPTEN_KEEPALIVE void arv_web_disconnect() {
  if (g_app.session != nullptr) {
    g_app.session->disconnect("browser disconnect");
    g_app.session.reset();
  }
  clear_block_render_assets();
  reset_input(true);
  arv_web_flush_storage();
  g_app.connected = false;
  g_app.connecting = false;
  g_app.flushed_after_pack_ready = false;
  g_app.last_connection_state = arv::net::ConnectionState::Disconnected;
  set_status("disconnected");
}

} // extern "C"

int main() {
  g_app.start_time = emscripten_get_now() * 0.001;
  set_status("starting Arvane web client");
  if (!initialize_webgl()) {
    return 1;
  }
  install_callbacks();
  arv_web_note_runtime_ready();
  emscripten_set_main_loop(arv_web_frame, 0, EM_TRUE);
  return 0;
}
