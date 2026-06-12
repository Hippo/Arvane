#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include "arv/client/client_session.hpp"
#include "arv/crypto/crypto_init.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/net/transport.hpp"
#include "arv/net/websocket_client_transport.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/protocol_version.hpp"
#include "web_chunk_mesh.hpp"
#include "web_player_movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

struct BrowserInputState {
  bool forward{};
  bool back{};
  bool left{};
  bool right{};
  bool jump{};
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
  arv::web::WebPlayerMovementState movement{};
  arv::player::PlayerTuning movement_tuning{arv::player::default_player_tuning()};
  arv::physics::PlayerMovementAuthority movement_authority{false, true};
  bool spawn_initialized{};
};

struct ChunkGpuMesh {
  arv::ChunkSectionCoord coord{};
  arv::u64 content_version{};
  GLuint vao{};
  GLuint vbo{};
  GLsizei vertex_count{};
};

struct ChunkRenderer {
  GLuint program{};
  GLint mvp_location{-1};
  std::vector<ChunkGpuMesh> meshes{};
  GLuint entity_vao{};
  GLuint entity_vbo{};
  GLsizei entity_vertex_count{};
  bool announced_first_upload{};
  bool announced_first_entity_upload{};
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

void destroy_mesh(ChunkGpuMesh& mesh) {
  if (mesh.vbo != 0U) {
    glDeleteBuffers(1, &mesh.vbo);
    mesh.vbo = 0U;
  }
  if (mesh.vao != 0U) {
    glDeleteVertexArrays(1, &mesh.vao);
    mesh.vao = 0U;
  }
  mesh.vertex_count = 0;
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

void reset_input(bool keep_view = false) {
  const float yaw = g_app.input.movement.yaw_radians;
  const float pitch = g_app.input.movement.pitch_radians;
  g_app.input = {};
  if (keep_view) {
    g_app.input.movement.yaw_radians = yaw;
    g_app.input.movement.pitch_radians = pitch;
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
  static constexpr const char* vertex_shader_source = R"(#version 300 es
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
  static constexpr const char* fragment_shader_source = R"(#version 300 es
precision highp float;
in vec3 v_color;
out vec4 out_color;
void main() {
  out_color = vec4(v_color, 1.0);
}
)";

  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
  if (vertex_shader == 0U) {
    return false;
  }
  const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
  if (fragment_shader == 0U) {
    glDeleteShader(vertex_shader);
    return false;
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
    set_status(std::string{"chunk renderer link failed: "} + log.data());
    glDeleteProgram(program);
    return false;
  }

  g_app.renderer.program = program;
  g_app.renderer.mvp_location = glGetUniformLocation(program, "u_mvp");
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

void upload_chunk_mesh(const arv::protocol::ClientboundChunkSectionData& section) {
  auto found = std::find_if(
      g_app.renderer.meshes.begin(),
      g_app.renderer.meshes.end(),
      [&](const ChunkGpuMesh& mesh) {
        return arv::web::same_chunk_section_coord(mesh.coord, section.coord);
      });
  if (found == g_app.renderer.meshes.end()) {
    found = g_app.renderer.meshes.insert(
        g_app.renderer.meshes.end(),
        ChunkGpuMesh{section.coord, section.chunk_content_version, 0U, 0U, 0});
  } else {
    destroy_mesh(*found);
    found->content_version = section.chunk_content_version;
  }

  const std::vector<arv::web::WebChunkVertex> vertices =
      arv::web::build_fallback_chunk_vertices(section);
  upload_vertices(found->vao, found->vbo, found->vertex_count, vertices, GL_STATIC_DRAW);

  if (found->vertex_count > 0 && !g_app.renderer.announced_first_upload) {
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

  std::vector<arv::web::WebEntityMarker> markers;
  markers.reserve(g_app.session->entity_mirrors().size());
  for (const arv::client::ClientSessionEntityMirror& mirror : g_app.session->entity_mirrors()) {
    markers.push_back({mirror.position, mirror.entity_type, mirror.state_flags});
  }
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
        found->content_version == section.chunk_content_version) {
      continue;
    }
    upload_chunk_mesh(section);
    if (++uploads_this_frame >= 1) {
      break;
    }
  }
}

void render_remote_world() {
  if (g_app.renderer.program == 0U ||
      g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame) {
    return;
  }
  sync_chunk_meshes();
  sync_entity_mesh();
  if (g_app.renderer.meshes.empty() && g_app.renderer.entity_vertex_count == 0) {
    return;
  }

  const float aspect = static_cast<float>(g_app.width) / static_cast<float>(std::max(g_app.height, 1));
  const arv::Vec3f eye =
      g_app.input.movement.position +
      arv::Vec3f{
          0.0F,
          arv::web::web_player_eye_height(
              g_app.input.movement,
              g_app.input.movement_tuning),
          0.0F};
  const arv::Vec3f center =
      eye + arv::view_forward(
                g_app.input.movement.yaw_radians,
                g_app.input.movement.pitch_radians);
  const arv::web::Mat4 mvp = arv::web::multiply(
      arv::web::perspective(arv::degrees_to_radians(70.0F), aspect, 0.05F, 512.0F),
      arv::web::look_at(eye, center, {0.0F, 1.0F, 0.0F}));

  glUseProgram(g_app.renderer.program);
  glUniformMatrix4fv(g_app.renderer.mvp_location, 1, GL_FALSE, mvp.values.data());
  for (const ChunkGpuMesh& mesh : g_app.renderer.meshes) {
    if (mesh.vertex_count == 0 || mesh.vao == 0U) {
      continue;
    }
    glBindVertexArray(mesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh.vertex_count);
  }
  if (g_app.renderer.entity_vertex_count > 0 && g_app.renderer.entity_vao != 0U) {
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

void poll_session() {
  if (g_app.session == nullptr) {
    return;
  }

  g_app.session->pump();
  const arv::net::ConnectionState state = g_app.session->connection_state();
  if (state == arv::net::ConnectionState::InGame) {
    if (std::optional<arv::protocol::ClientboundPlayerMoveCorrection> correction =
            g_app.session->take_player_move_correction();
        correction.has_value()) {
      arv::web::apply_web_player_move_correction(
          g_app.input.movement,
          correction.value());
      g_app.input.spawn_initialized = true;
      set_status("server movement correction applied");
    }
    if (std::optional<arv::protocol::ClientboundPlayerVelocity> velocity =
            g_app.session->take_player_velocity();
        velocity.has_value() && g_app.session->world_join().has_value()) {
      arv::web::apply_web_player_velocity(
          g_app.input.movement,
          velocity.value(),
          g_app.session->world_join()->player_id);
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
      arv::web::apply_web_player_permissions(
          g_app.input.movement,
          g_app.input.movement_authority,
          permissions.value());
    }
    if (std::optional<arv::protocol::ClientboundPlayerMovementTuning> tuning =
            g_app.session->take_player_movement_tuning();
        tuning.has_value()) {
      arv::web::apply_web_player_movement_tuning(
          g_app.input.movement_tuning,
          tuning.value());
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
    clear_chunk_meshes();
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

void submit_web_movement(double now) {
  if (g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame ||
      !g_app.session->world_join().has_value()) {
    return;
  }

  const auto& join = *g_app.session->world_join();
  if (!g_app.input.spawn_initialized) {
    const float yaw = g_app.input.movement.yaw_radians;
    const float pitch = g_app.input.movement.pitch_radians;
    g_app.input.movement = {};
    g_app.input.movement.position = join.spawn_position;
    g_app.input.movement.yaw_radians = yaw;
    g_app.input.movement.pitch_radians = pitch;
    g_app.input.last_frame_time = now;
    g_app.input.movement_accumulator_seconds = 0.0;
    g_app.input.movement_send_accumulator_seconds = 0.0;
    g_app.input.client_tick = join.initial_tick;
    g_app.input.spawn_initialized = true;
  }

  const double dt_seconds = std::clamp(
      now - g_app.input.last_frame_time,
      0.0,
      0.1);
  g_app.input.last_frame_time = now;

  arv::web::WebPlayerMovementInput movement_input;
  movement_input.forward = g_app.input.forward;
  movement_input.back = g_app.input.back;
  movement_input.left = g_app.input.left;
  movement_input.right = g_app.input.right;
  movement_input.jump = g_app.input.jump;
  movement_input.sneak = g_app.input.sneak;
  movement_input.sprint = g_app.input.sprint;
  movement_input.toggle_fly_pressed = g_app.input.toggle_fly_pressed;
  movement_input.mouse_dx = static_cast<float>(g_app.input.mouse_dx);
  movement_input.mouse_dy = static_cast<float>(g_app.input.mouse_dy);
  g_app.input.mouse_dx = 0.0;
  g_app.input.mouse_dy = 0.0;
  arv::web::apply_web_player_look(
      g_app.input.movement,
      movement_input.mouse_dx,
      movement_input.mouse_dy,
      g_app.input.movement_tuning);
  movement_input.mouse_dx = 0.0F;
  movement_input.mouse_dy = 0.0F;

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

    const arv::web::WebPlayerMovementStep movement_step =
        arv::web::simulate_web_player_movement(
            g_app.input.movement,
            movement_input,
            g_app.session->chunk_sections(),
            tick_seconds,
            g_app.input.movement_tuning,
            g_app.input.movement_authority);
    g_app.input.movement = movement_step.state;
    g_app.input.movement_send_accumulator_seconds += tick_seconds;

    arv::protocol::PlayerActionFlags action_flags = arv::protocol::PlayerActionFlags::None;
    if (g_app.input.primary_pressed) {
      action_flags |= arv::protocol::PlayerActionFlags::PrimaryPressed;
    }
    if (g_app.input.primary_held) {
      action_flags |= arv::protocol::PlayerActionFlags::PrimaryHeld;
    }
    if (g_app.input.secondary_pressed) {
      action_flags |= arv::protocol::PlayerActionFlags::SecondaryPressed;
    }
    if (g_app.input.secondary_held) {
      action_flags |= arv::protocol::PlayerActionFlags::SecondaryHeld;
    }
    if (g_app.input.toggle_fly_pressed) {
      action_flags |= arv::protocol::PlayerActionFlags::ToggleFlyPressed;
    }
    const bool has_action =
        action_flags != arv::protocol::PlayerActionFlags::None;
    const double send_interval =
        1.0 / static_cast<double>(std::max(g_app.input.movement_send_rate_hz, 1.0));
    const arv::u64 client_tick = g_app.input.client_tick++;
    if (g_app.input.movement_send_accumulator_seconds < send_interval && !has_action) {
      movement_input.toggle_fly_pressed = false;
      continue;
    }
    g_app.input.movement_send_accumulator_seconds = 0.0;

    arv::protocol::ServerboundPlayerMove move;
    move.client_tick = client_tick;
    move.position = g_app.input.movement.position;
    move.velocity = g_app.input.movement.velocity;
    move.yaw_radians = g_app.input.movement.yaw_radians;
    move.pitch_radians = g_app.input.movement.pitch_radians;
    move.flags = movement_step.flags;
    move.action_flags = action_flags;
    (void)g_app.session->submit_player_move(move);
    g_app.input.primary_pressed = false;
    g_app.input.secondary_pressed = false;
    g_app.input.toggle_fly_pressed = false;
    movement_input.toggle_fly_pressed = false;
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

  poll_session();
  resize_canvas();
  const double now = emscripten_get_now() * 0.001;
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
  clear_chunk_meshes();
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
  clear_chunk_meshes();
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
