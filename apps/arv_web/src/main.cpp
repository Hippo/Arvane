#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include "arv/client/block_render_assets.hpp"
#include "arv/client/chunk_snapshot.hpp"
#include "arv/client/client_session.hpp"
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
  IndexedGpuMesh opaque{};
  IndexedGpuMesh transparent{};
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
  std::vector<ChunkGpuMesh> meshes{};
  GLuint entity_vao{};
  GLuint entity_vbo{};
  GLsizei entity_vertex_count{};
  bool announced_first_upload{};
  bool announced_first_entity_upload{};
};

struct WebBlockRenderAssets {
  arv::client::ClientBlockRenderAssets loaded{};
  std::vector<std::filesystem::path> pack_cache_paths{};
  std::string registry_snapshot_json{};
  bool ready{};
};

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

void clear_block_render_assets() {
  clear_chunk_meshes();
  destroy_chunk_texture();
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

void upload_chunk_mesh(const arv::protocol::ClientboundChunkSectionData& section) {
  if (!ensure_block_render_assets_loaded()) {
    return;
  }
  auto found = std::find_if(
      g_app.renderer.meshes.begin(),
      g_app.renderer.meshes.end(),
      [&](const ChunkGpuMesh& mesh) {
        return arv::web::same_chunk_section_coord(mesh.coord, section.coord);
      });
  if (found == g_app.renderer.meshes.end()) {
    found = g_app.renderer.meshes.insert(
        g_app.renderer.meshes.end(),
        ChunkGpuMesh{section.coord, section.chunk_content_version, section.chunk_light_version});
  } else {
    destroy_mesh(*found);
    found->content_version = section.chunk_content_version;
    found->light_version = section.chunk_light_version;
  }

  const arv::voxel::ChunkSnapshot snapshot =
      arv::client::chunk_snapshot_from_section_data(section);
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

void sync_entity_mesh() {
  destroy_entity_mesh();
  if (g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame) {
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
    const auto found = std::find_if(
        g_app.renderer.meshes.begin(),
        g_app.renderer.meshes.end(),
        [&](const ChunkGpuMesh& mesh) {
          return arv::web::same_chunk_section_coord(mesh.coord, section.coord);
        });
    if (found != g_app.renderer.meshes.end() &&
        found->content_version == section.chunk_content_version &&
        found->light_version == section.chunk_light_version) {
      continue;
    }
    upload_chunk_mesh(section);
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
  if (g_app.renderer.meshes.empty() && g_app.renderer.entity_vertex_count == 0) {
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const ChunkGpuMesh& mesh : g_app.renderer.meshes) {
      draw_chunk_mesh(mesh, mesh.transparent);
    }
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
