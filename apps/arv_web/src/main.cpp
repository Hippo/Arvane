#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include "arv/client/block_render_assets.hpp"
#include "arv/client/block_outline_target.hpp"
#include "arv/client/chunk_collision_query.hpp"
#include "arv/client/chunk_snapshot.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/gameplay_interactions.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/client/item_rendering.hpp"
#include "arv/client/item_ui_model.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/client/player_skin_rendering.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/client/sky_rendering.hpp"
#include "arv/client/gameplay_ui_model.hpp"
#include "arv/client/ui_interactions.hpp"
#include "arv/base/ui/screens/pause_menu.hpp"
#include "arv/base/ui/widgets/gameplay.hpp"
#include "arv/crypto/crypto_init.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/io/config.hpp"
#include "arv/net/transport.hpp"
#include "arv/net/websocket_client_transport.hpp"
#include "arv/player/inventory.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/protocol_version.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "arv/render/render_culling.hpp"
#include "arv/text/language.hpp"
#include "arv/text/rich_text.hpp"
#include "arv/ui/core/ui.hpp"
#include "web_application.hpp"
#include "web_entity_fallback.hpp"
#include "web_input.hpp"
#include "web_platform.hpp"
#include "web_player_skin_atlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace arv::web;

void disconnect_web_session();

inline constexpr arv::i32 MaxConfigurableChunkDistance = 32;
inline constexpr arv::Seconds BlockContainerAnimationLengthSeconds = 0.5;
inline constexpr std::string_view BlockContainerOpenAnimation = "open";
inline constexpr std::string_view BlockContainerCloseAnimation = "close";

[[nodiscard]] arv::ui::RenderSettingsView default_web_render_settings() noexcept {
  arv::ui::RenderSettingsView settings;
  settings.load_distance = 4;
  settings.view_distance = 4;
  settings.stream_distance = 4;
  return settings;
}

[[nodiscard]] arv::ui::RenderSettingsView normalized_web_render_settings(
    arv::ui::RenderSettingsView settings) noexcept {
  settings.load_distance =
      std::clamp(settings.load_distance, 1, MaxConfigurableChunkDistance);
  settings.simulation_distance =
      std::clamp(settings.simulation_distance, 0, settings.load_distance);
  settings.stream_distance =
      std::clamp(settings.stream_distance, 1, settings.load_distance);
  settings.view_distance =
      std::clamp(settings.view_distance, 1, settings.stream_distance);
  settings.fps_cap = std::clamp<arv::u32>(settings.fps_cap, 30U, 240U);
  settings.mesh_upload_budget =
      std::clamp<arv::u32>(settings.mesh_upload_budget, 1U, 32U);
  settings.star_intensity = std::clamp(settings.star_intensity, 0.0F, 3.0F);
  settings.cloud_intensity = std::clamp(settings.cloud_intensity, 0.0F, 3.0F);
  settings.cirrus_amount = std::clamp(settings.cirrus_amount, 0.0F, 1.0F);
  settings.cumulus_amount = std::clamp(settings.cumulus_amount, 0.0F, 1.0F);
  settings.outline_red = std::clamp(settings.outline_red, 0.0F, 1.0F);
  settings.outline_green = std::clamp(settings.outline_green, 0.0F, 1.0F);
  settings.outline_blue = std::clamp(settings.outline_blue, 0.0F, 1.0F);
  settings.outline_thickness = std::clamp(settings.outline_thickness, 0.1F, 2.0F);
  return settings;
}

[[nodiscard]] arv::ui::ControlsSettingsView normalized_web_controls_settings(
    arv::ui::ControlsSettingsView settings) noexcept {
  settings.mouse_sensitivity_percent =
      std::clamp(settings.mouse_sensitivity_percent, 10.0F, 300.0F);
  return settings;
}

[[nodiscard]] arv::ui::InterfaceSettingsView normalized_web_interface_settings(
    arv::ui::InterfaceSettingsView settings) noexcept {
  settings.chat_history_max =
      std::clamp<arv::u32>(settings.chat_history_max, 100U, 5000U);
  settings.chat_open_lines =
      std::clamp<arv::u32>(settings.chat_open_lines, 1U, 16U);
  settings.chat_passive_lines =
      std::clamp<arv::u32>(settings.chat_passive_lines, 0U, 16U);
  settings.chat_passive_visible_seconds =
      std::clamp(settings.chat_passive_visible_seconds, 0.0F, 30.0F);
  settings.chat_passive_fade_seconds =
      std::clamp(settings.chat_passive_fade_seconds, 0.0F, 30.0F);
  return settings;
}

[[nodiscard]] arv::player::PlayerTuning web_player_tuning(
    const arv::ui::ControlsSettingsView& settings) noexcept {
  arv::player::PlayerTuning tuning = arv::player::default_player_tuning();
  tuning.mouse_sensitivity_radians =
      arv::player::DefaultMouseSensitivityRadians *
      (std::clamp(settings.mouse_sensitivity_percent, 10.0F, 300.0F) / 100.0F);
  return tuning;
}

[[nodiscard]] arv::platform::InputDeviceType platform_device_from(
    arv::ui::InputBindingDevice type) noexcept {
  switch (type) {
    case arv::ui::InputBindingDevice::Keyboard:
      return arv::platform::InputDeviceType::Keyboard;
    case arv::ui::InputBindingDevice::MouseButton:
      return arv::platform::InputDeviceType::MouseButton;
    case arv::ui::InputBindingDevice::MouseWheel:
      return arv::platform::InputDeviceType::MouseWheel;
  }
  return arv::platform::InputDeviceType::Keyboard;
}

[[nodiscard]] arv::ui::InputBindingDevice ui_device_from(
    arv::platform::InputDeviceType type) noexcept {
  switch (type) {
    case arv::platform::InputDeviceType::Keyboard:
      return arv::ui::InputBindingDevice::Keyboard;
    case arv::platform::InputDeviceType::MouseButton:
      return arv::ui::InputBindingDevice::MouseButton;
    case arv::platform::InputDeviceType::MouseWheel:
      return arv::ui::InputBindingDevice::MouseWheel;
  }
  return arv::ui::InputBindingDevice::Keyboard;
}

[[nodiscard]] std::string binding_conflict_key(
    const arv::platform::InputBinding& binding) {
  return std::string{arv::platform::input_device_type_name(binding.device_type)} + ":" +
         binding.input_code + ":" + std::to_string(binding.modifiers);
}

[[nodiscard]] arv::ui::InputBindingView input_binding_view_from(
    const arv::platform::InputBinding& binding) {
  return {
      true,
      ui_device_from(binding.device_type),
      binding.input_code,
      arv::platform::input_binding_label(binding),
      binding.modifiers};
}

[[nodiscard]] std::optional<arv::platform::InputBinding> platform_binding_from(
    const arv::ui::InputBindingView& binding) {
  const arv::platform::InputDeviceType device = platform_device_from(binding.device);
  const std::optional<std::string> canonical =
      arv::platform::canonical_input_code(device, binding.input_code);
  if (!canonical.has_value()) {
    return std::nullopt;
  }
  return arv::platform::InputBinding{device, canonical.value(), binding.modifiers};
}

[[nodiscard]] std::string display_name_from_action(std::string_view action_name) {
  std::string display;
  display.reserve(action_name.size() + 4U);
  for (arv::usize index = 0; index < action_name.size(); ++index) {
    const char ch = action_name[index];
    if (index > 0U && ch >= 'A' && ch <= 'Z') {
      display.push_back(' ');
    }
    display.push_back(ch);
  }
  return display;
}

class WebUiBridge final : public arv::ui::UICommandSink,
                          public arv::ui::UIScreenDataSource,
                          public arv::ui::UIClipboard {
public:
  void submit(arv::ui::UICommand command) override {
    if (std::holds_alternative<arv::ui::BackToMainMenuCommand>(command) ||
        std::holds_alternative<arv::ui::QuitGameCommand>(command)) {
      disconnect_web_session();
      return;
    }
    if (std::holds_alternative<arv::ui::ResumeGameCommand>(command)) {
      return;
    }
    if (const auto* request =
            std::get_if<arv::ui::SetRenderSettingsCommand>(&command);
        request != nullptr) {
      apply_render_settings(request->settings);
      return;
    }
    if (const auto* request =
            std::get_if<arv::ui::SetControlsSettingsCommand>(&command);
        request != nullptr) {
      apply_controls_settings(request->settings);
      return;
    }
    if (const auto* request =
            std::get_if<arv::ui::SetInterfaceSettingsCommand>(&command);
        request != nullptr) {
      apply_interface_settings(request->settings);
      return;
    }
    if (const auto* request = std::get_if<arv::ui::SetKeyBindCommand>(&command);
        request != nullptr) {
      apply_key_bind(*request);
      return;
    }
    if (const auto* request = std::get_if<arv::ui::ResetKeyBindCommand>(&command);
        request != nullptr) {
      apply_key_bind_reset(*request);
      return;
    }
  }

  [[nodiscard]] arv::ui::RenderSettingsView render_settings() const override {
    return render_settings_;
  }

  [[nodiscard]] arv::ui::ControlsSettingsView controls_settings() const override {
    return controls_settings_;
  }

  [[nodiscard]] arv::ui::InterfaceSettingsView interface_settings() const override {
    return interface_settings_;
  }

  [[nodiscard]] std::vector<arv::ui::KeyBindEntryView> key_bind_entries() const override {
    std::vector<arv::ui::KeyBindEntryView> entries;
    arv::HashMap<std::string, arv::u32> binding_counts;
    for (arv::usize index = 0;
         index < static_cast<arv::usize>(arv::platform::LogicalAction::Count);
         ++index) {
      const auto action = static_cast<arv::platform::LogicalAction>(index);
      for (const arv::platform::InputBinding& binding :
           arv::platform::bindings_for_action(g_app.input_bindings, action)) {
        ++binding_counts[binding_conflict_key(binding)];
      }
    }

    entries.reserve(static_cast<arv::usize>(arv::platform::LogicalAction::Count));
    for (arv::usize index = 0;
         index < static_cast<arv::usize>(arv::platform::LogicalAction::Count);
         ++index) {
      const auto action = static_cast<arv::platform::LogicalAction>(index);
      arv::ui::KeyBindEntryView entry;
      entry.action_name = std::string{arv::platform::logical_action_name(action)};
      entry.display_name = display_name_from_action(entry.action_name);
      const auto& bindings = arv::platform::bindings_for_action(g_app.input_bindings, action);
      if (!bindings.empty()) {
        entry.primary.has_binding = true;
        entry.primary.binding = input_binding_view_from(bindings[0]);
        entry.primary.conflict = binding_counts[binding_conflict_key(bindings[0])] > 1U;
      }
      if (bindings.size() > 1U) {
        entry.secondary.has_binding = true;
        entry.secondary.binding = input_binding_view_from(bindings[1]);
        entry.secondary.conflict = binding_counts[binding_conflict_key(bindings[1])] > 1U;
      }
      entries.push_back(std::move(entry));
    }
    return entries;
  }

  [[nodiscard]] std::string text() const override {
    return clipboard_;
  }

  void set_text(std::string text) override {
    clipboard_ = std::move(text);
  }

private:
  void apply_render_settings(arv::ui::RenderSettingsView settings) {
    render_settings_ = normalized_web_render_settings(settings);
    if (g_app.session != nullptr) {
      g_app.session->set_chunk_view_distance(render_settings_.stream_distance);
    }
  }

  void apply_controls_settings(arv::ui::ControlsSettingsView settings) {
    controls_settings_ = normalized_web_controls_settings(settings);
    if (g_app.session != nullptr) {
      g_app.session->set_player_tuning(web_player_tuning(controls_settings_));
    }
  }

  void apply_interface_settings(arv::ui::InterfaceSettingsView settings) {
    interface_settings_ = normalized_web_interface_settings(settings);
    if (g_app.session != nullptr) {
      g_app.session->configure_chat({interface_settings_.chat_history_max});
    }
  }

  void apply_key_bind(const arv::ui::SetKeyBindCommand& command) {
    const std::optional<arv::platform::LogicalAction> action =
        arv::platform::logical_action_from_name(command.action_name);
    if (!action.has_value() || command.slot > 1U) {
      return;
    }

    auto& action_bindings =
        arv::platform::bindings_for_action(g_app.input_bindings, action.value());
    if (command.binding.has_value()) {
      const std::optional<arv::platform::InputBinding> binding =
          platform_binding_from(command.binding.value());
      if (!binding.has_value()) {
        return;
      }
      if (action_bindings.size() <= command.slot) {
        const arv::platform::InputBindings defaults =
            arv::platform::default_input_bindings();
        const auto& default_bindings =
            arv::platform::bindings_for_action(defaults, action.value());
        while (action_bindings.size() <= command.slot) {
          if (action_bindings.empty() && !default_bindings.empty()) {
            action_bindings.push_back(default_bindings.front());
          } else {
            action_bindings.emplace_back(binding.value());
          }
        }
      }
      action_bindings[command.slot] = binding.value();
    } else if (command.slot == 1U && action_bindings.size() > 1U) {
      action_bindings.erase(action_bindings.begin() + 1);
    } else {
      return;
    }

    if (action_bindings.size() > 2U) {
      action_bindings.resize(2U);
    }
  }

  void apply_key_bind_reset(const arv::ui::ResetKeyBindCommand& command) {
    const std::optional<arv::platform::LogicalAction> action =
        arv::platform::logical_action_from_name(command.action_name);
    if (!action.has_value()) {
      return;
    }
    const arv::platform::InputBindings defaults =
        arv::platform::default_input_bindings();
    auto& action_bindings =
        arv::platform::bindings_for_action(g_app.input_bindings, action.value());
    action_bindings = arv::platform::bindings_for_action(defaults, action.value());
    if (action_bindings.size() > 2U) {
      action_bindings.resize(2U);
    }
  }

  arv::ui::RenderSettingsView render_settings_{
      normalized_web_render_settings(default_web_render_settings())};
  arv::ui::ControlsSettingsView controls_settings_{
      normalized_web_controls_settings({})};
  arv::ui::InterfaceSettingsView interface_settings_{
      normalized_web_interface_settings({})};
  std::string clipboard_{};
};

WebUiBridge g_ui_bridge;

[[nodiscard]] arv::io::UiConfig::ChatConfig web_chat_config() noexcept {
  const arv::ui::InterfaceSettingsView settings = g_ui_bridge.interface_settings();
  arv::io::UiConfig::ChatConfig config;
  config.max_history_messages = settings.chat_history_max;
  config.open_max_lines = settings.chat_open_lines;
  config.passive_max_lines = settings.chat_passive_lines;
  config.passive_visible_seconds = settings.chat_passive_visible_seconds;
  config.passive_fade_seconds = settings.chat_passive_fade_seconds;
  return config;
}

[[nodiscard]] arv::io::RenderConfig::SkyConfig web_sky_config() noexcept {
  const arv::ui::RenderSettingsView settings = g_ui_bridge.render_settings();
  arv::io::RenderConfig::SkyConfig config;
  config.star_intensity = settings.star_intensity;
  config.cloud_intensity = settings.cloud_intensity;
  config.cirrus_amount = settings.cirrus_amount;
  config.cumulus_amount = settings.cumulus_amount;
  return config;
}

[[nodiscard]] arv::ui::UiDisplayMetrics web_ui_metrics() {
  const float framebuffer_width =
      static_cast<float>(std::max(g_app.width, 1));
  const float framebuffer_height =
      static_cast<float>(std::max(g_app.height, 1));
  const float scale = static_cast<float>(std::max(0.25, device_pixel_ratio()));
  return arv::ui::UiDisplayMetrics::from_window_and_framebuffer(
      {
          framebuffer_width / scale,
          framebuffer_height / scale,
      },
      {
          framebuffer_width,
          framebuffer_height,
      });
}

[[nodiscard]] arv::ui::UIContext make_web_ui_context() {
  const arv::ui::UiDisplayMetrics metrics = web_ui_metrics();
  return {
      g_app.ui.tree,
      metrics.logical_size,
      &g_ui_bridge,
      &g_ui_bridge,
      &g_app.ui.screen_manager,
      &g_app.ui.theme,
      &g_app.ui.language,
      &g_ui_bridge};
}

void reset_gameplay_screen_stack() {
  g_app.ui.screen_manager.clear();
  g_app.ui.screen_manager.push(
      arv::ui::make_gameplay_screen({.show_quit = false}));
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

void destroy_player_skin_texture() {
  if (g_app.renderer.player_skin_texture != 0U) {
    glDeleteTextures(1, &g_app.renderer.player_skin_texture);
  }
  g_app.renderer.player_skin_texture = 0U;
  g_app.renderer.player_skin_texture_width = 0U;
  g_app.renderer.player_skin_texture_height = 0U;
  g_app.renderer.player_skin_texture_layers = 0U;
  g_app.renderer.player_skin_allocator.clear();
  g_app.renderer.player_skin_regions.clear();
}

void destroy_item_texture() {
  if (g_app.renderer.item_texture != 0U) {
    glDeleteTextures(1, &g_app.renderer.item_texture);
  }
  g_app.renderer.item_texture = 0U;
  g_app.renderer.item_texture_width = 0U;
  g_app.renderer.item_texture_height = 0U;
}

void destroy_font_texture() {
  if (g_app.renderer.font_texture != 0U) {
    glDeleteTextures(1, &g_app.renderer.font_texture);
  }
  g_app.renderer.font_texture = 0U;
  g_app.renderer.font_texture_width = 0U;
  g_app.renderer.font_texture_height = 0U;
}

void clear_web_ui_assets() {
  destroy_font_texture();
  g_app.ui.text_measurer.reset();
  g_app.ui.fonts = arv::asset::FontSystem{};
  g_app.ui.font_stacks = {};
  g_app.ui.ready = false;
}

void clear_entity_render_assets() {
  destroy_entity_mesh();
  destroy_entity_gpu_models();
  destroy_entity_texture();
  destroy_player_skin_texture();
  destroy_item_texture();
  g_app.entity_assets = {};
  g_app.renderer.announced_first_entity_upload = false;
}

void clear_block_render_assets() {
  clear_chunk_meshes();
  destroy_chunk_texture();
  clear_entity_render_assets();
  clear_web_ui_assets();
  g_app.block_assets = {};
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
  static constexpr const char* sky_vertex_shader_source = R"(#version 300 es
precision highp float;
out vec3 v_view_ray;

uniform vec3 u_camera_forward;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;
uniform float u_camera_aspect;
uniform float u_camera_fov_y_degrees;

void main() {
  vec2 ndc = vec2(
      (gl_VertexID == 2 || gl_VertexID == 3) ? 1.0 : -1.0,
      (gl_VertexID == 1 || gl_VertexID == 3) ? -1.0 : 1.0);
  float focal_spread = tan(radians(u_camera_fov_y_degrees) * 0.5);
  v_view_ray =
      u_camera_forward +
      u_camera_right * (ndc.x * max(u_camera_aspect, 0.01) * focal_spread) +
      u_camera_up * (ndc.y * focal_spread);
  gl_Position = vec4(ndc, 1.0, 1.0);
}
)";
  static constexpr const char* sky_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
in vec3 v_view_ray;
out vec4 out_color;

uniform vec3 u_sun_direction;
uniform vec3 u_moon_direction;
uniform vec3 u_daylight_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform float u_daylight_strength;
uniform float u_moonlight_strength;
uniform float u_star_visibility;
uniform float u_day_fraction;
uniform float u_cloud_time_seconds;
uniform float u_cirrus_amount;
uniform float u_cumulus_amount;
uniform float u_cloud_intensity;
uniform float u_exposure;

const float PI = 3.14159265359;
const float Br = 0.0025;
const float Bm = 0.0003;
const float MieG = 0.9800;
const vec3 Wavelength = vec3(0.650, 0.570, 0.475);
const float EffectEpsilon = 0.001;

float hash(float n) {
  return fract(sin(n) * 43758.5453123);
}

float noise(vec3 x) {
  vec3 f = fract(x);
  vec3 p = floor(x);
  float n = dot(p, vec3(1.0, 157.0, 113.0));
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash(n +   0.0), hash(n +   1.0), f.x),
                 mix(hash(n + 157.0), hash(n + 158.0), f.x), f.y),
             mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
                 mix(hash(n + 270.0), hash(n + 271.0), f.x), f.y), f.z);
}

const mat3 FbmMatrix = mat3(
   0.00,  1.60,  1.20,
  -1.60,  0.72, -0.96,
  -1.20, -0.96,  1.28
);

float fbm(vec3 p) {
  float f = 0.0;
  f += noise(p) * 0.500; p = FbmMatrix * p * 1.10;
  f += noise(p) * 0.250; p = FbmMatrix * p * 1.20;
  f += noise(p) * 0.166; p = FbmMatrix * p * 1.30;
  f += noise(p) * 0.083; p = FbmMatrix * p * 1.40;
  f += noise(p) * 0.042;
  return f;
}

vec3 view_direction() {
  return normalize(v_view_ray);
}

vec3 scattering_color(vec3 view_dir, float sun_height, float day_amount) {
  vec3 kr = Br / pow(Wavelength, vec3(4.0));
  vec3 km = Bm / pow(Wavelength, vec3(0.84));
  float horizon = max(view_dir.y, 0.0);
  float mu = dot(view_dir, u_sun_direction);
  float rayleigh = 3.0 / (8.0 * PI) * (1.0 + mu * mu);
  vec3 mie = (kr + km * (1.0 - MieG * MieG) /
      (2.0 + MieG * MieG) /
      pow(max(1.0 + MieG * MieG - 2.0 * MieG * mu, 0.001), 1.5)) / (Br + Bm);

  float low_horizon = exp(-horizon * 16.0) + 0.1;
  vec3 day_extinction =
      exp(-exp(-((horizon + sun_height * 4.0) *
                  low_horizon / 80.0) / Br) *
          low_horizon * kr / Br) *
      exp(-horizon * exp(-horizon * 8.0) * 4.0) *
      exp(-horizon * 2.0) * 4.0;

  vec3 sky = rayleigh * mie * day_extinction * u_daylight_color;
  vec3 zenith_tint = mix(u_fog_color, vec3(0.11, 0.34, 0.90) * u_daylight_color,
                         pow(clamp(horizon, 0.0, 1.0), 0.35));
  sky = mix(zenith_tint * u_daylight_strength, sky, 0.65);

  vec3 night = vec3(0.010, 0.016, 0.045) +
      u_ambient_color * 0.55 +
      vec3(0.08, 0.10, 0.15) * u_moonlight_strength *
          smoothstep(-0.08, 0.45, u_moon_direction.y);
  return mix(night, sky, day_amount);
}

float star_field(vec3 view_dir) {
  vec3 cell = floor(view_dir * 300.0);
  float seed = dot(cell, vec3(17.0, 59.0, 113.0));
  float star = smoothstep(0.9925, 1.0, hash(seed));
  float sparkle = 0.78 + 0.22 * sin(u_cloud_time_seconds * 0.45 + hash(seed + 19.0) * 6.28318);
  float scale = mix(0.55, 1.25, hash(seed + 11.0));
  return star * sparkle * scale;
}

vec3 apply_clouds(vec3 color, vec3 view_dir, float day_amount) {
  float cirrus_amount = clamp(u_cirrus_amount, 0.0, 1.0);
  float cumulus_amount = clamp(u_cumulus_amount, 0.0, 1.0);
  float cloud_intensity = clamp(u_cloud_intensity, 0.0, 3.0);
  if (cloud_intensity <= EffectEpsilon ||
      (cirrus_amount <= EffectEpsilon && cumulus_amount <= EffectEpsilon)) {
    return color;
  }

  float height_mask = smoothstep(-0.03, 0.16, view_dir.y);
  if (height_mask <= EffectEpsilon) {
    return color;
  }

  vec3 plane = view_dir / max(view_dir.y, 0.080);

  float cirrus = 0.0;
  if (cirrus_amount > EffectEpsilon) {
    vec3 cirrus_wind = vec3(u_cloud_time_seconds * 0.010, 0.0, u_cloud_time_seconds * 0.004);
    float cirrus_base = fbm(plane * 1.15 + cirrus_wind);
    float cirrus_detail = fbm(plane * 3.40 + cirrus_wind * 1.7 + vec3(0.0, 8.0, 0.0));
    cirrus = smoothstep(0.50, 0.78, cirrus_base + cirrus_detail * 0.18) *
        cirrus_amount * 0.55;
  }

  float cumulus = 0.0;
  if (cumulus_amount > EffectEpsilon) {
    vec3 cumulus_wind = vec3(u_cloud_time_seconds * 0.026, 4.2, u_cloud_time_seconds * 0.016);
    float cumulus_base = fbm(plane * 0.55 + cumulus_wind);
    float cumulus_detail = fbm(plane * 2.10 + cumulus_wind * 1.3 + vec3(7.0, 0.0, 0.0));
    float cumulus_shape = cumulus_base * 0.82 + cumulus_detail * 0.18;
    cumulus = smoothstep(0.48 - cumulus_amount * 0.22, 0.76, cumulus_shape) *
        cumulus_amount;
  }

  float density = clamp((cirrus + cumulus) * cloud_intensity, 0.0, 1.0) * height_mask;
  float sun_lit = smoothstep(0.15, 0.95, dot(view_dir, u_sun_direction)) *
      day_amount;
  float moon_lit = smoothstep(-0.02, 0.42, u_moon_direction.y) *
      u_moonlight_strength * (1.0 - day_amount);
  vec3 cloud_shadow = mix(
      vec3(0.035, 0.040, 0.065) + u_ambient_color * 0.65,
      u_fog_color * 0.92 + u_daylight_color * 0.15,
      day_amount);
  vec3 cloud_bright = mix(
      vec3(0.10, 0.12, 0.18) + u_ambient_color * 0.50,
      u_daylight_color * 0.92 + u_fog_color * 0.70,
      day_amount);
  vec3 cloud_light = mix(
      cloud_shadow,
      cloud_bright,
      clamp(day_amount * 0.38 + sun_lit * 0.35 + moon_lit * 1.60, 0.0, 0.85));
  float opacity = density * (0.56 - day_amount * 0.16 + day_amount * 0.42);
  return mix(color, cloud_light, clamp(opacity, 0.0, 0.88));
}

vec3 apply_moon(vec3 color, vec3 view_dir, float day_amount) {
  if (u_moon_direction.y <= -0.02) {
    return color;
  }

  float moon_dot = dot(view_dir, u_moon_direction);
  float core = smoothstep(0.99955, 0.99988, moon_dot);
  float rim = smoothstep(0.99840, 0.99960, moon_dot) * (1.0 - core) * 0.65;
  float glow = smoothstep(0.98600, 0.99920, moon_dot) * 0.18;
  float horizon = smoothstep(-0.02, 0.10, u_moon_direction.y) *
      smoothstep(-0.02, 0.08, view_dir.y);
  float night_visibility = (1.0 - smoothstep(0.02, 0.30, u_sun_direction.y)) *
      (1.0 - day_amount * 0.35);
  vec3 moon_color = mix(vec3(0.78, 0.84, 1.0), vec3(1.0, 0.94, 0.82), 0.65);
  float strength = (core * 1.65 + rim * 0.55 + glow) *
      horizon * night_visibility * (0.55 + u_moonlight_strength * 3.0);
  return color + moon_color * strength;
}

void main() {
  vec3 view_dir = view_direction();
  float sun_height = u_sun_direction.y;
  float day_amount = smoothstep(-0.08, 0.16, sun_height);
  float above_horizon = smoothstep(-0.02, 0.10, view_dir.y);

  vec3 color = scattering_color(view_dir, sun_height, day_amount);
  float star_visibility = max(u_star_visibility, 0.0);
  if (star_visibility > EffectEpsilon && above_horizon > EffectEpsilon) {
    color += vec3(0.70, 0.78, 1.0) *
        star_field(view_dir) * star_visibility * above_horizon;
  }
  color = apply_clouds(color, view_dir, day_amount);
  color = apply_moon(color, view_dir, day_amount);

  color = mix(u_fog_color * 0.75, color, smoothstep(-0.10, 0.18, view_dir.y));
  color += hash(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)) + u_day_fraction * 31.0) * 0.006;
  color = vec3(1.0) - exp(-max(color, vec3(0.0)) * max(u_exposure, 0.01));
  out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";
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
uniform float u_animation_time_seconds;
out vec2 v_uv;
out float v_light;
flat out uint v_atlas_page;
flat out uint v_material_class;
flat out uint v_animation_id;
flat out uint v_material_flags;
const uint VertexAnimationWaterStill = 1u;
const uint VertexAnimationWaterFlow = 2u;
const uint VertexAnimationWaterSurface = 3u;
const uint VertexMaterialFlagDisableAo = 8u;
bool is_water_animation(uint animation_id) {
  return animation_id == VertexAnimationWaterStill ||
         animation_id == VertexAnimationWaterFlow ||
         animation_id == VertexAnimationWaterSurface;
}
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
  uint animation_id = (a_light_face_material >> 23u) & 15u;
  if (is_water_animation(animation_id) && animation_id == VertexAnimationWaterSurface) {
    float wave_phase =
        dot((u_chunk_origin + local_pos).xz, vec2(1.73, 1.19)) +
        u_animation_time_seconds * 1.4;
    local_pos.y += sin(wave_phase) * 0.025;
  }
  uint atlas_u = a_uv & 65535u;
  uint atlas_v = (a_uv >> 16u) & 65535u;
  v_uv = (vec2(float(atlas_u), float(atlas_v)) + vec2(0.5)) * u_atlas_inv_size;
  v_atlas_page = a_atlas_page;
  v_material_class = (a_light_face_material >> 19u) & 15u;
  v_animation_id = animation_id;
  v_material_flags = (a_light_face_material >> 27u) & 31u;

  float block_r = float((a_light_face_material >> 0u) & 15u);
  float block_g = float((a_light_face_material >> 4u) & 15u);
  float block_b = float((a_light_face_material >> 8u) & 15u);
  float sky = float((a_light_face_material >> 12u) & 15u);
  float light = max(max(max(block_r, block_g), block_b), sky) / 15.0;
  bool disable_ao = (v_material_flags & VertexMaterialFlagDisableAo) != 0u;
  float ao_factor = disable_ao ? 1.0 : mix(0.58, 1.0, float(ao) / 3.0);
  v_light = clamp(light, 0.18, 1.0) * ao_factor;
  gl_Position = u_mvp * vec4(u_chunk_origin + local_pos, 1.0);
}
)";
  static constexpr const char* chunk_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
uniform sampler2DArray u_chunk_atlas;
uniform float u_alpha;
uniform float u_animation_time_seconds;
in vec2 v_uv;
in float v_light;
flat in uint v_atlas_page;
flat in uint v_material_class;
flat in uint v_animation_id;
flat in uint v_material_flags;
out vec4 out_color;
const uint VertexMaterialClassCutout = 1u;
const uint VertexMaterialClassWater = 3u;
const uint VertexMaterialClassFoliage = 5u;
const uint VertexAnimationWaterStill = 1u;
const uint VertexAnimationWaterFlow = 2u;
const uint VertexAnimationWaterSurface = 3u;
const uint VertexMaterialFlagTintFromBiome = 1u;
const uint VertexMaterialFlagTintFromFluid = 2u;
vec3 biome_tint_for_vertex(uint material_class) {
  return material_class == VertexMaterialClassFoliage ? vec3(0.42, 0.68, 0.28) : vec3(1.0);
}
vec3 fluid_tint_for_vertex(uint material_class) {
  return material_class == VertexMaterialClassWater ? vec3(0.42, 0.66, 0.88) : vec3(1.0);
}
bool is_water_animation(uint animation_id) {
  return animation_id == VertexAnimationWaterStill ||
         animation_id == VertexAnimationWaterFlow ||
         animation_id == VertexAnimationWaterSurface;
}
void main() {
  vec4 texel = texture(u_chunk_atlas, vec3(v_uv, float(v_atlas_page)));
  bool water = v_material_class == VertexMaterialClassWater;
  if (texel.a < (water ? 0.01 : 0.1)) {
    discard;
  }
  if ((v_material_flags & VertexMaterialFlagTintFromBiome) != 0u) {
    texel.rgb *= biome_tint_for_vertex(v_material_class);
  }
  if ((v_material_flags & VertexMaterialFlagTintFromFluid) != 0u) {
    texel.rgb *= fluid_tint_for_vertex(v_material_class);
  }
  float alpha = water ? max(texel.a * u_alpha, 0.42) : texel.a * u_alpha;
  float shimmer = water && is_water_animation(v_animation_id)
      ? sin(u_animation_time_seconds * 1.7 + v_uv.x * 96.0 + v_uv.y * 64.0) * 0.035
      : 0.0;
  float lit = water ? clamp(max(v_light, 0.72) + shimmer, 0.0, 1.0) : v_light;
  out_color = vec4(texel.rgb * lit, alpha);
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
uniform vec4 u_player_skin_region;
uniform int u_player_skin_page;
out vec2 v_uv;
out vec3 v_color;
flat out uint v_flags;
flat out uint v_texture_domain;
flat out uint v_atlas_page;
out vec4 v_player_skin_region;

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
  v_atlas_page = a_texture_domain == 2u ? uint(u_player_skin_page) : a_atlas_page;
  v_player_skin_region = u_player_skin_region;
  gl_Position = u_mvp * vec4(world, 1.0);
}
)";
  static constexpr const char* entity_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
uniform sampler2DArray u_entity_atlas;
uniform sampler2DArray u_player_skin_atlas;
uniform vec2 u_atlas_size;
uniform vec2 u_player_skin_atlas_size;
in vec2 v_uv;
in vec3 v_color;
flat in uint v_flags;
flat in uint v_texture_domain;
flat in uint v_atlas_page;
in vec4 v_player_skin_region;
out vec4 out_color;

vec4 sample_entity_texel() {
  if (v_texture_domain == 2u &&
      v_player_skin_region.z > 0.0 &&
      v_player_skin_region.w > 0.0) {
    vec2 skin_atlas_size = max(u_player_skin_atlas_size, vec2(1.0, 1.0));
    vec2 skin_uv =
        (v_player_skin_region.xy + v_uv * v_player_skin_region.zw) / skin_atlas_size;
    return texture(u_player_skin_atlas, vec3(skin_uv, float(v_atlas_page)));
  }
  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  return texture(u_entity_atlas, vec3(v_uv / atlas_size, float(v_atlas_page)));
}

void main() {
  vec4 texel = sample_entity_texel();
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
  static constexpr const char* ui_vertex_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_page;
uniform vec2 u_viewport;
out vec2 v_uv;
out vec4 v_color;
flat out int v_page;
void main() {
  vec2 viewport = max(u_viewport, vec2(1.0, 1.0));
  vec2 ndc = vec2(
      (a_position.x / viewport.x) * 2.0 - 1.0,
      1.0 - (a_position.y / viewport.y) * 2.0);
  v_uv = a_uv;
  v_color = a_color;
  v_page = int(a_page + 0.5);
  gl_Position = vec4(ndc, 0.0, 1.0);
}
)";
  static constexpr const char* ui_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
uniform sampler2DArray u_atlas;
uniform int u_mode;
in vec2 v_uv;
in vec4 v_color;
flat in int v_page;
out vec4 out_color;
void main() {
  vec4 color = v_color;
  if (u_mode == 1) {
    color *= texture(u_atlas, vec3(v_uv, float(v_page)));
  }
  if (color.a <= 0.01) {
    discard;
  }
  out_color = color;
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

  g_app.renderer.sky_program = link_program(
      "sky renderer",
      sky_vertex_shader_source,
      sky_fragment_shader_source);
  if (g_app.renderer.sky_program == 0U) {
    return false;
  }
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
  g_app.renderer.ui_program = link_program(
      "ui renderer",
      ui_vertex_shader_source,
      ui_fragment_shader_source);
  if (g_app.renderer.ui_program == 0U) {
    return false;
  }

  g_app.renderer.sky_camera_forward_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_camera_forward");
  g_app.renderer.sky_camera_right_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_camera_right");
  g_app.renderer.sky_camera_up_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_camera_up");
  g_app.renderer.sky_camera_aspect_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_camera_aspect");
  g_app.renderer.sky_camera_fov_y_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_camera_fov_y_degrees");
  g_app.renderer.sky_sun_direction_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_sun_direction");
  g_app.renderer.sky_moon_direction_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_moon_direction");
  g_app.renderer.sky_daylight_color_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_daylight_color");
  g_app.renderer.sky_ambient_color_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_ambient_color");
  g_app.renderer.sky_fog_color_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_fog_color");
  g_app.renderer.sky_daylight_strength_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_daylight_strength");
  g_app.renderer.sky_moonlight_strength_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_moonlight_strength");
  g_app.renderer.sky_star_visibility_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_star_visibility");
  g_app.renderer.sky_day_fraction_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_day_fraction");
  g_app.renderer.sky_cloud_time_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_cloud_time_seconds");
  g_app.renderer.sky_cirrus_amount_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_cirrus_amount");
  g_app.renderer.sky_cumulus_amount_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_cumulus_amount");
  g_app.renderer.sky_cloud_intensity_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_cloud_intensity");
  g_app.renderer.sky_exposure_location =
      glGetUniformLocation(g_app.renderer.sky_program, "u_exposure");
  g_app.renderer.chunk_mvp_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_mvp");
  g_app.renderer.chunk_origin_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_chunk_origin");
  g_app.renderer.chunk_atlas_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_chunk_atlas");
  g_app.renderer.chunk_atlas_inv_size_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_atlas_inv_size");
  g_app.renderer.chunk_alpha_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_alpha");
  g_app.renderer.chunk_animation_time_location =
      glGetUniformLocation(g_app.renderer.chunk_program, "u_animation_time_seconds");
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
  g_app.renderer.entity_player_skin_atlas_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_player_skin_atlas");
  g_app.renderer.entity_player_skin_atlas_size_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_player_skin_atlas_size");
  g_app.renderer.entity_player_skin_region_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_player_skin_region");
  g_app.renderer.entity_player_skin_page_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_player_skin_page");
  g_app.renderer.entity_bone_count_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_bone_count");
  g_app.renderer.entity_bone_pivots_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_bone_pivots[0]");
  g_app.renderer.entity_bone_rotations_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_bone_rotations[0]");
  g_app.renderer.entity_bone_positions_location =
      glGetUniformLocation(g_app.renderer.entity_program, "u_bone_positions[0]");
  g_app.renderer.ui_viewport_location =
      glGetUniformLocation(g_app.renderer.ui_program, "u_viewport");
  g_app.renderer.ui_atlas_location =
      glGetUniformLocation(g_app.renderer.ui_program, "u_atlas");
  g_app.renderer.ui_mode_location =
      glGetUniformLocation(g_app.renderer.ui_program, "u_mode");

  glGenVertexArrays(1, &g_app.renderer.ui_vao);
  glGenBuffers(1, &g_app.renderer.ui_vbo);
  glBindVertexArray(g_app.renderer.ui_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_app.renderer.ui_vbo);
  glEnableVertexAttribArray(0U);
  glVertexAttribPointer(
      0U,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebUiVertex),
      reinterpret_cast<const void*>(offsetof(WebUiVertex, position)));
  glEnableVertexAttribArray(1U);
  glVertexAttribPointer(
      1U,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebUiVertex),
      reinterpret_cast<const void*>(offsetof(WebUiVertex, uv)));
  glEnableVertexAttribArray(2U);
  glVertexAttribPointer(
      2U,
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebUiVertex),
      reinterpret_cast<const void*>(offsetof(WebUiVertex, color)));
  glEnableVertexAttribArray(3U);
  glVertexAttribPointer(
      3U,
      1,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebUiVertex),
      reinterpret_cast<const void*>(offsetof(WebUiVertex, page)));
  glBindBuffer(GL_ARRAY_BUFFER, 0U);
  glBindVertexArray(0U);
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

[[nodiscard]] bool ensure_player_skin_texture_array() {
  if (g_app.renderer.player_skin_texture != 0U) {
    return true;
  }

  GLint max_texture_size = 0;
  GLint max_array_layers = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_array_layers);

  constexpr arv::u16 PreferredPageSize = 2048U;
  constexpr arv::u16 PreferredMaxPages = 8U;
  const GLint texture_limit =
      max_texture_size > 0 ? max_texture_size : static_cast<GLint>(PreferredPageSize);
  const GLint layer_limit =
      max_array_layers > 0 ? max_array_layers : static_cast<GLint>(PreferredMaxPages);
  const arv::u16 page_size = static_cast<arv::u16>(
      std::max<GLint>(1, std::min<GLint>(PreferredPageSize, texture_limit)));
  const arv::u16 layers = static_cast<arv::u16>(
      std::max<GLint>(1, std::min<GLint>(PreferredMaxPages, layer_limit)));

  g_app.renderer.player_skin_allocator.configure({page_size, page_size, layers});
  glGenTextures(1, &g_app.renderer.player_skin_texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.player_skin_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
  glTexImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      GL_RGBA8,
      page_size,
      page_size,
      layers,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      nullptr);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  g_app.renderer.player_skin_texture_width = page_size;
  g_app.renderer.player_skin_texture_height = page_size;
  g_app.renderer.player_skin_texture_layers = layers;
  return g_app.renderer.player_skin_texture != 0U;
}

[[nodiscard]] bool upload_web_player_skin_texture(
    std::string skin_hash,
    const arv::asset::Image& image) {
  if (skin_hash.empty() ||
      g_app.renderer.player_skin_regions.find(skin_hash) !=
          g_app.renderer.player_skin_regions.end()) {
    return true;
  }
  if (image.empty() ||
      image.width > std::numeric_limits<arv::u16>::max() ||
      image.height > std::numeric_limits<arv::u16>::max() ||
      image.pixels.size() !=
          static_cast<arv::usize>(image.width) * static_cast<arv::usize>(image.height) * 4U ||
      !ensure_player_skin_texture_array()) {
    return false;
  }

  const auto handle = g_app.renderer.player_skin_allocator.allocate(
      static_cast<arv::u16>(image.width),
      static_cast<arv::u16>(image.height));
  if (!handle.has_value()) {
    set_status("web player skin atlas is full");
    return false;
  }
  const auto region = g_app.renderer.player_skin_allocator.resolve(handle.value());
  if (!region.has_value()) {
    return false;
  }

  glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.player_skin_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      region->x,
      region->y,
      region->page_index,
      static_cast<GLsizei>(image.width),
      static_cast<GLsizei>(image.height),
      1,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      image.pixels.data());
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  g_app.renderer.player_skin_regions[std::move(skin_hash)] = handle.value();
  return true;
}

void refresh_web_player_skin_render_assets() {
  if (g_app.session == nullptr || !g_app.entity_assets.ready) {
    return;
  }

  const arv::client::LocalPlayerIdentity& identity = g_app.session->local_identity();
  for (const arv::protocol::PlayerProfileInfo& profile :
       g_app.session->player_profile_infos()) {
    const std::string& skin_hash = profile.skin.skin_hash;
    if (skin_hash.empty() ||
        g_app.renderer.player_skin_regions.find(skin_hash) !=
            g_app.renderer.player_skin_regions.end()) {
      continue;
    }
    std::optional<arv::asset::Image> image =
        arv::client::load_valid_player_skin_render_image(identity, profile.skin);
    if (!image.has_value()) {
      continue;
    }
    (void)upload_web_player_skin_texture(skin_hash, image.value());
  }
}

[[nodiscard]] bool upload_item_texture_array(
    const arv::render::TextureArrayUploadData& upload) {
  destroy_item_texture();
  if (!upload.valid()) {
    set_status("item texture atlas upload was empty");
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
    set_status("item texture atlas exceeds WebGL texture-array limits");
    return false;
  }

  glGenTextures(1, &g_app.renderer.item_texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.item_texture);
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
  g_app.renderer.item_texture_width = upload.width;
  g_app.renderer.item_texture_height = upload.height;
  return true;
}

[[nodiscard]] bool upload_font_texture_array(
    const arv::render::TextureArrayUploadData& upload) {
  destroy_font_texture();
  if (!upload.valid()) {
    set_status("font atlas upload was empty");
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
    set_status("font atlas exceeds WebGL texture-array limits");
    return false;
  }

  glGenTextures(1, &g_app.renderer.font_texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.font_texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
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
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  g_app.renderer.font_texture_width = upload.width;
  g_app.renderer.font_texture_height = upload.height;
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

[[nodiscard]] bool load_web_ui_font_stack(
    arv::asset::ResourceManager& resources,
    arv::asset::FontSystem& font_system,
    const std::string& asset_name,
    arv::u32 pixel_size,
    arv::asset::FontStack& stack,
    bool include_fallbacks) {
  std::vector<arv::asset::FontFaceHandle> faces;
  const arv::asset::AssetID primary_id{asset_name};
  const std::filesystem::path primary_path =
      resources.resolve_asset_path(primary_id, arv::asset::AssetKind::Font);
  auto primary = font_system.load_face(primary_path, pixel_size, primary_id);
  if (!primary) {
    set_status("failed to load web UI font: " + primary.error().message);
    return false;
  }
  faces.emplace_back(primary.value());

  if (include_fallbacks) {
    for (const arv::asset::AssetID fallback : {
             arv::asset::AssetID{"arvane:font/symbol"},
             arv::asset::AssetID{"arvane:font/arabic"},
             arv::asset::AssetID{"arvane:font/emoji"},
         }) {
      const std::filesystem::path path =
          resources.resolve_asset_path(fallback, arv::asset::AssetKind::Font);
      if (!std::filesystem::exists(path)) {
        continue;
      }
      auto face = font_system.load_face(path, pixel_size, fallback);
      if (face) {
        faces.emplace_back(face.value());
      }
    }
  }

  stack = font_system.make_stack(faces);
  return true;
}

void load_web_language(const arv::asset::PackManager& packs) {
  const auto files = packs.data_files_at("lang/en_US.yaml");
  std::vector<std::filesystem::path> paths;
  paths.reserve(files.size());
  for (const arv::asset::PackDataFile& file : files) {
    paths.push_back(file.path);
  }
  auto language = arv::text::load_language_files(paths, "en_US");
  if (!language) {
    return;
  }
  g_app.ui.language = std::move(language.value());
  if (g_app.session != nullptr) {
    g_app.session->configure_language(&g_app.ui.language);
  }
}

[[nodiscard]] bool ensure_block_render_assets_loaded() {
  const std::string* registry_snapshot = g_app.session == nullptr
      ? nullptr
      : &g_app.session->multiplayer_registry_snapshot_json();
  if (g_app.session == nullptr ||
      registry_snapshot->empty()) {
    return false;
  }
  const std::span<const std::filesystem::path> cache_paths =
      g_app.session->multiplayer_pack_cache_paths();
  if (cache_paths.empty()) {
    return false;
  }
  if (g_app.block_assets.ready &&
      g_app.block_assets.registry_snapshot_json == *registry_snapshot &&
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
  g_app.block_assets.registry_snapshot_json = *registry_snapshot;
  g_app.block_assets.pack_cache_paths = std::move(paths);
  g_app.block_assets.ready = true;
  set_status("web block textures and models ready");
  return true;
}

[[nodiscard]] bool ensure_entity_render_assets_loaded() {
  if (!ensure_block_render_assets_loaded()) {
    return false;
  }
  const std::string* registry_snapshot = g_app.session == nullptr
      ? nullptr
      : &g_app.session->multiplayer_registry_snapshot_json();
  if (g_app.session == nullptr ||
      registry_snapshot->empty()) {
    return false;
  }
  const std::span<const std::filesystem::path> cache_paths =
      g_app.session->multiplayer_pack_cache_paths();
  if (cache_paths.empty()) {
    return false;
  }
  if (g_app.entity_assets.ready &&
      g_app.entity_assets.registry_snapshot_json == *registry_snapshot &&
      same_pack_paths(cache_paths, g_app.entity_assets.pack_cache_paths) &&
      g_app.renderer.entity_texture != 0U &&
      g_app.renderer.item_texture != 0U &&
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

  auto item_animation_text =
      packs.load_merged_json_array_text("item_animations", "animations");
  if (!item_animation_text) {
    set_status(
        "failed to load web item animations: " +
        item_animation_text.error().describe());
    return false;
  }
  auto item_animation_library = arv::client::parse_item_animation_library_json(
      item_animation_text.value());
  if (!item_animation_library) {
    set_status(
        "failed to parse web item animations: " +
        item_animation_library.error().message);
    return false;
  }
  auto item_animation_references = arv::client::validate_item_animation_references(
      loaded_assets.value().item_registry,
      item_animation_library.value());
  if (!item_animation_references) {
    set_status(
        "failed to validate web item animations: " +
        item_animation_references.error().message);
    return false;
  }

  const std::array<arv::asset::AssetID, 1> ui_texture_assets{{
      arv::asset::AssetID{std::string{arv::client::GameplayCrosshairTextureAsset}},
  }};
  auto item_atlas = arv::client::build_item_texture_atlas(
      resources,
      loaded_assets.value().item_registry,
      {.max_page_size = 1024,
       .max_pages = 2,
       .padding = 2,
       .require_uniform_non_fallback_size = false},
      ui_texture_assets);
  if (!item_atlas) {
    set_status(
        "failed to build web item atlas: " +
        item_atlas.error().operation +
        ": " +
        item_atlas.error().message);
    return false;
  }
  const arv::render::TextureArrayUploadData item_atlas_upload =
      arv::client::texture_array_upload_from_atlas(item_atlas.value());
  if (!upload_item_texture_array(item_atlas_upload)) {
    return false;
  }
  if (const arv::asset::TextureAtlasEntry* crosshair =
          item_atlas.value().find(arv::client::GameplayCrosshairTextureAsset);
      crosshair != nullptr && !item_atlas.value().pages.empty()) {
    g_app.entity_assets.crosshair_image =
        arv::client::image_data_from_atlas_entry(
            *crosshair,
            item_atlas.value().pages.front().image.width,
            item_atlas.value().pages.front().image.height,
            arv::render::UiTextureSource::ItemAtlas);
  }

  const arv::render::TextureArrayUploadData atlas_upload =
      arv::client::texture_array_upload_from_atlas(
          loaded_assets.value().render_assets.texture_atlas);
  if (!upload_entity_texture_array(atlas_upload)) {
    return false;
  }
  upload_entity_models(loaded_assets.value().render_assets);

  g_app.entity_assets.loaded = std::move(loaded_assets.value());
  if (g_app.session != nullptr) {
    g_app.session->configure_vehicles(&g_app.entity_assets.loaded.vehicle_registry);
  }
  g_app.entity_assets.item_animation_library = std::move(item_animation_library.value());
  g_app.entity_assets.item_atlas = std::move(item_atlas.value());
  g_app.entity_assets.registry_snapshot_json = *registry_snapshot;
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
        return mesh.coord == section.coord;
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

[[nodiscard]] const arv::player::ItemRecord* selected_web_held_item() noexcept {
  if (g_app.session == nullptr ||
      !g_app.session->has_authoritative_inventory() ||
      !g_app.entity_assets.ready) {
    return nullptr;
  }
  return arv::client::selected_inventory_item(
      g_app.session->inventory(),
      g_app.session->selected_hotbar_slot(),
      g_app.entity_assets.loaded.item_registry);
}

[[nodiscard]] std::string action_binding_label(
    arv::platform::LogicalAction action) {
  const arv::platform::ActionBindingList& bindings =
      arv::platform::bindings_for_action(g_app.input_bindings, action);
  return bindings.empty()
             ? std::string{}
             : arv::platform::input_binding_label(bindings.front());
}

[[nodiscard]] bool web_entity_interactions_available() noexcept {
  return g_app.session != nullptr &&
         g_app.block_assets.ready &&
         g_app.entity_assets.ready;
}

[[nodiscard]] arv::f32 web_default_attack_range() noexcept {
  return arv::io::GameplayConfig{}.combat.default_attack_range;
}

[[nodiscard]] arv::f32 web_ride_target_range() noexcept {
  return arv::player::DefaultReachDistance + 1.5F;
}

[[nodiscard]] std::optional<arv::EntityNetID> targeted_web_rideable_entity() {
  if (!web_entity_interactions_available()) {
    return std::nullopt;
  }
  const arv::client::ChunkSectionCollisionQuery query(
      g_app.chunk_sections,
      g_app.block_assets.loaded.block_registry);
  return arv::client::targeted_rideable_entity(
      {*g_app.session,
       query,
       g_app.block_assets.loaded.block_registry,
       g_app.entity_assets.loaded.entity_registry,
       g_app.entity_assets.loaded.vehicle_registry},
      web_ride_target_range());
}

void send_web_entity_interactions_from_input(
    const arv::client::InputFrame& input,
    arv::Seconds now_seconds) {
  if (!web_entity_interactions_available() ||
      g_app.ui.screen_manager.blocks_gameplay_input() ||
      g_app.session->inventory_open() ||
      g_app.session->chat_open()) {
    return;
  }

  const arv::client::ChunkSectionCollisionQuery query(
      g_app.chunk_sections,
      g_app.block_assets.loaded.block_registry);
  if (input.primary_pressed) {
    (void)arv::client::request_attack_entity_if_targeted(
        *g_app.session,
        query,
        g_app.block_assets.loaded.block_registry,
        g_app.entity_assets.loaded.entity_registry,
        g_app.entity_assets.loaded.vehicle_registry,
        selected_web_held_item(),
        web_default_attack_range());
  }
  if (input.mount_pressed &&
      !arv::client::local_player_is_mounted(*g_app.session) &&
      arv::client::request_ride_entity_if_targeted(
          *g_app.session,
          query,
          g_app.block_assets.loaded.block_registry,
          g_app.entity_assets.loaded.entity_registry,
          g_app.entity_assets.loaded.vehicle_registry,
          web_ride_target_range())) {
    g_app.input.held_item_secondary_use_active = false;
    g_app.input.held_item_animation.stop(now_seconds);
  }
}

void update_held_item_animation_from_input(
    const arv::client::InputFrame& input,
    arv::Seconds now_seconds) {
  if (g_app.session == nullptr ||
      !g_app.entity_assets.ready ||
      g_app.entity_assets.item_animation_library.clip_count() == 0U ||
      !g_app.session->has_authoritative_inventory()) {
    g_app.input.held_item_secondary_use_active = false;
    g_app.input.held_item_animation.stop(now_seconds);
    return;
  }

  const arv::player::ItemRecord* held_item = selected_web_held_item();
  if (held_item == nullptr) {
    g_app.input.held_item_secondary_use_active = false;
    g_app.input.held_item_animation.stop(now_seconds);
    return;
  }

  constexpr arv::client::HeldItemAnimationConditionState OptimisticServerAction{
      .placement_succeeds = true};
  if (input.primary_pressed) {
    (void)arv::client::play_held_item_action(
        g_app.input.held_item_animation,
        held_item,
        arv::client::HeldItemActionPrimaryUse,
        g_app.entity_assets.item_animation_library,
        OptimisticServerAction,
        now_seconds);
  }

  if (input.primary_held &&
      g_app.session->block_break_progress().active &&
      g_app.input.held_item_animation.finished(now_seconds)) {
    (void)arv::client::play_held_item_action(
        g_app.input.held_item_animation,
        held_item,
        arv::client::HeldItemActionPrimaryUse,
        g_app.entity_assets.item_animation_library,
        OptimisticServerAction,
        now_seconds);
  }

  if (input.secondary_pressed) {
    arv::client::HeldItemActionPlayResult result =
        arv::client::play_held_item_action(
            g_app.input.held_item_animation,
            held_item,
            arv::client::HeldItemActionSecondaryUseStart,
            g_app.entity_assets.item_animation_library,
            OptimisticServerAction,
            now_seconds);
    if (result == arv::client::HeldItemActionPlayResult::NoAnimation) {
      result = arv::client::play_held_item_action(
          g_app.input.held_item_animation,
          held_item,
          arv::client::HeldItemActionSecondaryUseHold,
          g_app.entity_assets.item_animation_library,
          OptimisticServerAction,
          now_seconds);
    }
    g_app.input.held_item_secondary_use_active =
        result != arv::client::HeldItemActionPlayResult::Suppressed;
  }

  if (input.secondary_held &&
      g_app.input.held_item_secondary_use_active &&
      g_app.input.held_item_animation.finished(now_seconds)) {
    const arv::client::HeldItemAnimationResolution hold_animation =
        arv::client::resolve_held_item_animation_binding(
            held_item,
            arv::client::HeldItemActionSecondaryUseHold,
            g_app.entity_assets.item_animation_library);
    if (hold_animation.clip != nullptr &&
        arv::client::held_item_animation_condition_passes(
            hold_animation.play_condition,
            OptimisticServerAction) &&
        g_app.input.held_item_animation.active_clip() != hold_animation.clip) {
      g_app.input.held_item_animation.play(*hold_animation.clip, now_seconds);
    }
  }

  if (!input.secondary_held && g_app.input.held_item_secondary_use_active) {
    g_app.input.held_item_secondary_use_active = false;
    if (arv::client::play_held_item_action(
            g_app.input.held_item_animation,
            held_item,
            arv::client::HeldItemActionSecondaryUseEnd,
            g_app.entity_assets.item_animation_library,
            OptimisticServerAction,
            now_seconds) != arv::client::HeldItemActionPlayResult::Played) {
      g_app.input.held_item_animation.stop(now_seconds);
    }
  }
}

void start_block_container_animation(
    arv::protocol::ContainerBlockSource source,
    std::string_view clip,
    arv::Seconds now_seconds) {
  g_app.active_container_animation_source = source;
  g_app.active_container_animation_clip = std::string{clip};
  g_app.active_container_animation_started_at = now_seconds;
}

void update_block_container_animation(arv::Seconds now_seconds) {
  const std::optional<arv::protocol::ContainerBlockSource> current_source =
      g_app.session == nullptr
          ? std::nullopt
          : g_app.session->active_container_block_source();
  if (current_source != g_app.last_container_animation_source) {
    if (current_source.has_value()) {
      start_block_container_animation(
          current_source.value(),
          BlockContainerOpenAnimation,
          now_seconds);
    } else if (g_app.last_container_animation_source.has_value()) {
      start_block_container_animation(
          g_app.last_container_animation_source.value(),
          BlockContainerCloseAnimation,
          now_seconds);
    }
    g_app.last_container_animation_source = current_source;
  }

  if (g_app.active_container_animation_source.has_value() &&
      g_app.active_container_animation_clip == BlockContainerCloseAnimation &&
      now_seconds - g_app.active_container_animation_started_at >
          BlockContainerAnimationLengthSeconds) {
    g_app.active_container_animation_source.reset();
    g_app.active_container_animation_clip.clear();
  }
}

void append_block_container_animation_playback(
    arv::render::EntityDrawItem& item,
    arv::protocol::ContainerBlockSource source,
    arv::Seconds now_seconds) {
  if (!g_app.active_container_animation_source.has_value() ||
      g_app.active_container_animation_source.value() != source ||
      g_app.active_container_animation_clip.empty()) {
    return;
  }
  item.animations.push_back({
      g_app.active_container_animation_clip,
      static_cast<arv::f32>(
          std::max<arv::Seconds>(
              0.0,
              now_seconds - g_app.active_container_animation_started_at))});
}

[[nodiscard]] std::vector<arv::render::EntityDrawItem> build_entity_draw_items(
    arv::Seconds animation_time_seconds) {
  std::vector<arv::render::EntityDrawItem> draws;
  if (g_app.session == nullptr || !g_app.entity_assets.ready) {
    return draws;
  }
  update_block_container_animation(animation_time_seconds);
  const arv::WorldID world_id = g_app.session->camera().world_id();
  const std::optional<arv::EntityNetID> local_entity_id =
      g_app.session->local_entity_id();
  const std::optional<arv::PlayerID> local_player_id =
      g_app.session->local_player_id();
  const std::vector<arv::client::ReplicatedEntityMirror> mirrors =
      g_app.session->replicated_entity_mirrors();
  draws.reserve(mirrors.size() + g_app.session->dropped_item_count() + 2U);
  for (const arv::client::ReplicatedEntityMirror& mirror : mirrors) {
    if (mirror.world_id != world_id) {
      continue;
    }
    const bool is_local_player =
        (local_entity_id.has_value() && mirror.entity_id == local_entity_id.value()) ||
        (local_player_id.has_value() && mirror.owning_player.has_value() &&
         mirror.owning_player.value() == local_player_id.value());
    if (is_local_player) {
      continue;
    }
    const auto model =
        g_app.entity_assets.loaded.render_assets.model_by_type.find(mirror.entity_type);
    if (model == g_app.entity_assets.loaded.render_assets.model_by_type.end()) {
      continue;
    }

    arv::render::EntityModelID model_id = model->second;
    arv::render::TextureRegionHandle player_skin_region;
    const arv::protocol::PlayerProfileInfo* profile =
        mirror.owning_player.has_value()
            ? g_app.session->profile_info_for_player(mirror.owning_player.value())
            : nullptr;
    const bool slim_player = profile != nullptr && profile->skin.slim_model;
    if (slim_player &&
        g_app.entity_assets.loaded.render_assets.player_slim_model.value != 0U) {
      model_id = g_app.entity_assets.loaded.render_assets.player_slim_model;
    }
    if (profile != nullptr && !profile->skin.skin_hash.empty()) {
      const auto region = g_app.renderer.player_skin_regions.find(profile->skin.skin_hash);
      if (region != g_app.renderer.player_skin_regions.end() &&
          region->second.valid() &&
          g_app.renderer.player_skin_allocator.resolve(region->second).has_value()) {
        player_skin_region = region->second;
        if (slim_player &&
            g_app.entity_assets.loaded.render_assets.player_slim_skin_model.value != 0U) {
          model_id = g_app.entity_assets.loaded.render_assets.player_slim_skin_model;
        } else if (g_app.entity_assets.loaded.render_assets.player_skin_model.value != 0U) {
          model_id = g_app.entity_assets.loaded.render_assets.player_skin_model;
        }
      }
    }

    arv::render::EntityDrawItem draw;
    draw.model_id = model_id;
    if (const auto animation =
            g_app.entity_assets.loaded.render_assets.animation_by_type.find(mirror.entity_type);
        animation != g_app.entity_assets.loaded.render_assets.animation_by_type.end()) {
      draw.animation_id = animation->second;
    }
    draw.position = mirror.render_position;
    draw.rotation = mirror.render_rotation;
    draw.scale = 1.0F;
    draw.player_skin_region = player_skin_region;
    draw.animations.reserve(mirror.animations.size());
    for (const arv::protocol::EntityAnimationPlayback& animation : mirror.animations) {
      draw.animations.push_back({animation.name, animation.time_seconds});
    }
    if (mirror.owning_player.has_value()) {
      draw.bone_rotation_overrides.push_back({"head", mirror.head_rotation_degrees});
    }
    draws.push_back(std::move(draw));
  }
  if (!local_entity_id.has_value()) {
    return draws;
  }

  arv::render::FramePacket item_frame;
  item_frame.camera = g_app.session->camera().snapshot(
      static_cast<arv::u32>(std::max(g_app.width, 1)),
      static_cast<arv::u32>(std::max(g_app.height, 1)),
      0.0F);
  item_frame.world_view = arv::render::render_view_from_camera(item_frame.camera);
  item_frame.world_view.world_id = world_id;
  const std::vector<arv::client::DroppedItemMirror> dropped_items =
      g_app.session->dropped_item_mirrors();
  arv::client::append_dropped_item_draws(
      item_frame,
      g_app.entity_assets.loaded.render_assets,
      dropped_items,
      world_id,
      animation_time_seconds);

  const auto& block_attached_models =
      g_app.entity_assets.loaded.render_assets.block_attached_model_by_variant;
  const auto& block_attached_animations =
      g_app.entity_assets.loaded.render_assets.block_attached_animation_by_variant;
  for (const arv::protocol::ClientboundChunkSectionData& section : g_app.chunk_sections) {
    if (section.world_id != world_id) {
      continue;
    }
    if (!arv::render::chunk_aabb_intersects_view(item_frame.world_view, section.coord)) {
      continue;
    }
    for (arv::usize index = 0; index < arv::voxel::ChunkVolume; ++index) {
      const arv::voxel::BlockVariantID variant_id = section.blocks[index];
      if (variant_id.value >= block_attached_models.size()) {
        continue;
      }
      const arv::render::EntityModelID model_id =
          block_attached_models[variant_id.value];
      if (model_id.value == 0U) {
        continue;
      }

      const arv::BlockPos block =
          arv::chunk_local_to_block(section.coord, arv::voxel::local_pos(index));
      arv::render::EntityDrawItem item;
      item.model_id = model_id;
      if (variant_id.value < block_attached_animations.size()) {
        item.animation_id = block_attached_animations[variant_id.value];
      }
      item.position = arv::block_floor_center(block);
      if (variant_id.value <
          g_app.entity_assets.loaded.render_assets
              .block_attached_rotation_by_variant.size()) {
        item.rotation =
            g_app.entity_assets.loaded.render_assets
                .block_attached_rotation_by_variant[variant_id.value];
      }
      item.scale = 1.0F;
      if (item.animation_id.value != 0U) {
        append_block_container_animation_playback(
            item,
            arv::protocol::ContainerBlockSource{world_id, block},
            animation_time_seconds);
      }
      item_frame.entities.push_back(std::move(item));
    }
  }

  const arv::client::CameraShell& camera = g_app.session->camera();
  const arv::client::ChunkSectionCollisionQuery outline_query(
      g_app.chunk_sections,
      g_app.block_assets.loaded.block_registry);
  const std::optional<arv::client::BlockOutlineTarget> outline_target =
      arv::client::find_block_outline_target(
          outline_query,
          g_app.block_assets.loaded.block_registry,
          camera.position(),
          camera.forward_vector(),
          arv::player::DefaultReachDistance);
  if (outline_target.has_value() &&
      outline_target->variant.value <
          g_app.entity_assets.loaded.render_assets.outline_model_by_variant.size()) {
    const arv::render::EntityModelID model_id =
        g_app.entity_assets.loaded.render_assets
            .outline_model_by_variant[outline_target->variant.value];
    if (model_id.value != 0U) {
      arv::render::EntityDrawItem item;
      item.model_id = model_id;
      item.position = {
          static_cast<arv::f32>(outline_target->block.value.x) + 0.5F,
          static_cast<arv::f32>(outline_target->block.value.y) + 0.5F,
          static_cast<arv::f32>(outline_target->block.value.z) + 0.5F};
      item.scale = 1.0F;
      item_frame.entities.push_back(std::move(item));
    }
  }

  const arv::protocol::BlockBreakProgress& block_break =
      g_app.session->block_break_progress();
  if (block_break.active &&
      block_break.variant.value <
          g_app.entity_assets.loaded.render_assets.break_overlay_model_by_variant.size() &&
      block_break.stage < arv::client::BreakStageTextureCount) {
    const arv::render::EntityModelID model_id =
        g_app.entity_assets.loaded.render_assets
            .break_overlay_model_by_variant[block_break.variant.value][block_break.stage];
    if (model_id.value != 0U) {
      arv::render::EntityDrawItem item;
      item.model_id = model_id;
      item.position = {
          static_cast<arv::f32>(block_break.block.value.x) + 0.5F,
          static_cast<arv::f32>(block_break.block.value.y) + 0.5F,
          static_cast<arv::f32>(block_break.block.value.z) + 0.5F};
      item.scale = 1.0F;
      item_frame.entities.push_back(std::move(item));
    }
  }

  if (g_app.session->has_authoritative_inventory() &&
      !g_app.session->inventory_open() &&
      !g_app.session->chat_open() &&
      !g_app.session->container_open()) {
    const arv::usize selected_slot = g_app.session->selected_hotbar_slot();
    const arv::player::Inventory& inventory = g_app.session->inventory();
    if (selected_slot < inventory.slot_count()) {
      const std::optional<arv::player::ItemStack>& held_stack =
          inventory.slot(selected_slot);
      if (held_stack.has_value() && held_stack->count > 0U) {
        arv::client::append_held_item_draw(
            item_frame,
            g_app.entity_assets.loaded.render_assets,
            held_stack.value(),
            item_frame.camera,
            g_app.input.held_item_animation.sample(animation_time_seconds));
      }
    }
  }
  draws.insert(
      draws.end(),
      item_frame.entities.begin(),
      item_frame.entities.end());
  return draws;
}

void draw_entity_models(
    const arv::Mat4f& mvp,
    bool translucent,
    arv::Seconds animation_time_seconds) {
  if (g_app.renderer.entity_program == 0U ||
      g_app.renderer.entity_texture == 0U ||
      !g_app.entity_assets.ready ||
      g_app.renderer.entity_models.empty()) {
    return;
  }

  refresh_web_player_skin_render_assets();
  std::vector<arv::render::EntityDrawItem> draws =
      build_entity_draw_items(animation_time_seconds);
  const arv::Vec3f eye = g_app.session == nullptr
      ? arv::Vec3f{}
      : g_app.session->camera().position();
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
  glUniform1i(g_app.renderer.entity_player_skin_atlas_location, 1);
  glUniform2f(
      g_app.renderer.entity_player_skin_atlas_size_location,
      static_cast<float>(std::max(g_app.renderer.player_skin_texture_width, 1U)),
      static_cast<float>(std::max(g_app.renderer.player_skin_texture_height, 1U)));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.entity_texture);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(
      GL_TEXTURE_2D_ARRAY,
      g_app.renderer.player_skin_texture != 0U
          ? g_app.renderer.player_skin_texture
          : g_app.renderer.entity_texture);

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
    arv::Vec4f player_skin_region{};
    arv::u32 player_skin_page = 0U;
    if (draw.player_skin_region.valid()) {
      const auto region =
          g_app.renderer.player_skin_allocator.resolve(draw.player_skin_region);
      if (region.has_value()) {
        player_skin_region = {
            static_cast<arv::f32>(region->x),
            static_cast<arv::f32>(region->y),
            static_cast<arv::f32>(region->width),
            static_cast<arv::f32>(region->height)};
        player_skin_page = region->page_index;
      }
    }
    glUniform4f(
        g_app.renderer.entity_player_skin_region_location,
        player_skin_region.x,
        player_skin_region.y,
        player_skin_region.z,
        player_skin_region.w);
    glUniform1i(
        g_app.renderer.entity_player_skin_page_location,
        static_cast<GLint>(player_skin_page));
    glBindVertexArray(gpu->second.vao);
    glDrawElements(GL_TRIANGLES, gpu->second.index_count, GL_UNSIGNED_INT, nullptr);
  }

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  glActiveTexture(GL_TEXTURE0);
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

  const std::vector<arv::client::ReplicatedEntityMirror> mirrors =
      g_app.session->replicated_entity_mirrors();
  const std::vector<arv::web::WebEntityMarker> markers =
      arv::web::build_visible_fallback_entity_markers(
          mirrors,
          g_app.session->camera().world_id(),
          g_app.session->local_entity_id(),
          g_app.session->local_player_id());
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
      g_app.chunk_sections;
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
              return section.coord == mesh.coord;
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
          return mesh.coord == section.coord;
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

void append_ui_quad_vertices(
    std::vector<WebUiVertex>& vertices,
    const std::array<arv::Vec2f, 4>& positions,
    const std::array<arv::Vec2f, 4>& uvs,
    arv::Vec4f color,
    arv::u32 page) {
  const auto push = [&](arv::usize index) {
    vertices.push_back({positions[index], uvs[index], color, static_cast<float>(page)});
  };
  push(0U);
  push(1U);
  push(2U);
  push(0U);
  push(2U);
  push(3U);
}

void append_ui_glyph_vertices(
    std::vector<WebUiVertex>& vertices,
    const arv::render::UIGlyphDrawItem& glyph) {
  arv::Vec2f min = glyph.min;
  arv::Vec2f max = glyph.max;
  arv::Vec2f uv_min = glyph.uv_min;
  arv::Vec2f uv_max = glyph.uv_max;
  if (glyph.clip) {
    const arv::Vec2f original_min = min;
    const arv::Vec2f original_max = max;
    min.x = std::max(min.x, glyph.clip_min.x);
    min.y = std::max(min.y, glyph.clip_min.y);
    max.x = std::min(max.x, glyph.clip_max.x);
    max.y = std::min(max.y, glyph.clip_max.y);
    if (max.x <= min.x || max.y <= min.y ||
        original_max.x <= original_min.x || original_max.y <= original_min.y) {
      return;
    }
    const float left_t = (min.x - original_min.x) / (original_max.x - original_min.x);
    const float right_t = (max.x - original_min.x) / (original_max.x - original_min.x);
    const float top_t = (min.y - original_min.y) / (original_max.y - original_min.y);
    const float bottom_t = (max.y - original_min.y) / (original_max.y - original_min.y);
    const arv::Vec2f original_uv_min = uv_min;
    const arv::Vec2f original_uv_max = uv_max;
    uv_min.x = original_uv_min.x + (original_uv_max.x - original_uv_min.x) * left_t;
    uv_max.x = original_uv_min.x + (original_uv_max.x - original_uv_min.x) * right_t;
    uv_min.y = original_uv_min.y + (original_uv_max.y - original_uv_min.y) * top_t;
    uv_max.y = original_uv_min.y + (original_uv_max.y - original_uv_min.y) * bottom_t;
  }

  const std::array<arv::Vec2f, 4> positions{{
      min,
      {max.x, min.y},
      max,
      {min.x, max.y},
  }};
  const std::array<arv::Vec2f, 4> uvs{{
      uv_min,
      {uv_max.x, uv_min.y},
      uv_max,
      {uv_min.x, uv_max.y},
  }};
  append_ui_quad_vertices(vertices, positions, uvs, glyph.color, glyph.page);
}

void append_ui_axis_quad_vertices(
    std::vector<WebUiVertex>& vertices,
    arv::Vec2f min,
    arv::Vec2f max,
    arv::Vec4f color) {
  if (max.x <= min.x || max.y <= min.y || color.w <= 0.0F) {
    return;
  }
  const std::array<arv::Vec2f, 4> positions{{
      min,
      {max.x, min.y},
      max,
      {min.x, max.y},
  }};
  constexpr std::array<arv::Vec2f, 4> uvs{{
      {0.0F, 0.0F},
      {1.0F, 0.0F},
      {1.0F, 1.0F},
      {0.0F, 1.0F},
  }};
  append_ui_quad_vertices(vertices, positions, uvs, color, 0U);
}

void append_ui_rect_vertices(
    std::vector<WebUiVertex>& vertices,
    const arv::render::UIRectDrawItem& rect) {
  arv::Vec2f min = rect.min;
  arv::Vec2f max = rect.max;
  if (rect.clip) {
    min.x = std::max(min.x, rect.clip_min.x);
    min.y = std::max(min.y, rect.clip_min.y);
    max.x = std::min(max.x, rect.clip_max.x);
    max.y = std::min(max.y, rect.clip_max.y);
  }
  append_ui_axis_quad_vertices(vertices, min, max, rect.color);

  const float border = std::max(rect.border_width, 0.0F);
  if (border <= 0.0F || rect.border_color.w <= 0.0F ||
      max.x <= min.x || max.y <= min.y) {
    return;
  }
  append_ui_axis_quad_vertices(vertices, min, {max.x, min.y + border}, rect.border_color);
  append_ui_axis_quad_vertices(vertices, {min.x, max.y - border}, max, rect.border_color);
  append_ui_axis_quad_vertices(vertices, min, {min.x + border, max.y}, rect.border_color);
  append_ui_axis_quad_vertices(vertices, {max.x - border, min.y}, max, rect.border_color);
}

void draw_ui_vertices(
    const std::vector<WebUiVertex>& vertices,
    GLint mode,
    GLuint texture) {
  if (vertices.empty() || g_app.renderer.ui_program == 0U ||
      g_app.renderer.ui_vao == 0U || g_app.renderer.ui_vbo == 0U) {
    return;
  }
  if (mode == 1 && texture == 0U) {
    return;
  }

  glUseProgram(g_app.renderer.ui_program);
  glUniform2f(
      g_app.renderer.ui_viewport_location,
      static_cast<float>(std::max(g_app.width, 1)),
      static_cast<float>(std::max(g_app.height, 1)));
  glUniform1i(g_app.renderer.ui_mode_location, mode);
  glUniform1i(g_app.renderer.ui_atlas_location, 0);
  if (mode == 1) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
  }
  glBindVertexArray(g_app.renderer.ui_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_app.renderer.ui_vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(vertices.size() * sizeof(WebUiVertex)),
      vertices.data(),
      GL_DYNAMIC_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindBuffer(GL_ARRAY_BUFFER, 0U);
  glBindVertexArray(0U);
  if (mode == 1) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  }
}

void draw_ui_frame(const arv::render::FramePacket& frame) {
  std::vector<WebUiVertex> color_vertices;
  std::vector<WebUiVertex> font_vertices;
  std::vector<WebUiVertex> terrain_vertices;
  std::vector<WebUiVertex> item_vertices;
  color_vertices.reserve(frame.ui_rects.size() * 30U);
  font_vertices.reserve(frame.ui_glyphs.size() * 6U);
  terrain_vertices.reserve(frame.ui_textured_quads.size() * 6U);
  item_vertices.reserve(frame.ui_textured_quads.size() * 6U);

  for (const arv::render::UIRectDrawItem& rect : frame.ui_rects) {
    append_ui_rect_vertices(color_vertices, rect);
  }
  for (const arv::render::UITexturedQuadDrawItem& quad : frame.ui_textured_quads) {
    switch (quad.source) {
      case arv::render::UiTextureSource::TerrainAtlas:
        append_ui_quad_vertices(
            terrain_vertices,
            quad.positions,
            quad.uvs,
            quad.color,
            quad.page);
        break;
      case arv::render::UiTextureSource::ItemAtlas:
        append_ui_quad_vertices(
            item_vertices,
            quad.positions,
            quad.uvs,
            quad.color,
            quad.page);
        break;
      case arv::render::UiTextureSource::White:
        append_ui_quad_vertices(
            color_vertices,
            quad.positions,
            quad.uvs,
            quad.color,
            0U);
        break;
      case arv::render::UiTextureSource::FontAtlas:
        append_ui_quad_vertices(
            font_vertices,
            quad.positions,
            quad.uvs,
            quad.color,
            quad.page);
        break;
      case arv::render::UiTextureSource::PlayerSkinAtlas:
        break;
    }
  }
  for (const arv::render::UIGlyphDrawItem& glyph : frame.ui_glyphs) {
    append_ui_glyph_vertices(font_vertices, glyph);
  }

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  draw_ui_vertices(color_vertices, 0, 0U);
  draw_ui_vertices(terrain_vertices, 1, g_app.renderer.chunk_texture);
  draw_ui_vertices(item_vertices, 1, g_app.renderer.item_texture);
  draw_ui_vertices(font_vertices, 1, g_app.renderer.font_texture);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

[[nodiscard]] bool ensure_web_ui_ready() {
  const arv::ui::UiDisplayMetrics metrics = web_ui_metrics();
  const float framebuffer_scale = metrics.scalar_to_framebuffer(1.0F);
  if (g_app.ui.ready &&
      std::abs(g_app.ui.font_framebuffer_scale - framebuffer_scale) > 0.01F) {
    clear_web_ui_assets();
  }
  if (g_app.ui.ready) {
    return true;
  }
  if (g_app.session == nullptr) {
    return false;
  }
  const std::span<const std::filesystem::path> cache_paths =
      g_app.session->multiplayer_pack_cache_paths();
  if (cache_paths.empty()) {
    return false;
  }
  auto packs_result = arv::asset::PackManager::load_client_bundle_roots(
      cache_paths);
  if (!packs_result) {
    set_status("failed to mount web UI assets: " + packs_result.error().describe());
    return false;
  }
  arv::asset::PackManager packs = std::move(packs_result.value());
  arv::asset::ResourceManager resources(packs);
  load_web_language(packs);
  const arv::ui::UiThemeTypography& type = g_app.ui.theme.typography;
  if (!load_web_ui_font_stack(
          resources,
          g_app.ui.fonts,
          type.default_font,
          metrics.font_pixel_size(type.size_sm),
          g_app.ui.font_stacks.small,
          false) ||
      !load_web_ui_font_stack(
          resources,
          g_app.ui.fonts,
          type.default_font,
          metrics.font_pixel_size(type.size_md),
          g_app.ui.font_stacks.body,
          true) ||
      !load_web_ui_font_stack(
          resources,
          g_app.ui.fonts,
          type.heading_font,
          metrics.font_pixel_size(type.size_lg),
          g_app.ui.font_stacks.heading,
          false) ||
      !load_web_ui_font_stack(
          resources,
          g_app.ui.fonts,
          type.heading_font,
          metrics.font_pixel_size(type.size_xl),
          g_app.ui.font_stacks.title,
          false) ||
      !load_web_ui_font_stack(
          resources,
          g_app.ui.fonts,
          type.monospace_font,
          metrics.font_pixel_size(type.size_md),
          g_app.ui.font_stacks.mono,
          false)) {
    return false;
  }
  g_app.ui.font_framebuffer_scale = framebuffer_scale;
  g_app.ui.text_measurer.emplace(
      g_app.ui.fonts,
      g_app.ui.font_stacks,
      framebuffer_scale);
  g_app.ui.ready = true;
  return true;
}

[[nodiscard]] bool sync_web_ui_font_atlas() {
  if (!g_app.ui.fonts.atlas_dirty()) {
    return true;
  }
  const arv::render::TextureArrayUploadData upload =
      arv::client::texture_array_upload_from_image(g_app.ui.fonts.atlas_image());
  if (!upload_font_texture_array(upload)) {
    return false;
  }
  g_app.ui.fonts.clear_atlas_dirty();
  return true;
}

void render_gameplay_ui(arv::Seconds time_seconds) {
  if (g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame ||
      !ensure_web_ui_ready()) {
    return;
  }

  std::vector<arv::ui::UiItemStackView> inventory_slots;
  const bool item_ui_ready =
      g_app.entity_assets.ready && g_app.renderer.item_texture != 0U;
  if (item_ui_ready && g_app.session->has_authoritative_inventory()) {
    inventory_slots = arv::client::inventory_slots_for_ui(
        g_app.session->inventory(),
        g_app.entity_assets.loaded.item_registry,
        g_app.entity_assets.item_atlas,
        g_app.block_assets.loaded.block_atlas);
  }

  std::optional<arv::ui::UiItemStackView> cursor_stack;
  if (item_ui_ready) {
    cursor_stack = arv::client::item_stack_for_ui(
        g_app.session->inventory_cursor_stack(),
        g_app.entity_assets.loaded.item_registry,
        g_app.entity_assets.item_atlas,
        g_app.block_assets.loaded.block_atlas);
  }
  const auto ui_now = std::chrono::steady_clock::now();
  if (item_ui_ready && g_app.session->has_authoritative_inventory()) {
    arv::client::update_hotbar_item_popup(
        g_app.ui.hotbar_item_popup,
        g_app.session->selected_hotbar_slot(),
        std::span<const arv::ui::UiItemStackView>{
            inventory_slots.data(),
            inventory_slots.size()},
        ui_now);
  } else {
    arv::client::reset_hotbar_item_popup(g_app.ui.hotbar_item_popup);
  }
  const arv::f32 item_popup_alpha =
      arv::client::hotbar_item_popup_alpha(g_app.ui.hotbar_item_popup, ui_now);
  arv::io::UiConfig::ChatConfig chat_config = web_chat_config();
  arv::client::refresh_gameplay_chat_lines(
      g_app.ui.chat_lines,
      *g_app.session,
      chat_config,
      ui_now,
      g_app.ui.chat_history_scroll_offset);
  const arv::ui::UiDisplayMetrics metrics = web_ui_metrics();
  const arv::Vec2f viewport = metrics.logical_size;
  const arv::Vec2f cursor_position =
      g_app.input.cursor_initialized
          ? arv::Vec2f{
                static_cast<float>(g_app.input.cursor_x),
                static_cast<float>(g_app.input.cursor_y)}
          : arv::Vec2f{viewport.x * 0.5F, viewport.y * 0.5F};

  arv::ui::RidePromptModel ride_prompt;
  if (web_entity_interactions_available() &&
      !g_app.ui.screen_manager.blocks_gameplay_input() &&
      !g_app.session->inventory_open() &&
      !g_app.session->chat_open() &&
      !arv::client::local_player_is_mounted(*g_app.session)) {
    std::string key_label =
        action_binding_label(arv::platform::LogicalAction::Mount);
    if (!key_label.empty() && targeted_web_rideable_entity().has_value()) {
      ride_prompt.visible = true;
      ride_prompt.key_label = std::move(key_label);
      ride_prompt.viewport = viewport;
    }
  }

  arv::ui::GameplayUiModel model =
      arv::client::build_gameplay_ui_model(
          {
              .client = *g_app.session,
              .chat_config = chat_config,
              .inventory_slots = std::span<const arv::ui::UiItemStackView>{
                  inventory_slots.data(),
                  inventory_slots.size()},
              .carried_inventory_stack = cursor_stack,
              .chat_lines = std::span<const arv::ui::ChatLineView>{
                  g_app.ui.chat_lines.data(),
                  g_app.ui.chat_lines.size()},
              .chat_history_scroll_offset = g_app.ui.chat_history_scroll_offset,
              .inventory_title = "Inventory",
              .tab_title = "Players",
              .viewport = viewport,
              .cursor_position = cursor_position,
              .hotbar_item_popup_name = item_popup_alpha > 0.0F
                  ? std::string_view{
                        g_app.ui.hotbar_item_popup.popup_name.data(),
                        g_app.ui.hotbar_item_popup.popup_name.size()}
                  : std::string_view{},
              .hotbar_item_popup_alpha = item_popup_alpha,
              .crosshair = {
                  .visible = g_app.entity_assets.crosshair_image.valid,
                  .viewport = viewport,
                  .image = g_app.entity_assets.crosshair_image,
              },
              .ride_prompt = std::move(ride_prompt),
              .language = &g_app.ui.language,
              .debug_fps_visible = false,
          });

  g_app.ui.tree.set_theme(&g_app.ui.theme);
  arv::ui::build_gameplay_ui(g_app.ui.tree, std::move(model));
  if (g_app.ui.screen_manager.has_active_screen()) {
    arv::ui::UIContext context = make_web_ui_context();
    context.viewport = viewport;
    g_app.ui.screen_manager.append_to(context);
  }
  g_app.ui.tree.layout_with_metrics(
      metrics,
      g_app.ui.text_measurer.has_value() ? &g_app.ui.text_measurer.value() : nullptr);
  const std::optional<arv::ui::UiAction> hover_action =
      g_app.ui.tree.route_mouse_move(cursor_position);
  const bool added_inventory_tooltip =
      arv::client::append_inventory_hover_tooltip_if_needed(
          g_app.ui.tree,
          g_app.session->inventory_open(),
          cursor_stack,
          std::span<const arv::ui::UiItemStackView>{
              inventory_slots.data(),
              inventory_slots.size()},
          metrics,
          cursor_position,
          hover_action);
  const bool added_chat_tooltip =
      arv::client::append_chat_hover_tooltip_if_needed(
          g_app.ui.tree,
          g_app.session->chat_open(),
          std::span<const arv::ui::ChatLineView>{
              g_app.ui.chat_lines.data(),
              g_app.ui.chat_lines.size()},
          metrics,
          cursor_position,
          hover_action);
  if (added_inventory_tooltip || added_chat_tooltip) {
    g_app.ui.tree.layout_with_metrics(
        metrics,
        g_app.ui.text_measurer.has_value() ? &g_app.ui.text_measurer.value() : nullptr);
    (void)g_app.ui.tree.route_mouse_move(cursor_position);
  }
  arv::render::FramePacket frame;
  arv::ui::append_ui_draw_commands(
      g_app.ui.tree,
      g_app.ui.fonts,
      g_app.ui.font_stacks,
      frame,
      metrics,
      time_seconds);
  if (!sync_web_ui_font_atlas()) {
    return;
  }
  draw_ui_frame(frame);
}

void draw_sky(const arv::client::CameraShell& camera, float aspect) {
  if (g_app.renderer.sky_program == 0U) {
    return;
  }
  const arv::render::SkyUniforms sky =
      arv::client::sky_uniforms_from_clock(g_app.world_clock, web_sky_config());
  if (!sky.enabled) {
    return;
  }
  if (g_app.renderer.sky_vao == 0U) {
    glGenVertexArrays(1, &g_app.renderer.sky_vao);
  }

  const arv::Vec3f forward = camera.forward_vector();
  const arv::Vec3f right = camera.right_vector();
  const arv::Vec3f up = camera.up_vector();

  glDepthMask(GL_FALSE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glUseProgram(g_app.renderer.sky_program);
  glUniform3f(
      g_app.renderer.sky_camera_forward_location,
      forward.x,
      forward.y,
      forward.z);
  glUniform3f(
      g_app.renderer.sky_camera_right_location,
      right.x,
      right.y,
      right.z);
  glUniform3f(g_app.renderer.sky_camera_up_location, up.x, up.y, up.z);
  glUniform1f(g_app.renderer.sky_camera_aspect_location, aspect);
  glUniform1f(g_app.renderer.sky_camera_fov_y_location, camera.fov_y_degrees());
  glUniform3f(
      g_app.renderer.sky_sun_direction_location,
      sky.sun_direction.x,
      sky.sun_direction.y,
      sky.sun_direction.z);
  glUniform3f(
      g_app.renderer.sky_moon_direction_location,
      sky.moon_direction.x,
      sky.moon_direction.y,
      sky.moon_direction.z);
  glUniform3f(
      g_app.renderer.sky_daylight_color_location,
      sky.daylight_color.x,
      sky.daylight_color.y,
      sky.daylight_color.z);
  glUniform3f(
      g_app.renderer.sky_ambient_color_location,
      sky.ambient_color.x,
      sky.ambient_color.y,
      sky.ambient_color.z);
  glUniform3f(
      g_app.renderer.sky_fog_color_location,
      sky.fog_color.x,
      sky.fog_color.y,
      sky.fog_color.z);
  glUniform1f(g_app.renderer.sky_daylight_strength_location, sky.daylight_strength);
  glUniform1f(g_app.renderer.sky_moonlight_strength_location, sky.moonlight_strength);
  glUniform1f(g_app.renderer.sky_star_visibility_location, sky.star_visibility);
  glUniform1f(g_app.renderer.sky_day_fraction_location, sky.day_fraction);
  glUniform1f(g_app.renderer.sky_cloud_time_location, sky.cloud_time_seconds);
  glUniform1f(g_app.renderer.sky_cirrus_amount_location, sky.cirrus_amount);
  glUniform1f(g_app.renderer.sky_cumulus_amount_location, sky.cumulus_amount);
  glUniform1f(g_app.renderer.sky_cloud_intensity_location, sky.cloud_intensity);
  glUniform1f(g_app.renderer.sky_exposure_location, sky.exposure);

  glBindVertexArray(g_app.renderer.sky_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0U);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
}

void render_remote_world() {
  if ((g_app.renderer.chunk_program == 0U || g_app.renderer.color_program == 0U) ||
      g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame) {
    return;
  }
  sync_chunk_meshes();
  sync_entity_mesh();

  const float aspect =
      static_cast<float>(g_app.width) / static_cast<float>(std::max(g_app.height, 1));
  const arv::Seconds animation_time_seconds =
      static_cast<arv::Seconds>(emscripten_get_now() * 0.001 - g_app.start_time);
  const arv::client::CameraShell& camera = g_app.session->camera();
  const arv::Vec3f eye = camera.position();
  const arv::Vec3f center = eye + camera.forward_vector();
  const arv::Mat4f mvp = arv::multiply_mat4f(
      arv::perspective_mat4f(
          arv::degrees_to_radians(camera.fov_y_degrees()),
          aspect,
          arv::client::DefaultCameraNearPlane,
          512.0F),
      arv::look_at_mat4f(eye, center, {0.0F, 1.0F, 0.0F}));
  draw_sky(camera, aspect);

  if (g_app.renderer.meshes.empty() &&
      g_app.renderer.entity_vertex_count == 0 &&
      (!g_app.entity_assets.ready || g_app.renderer.entity_models.empty())) {
    glUseProgram(0U);
    return;
  }

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
    glUniform1f(
        g_app.renderer.chunk_animation_time_location,
        static_cast<float>(animation_time_seconds));
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
    glUniform1f(g_app.renderer.chunk_alpha_location, 1.0F);
    for (const ChunkGpuMesh& mesh : g_app.renderer.meshes) {
      draw_chunk_mesh(mesh, mesh.opaque);
    }
    draw_entity_models(mvp, false, animation_time_seconds);
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
    glUniform1f(
        g_app.renderer.chunk_animation_time_location,
        static_cast<float>(animation_time_seconds));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_app.renderer.chunk_texture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUniform1f(g_app.renderer.chunk_alpha_location, 0.55F);
    for (const ChunkGpuMesh& mesh : g_app.renderer.meshes) {
      draw_chunk_mesh(mesh, mesh.transparent);
    }
    draw_entity_models(mvp, true, animation_time_seconds);
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

void apply_web_world_time_update(const arv::protocol::ClientboundWorldTime& time) {
  if (g_app.session != nullptr &&
      g_app.session->connection_state() == arv::net::ConnectionState::InGame &&
      time.world_id != g_app.session->camera().world_id()) {
    return;
  }
  g_app.world_clock.world_tick = time.world_tick;
  g_app.world_clock.day_time_tick = time.day_time_tick;
  g_app.world_clock.day_length_ticks = std::max<arv::u32>(time.day_length_ticks, 1U);
  g_app.world_clock.day_length_seconds = time.day_length_seconds;
  g_app.world_clock.time_scale = time.time_scale;
  g_app.world_clock.daylight_cycle = time.daylight_cycle;
  g_app.world_clock.day_time_accumulator = 0.0;
}

void apply_web_world_time_updates() {
  if (g_app.session == nullptr) {
    return;
  }
  if (const std::optional<arv::protocol::ClientboundWorldTime> time =
          g_app.session->take_world_time_update();
      time.has_value()) {
    apply_web_world_time_update(time.value());
  }
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

WebShellState shell_state_for_connection(arv::net::ConnectionState state) noexcept {
  switch (state) {
    case arv::net::ConnectionState::InGame:
      return WebShellState::Game;
    case arv::net::ConnectionState::Disconnected:
      return WebShellState::Connect;
    case arv::net::ConnectionState::Connecting:
    case arv::net::ConnectionState::TransportConnected:
    case arv::net::ConnectionState::ProtocolHandshake:
    case arv::net::ConnectionState::RegistrySync:
    case arv::net::ConnectionState::WorldJoin:
    case arv::net::ConnectionState::Disconnecting:
      return WebShellState::Connecting;
  }
  return WebShellState::Connect;
}

void mark_web_section_content_changed(
    arv::protocol::ClientboundChunkSectionData& section,
    arv::u64 server_tick) noexcept {
  const arv::u64 next_content_version =
      section.chunk_content_version == std::numeric_limits<arv::u64>::max()
          ? section.chunk_content_version
          : section.chunk_content_version + 1U;
  section.chunk_content_version = std::max(next_content_version, server_tick);
  section.chunk_version = std::max(section.chunk_version, section.chunk_content_version);
}

void apply_web_chunk_section_data(arv::protocol::ClientboundChunkSectionData data) {
  const arv::WorldID world_id = g_app.session == nullptr
      ? arv::WorldID{}
      : g_app.session->camera().world_id();
  if (data.world_id != world_id) {
    return;
  }
  auto found = std::find_if(
      g_app.chunk_sections.begin(),
      g_app.chunk_sections.end(),
      [&](const arv::protocol::ClientboundChunkSectionData& existing) {
        return existing.world_id == data.world_id && existing.coord == data.coord;
      });
  if (found == g_app.chunk_sections.end()) {
    g_app.chunk_sections.push_back(std::move(data));
  } else {
    *found = std::move(data);
  }
}

void apply_web_chunk_section_unload(
    const arv::protocol::ClientboundChunkSectionUnload& unload) {
  const arv::WorldID world_id = g_app.session == nullptr
      ? arv::WorldID{}
      : g_app.session->camera().world_id();
  if (unload.world_id != world_id) {
    return;
  }
  const auto new_end = std::remove_if(
      g_app.chunk_sections.begin(),
      g_app.chunk_sections.end(),
      [&](const arv::protocol::ClientboundChunkSectionData& existing) {
        return existing.world_id == unload.world_id && existing.coord == unload.coord;
      });
  g_app.chunk_sections.erase(new_end, g_app.chunk_sections.end());
}

void apply_web_block_update_batch(arv::protocol::ClientboundBlockUpdateBatch batch) {
  const arv::WorldID world_id = g_app.session == nullptr
      ? arv::WorldID{}
      : g_app.session->camera().world_id();
  if (batch.world_id != world_id) {
    return;
  }
  for (const arv::protocol::BlockUpdate& update : batch.updates) {
    const arv::ChunkSectionCoord coord = arv::block_to_chunk(update.pos);
    auto found = std::find_if(
        g_app.chunk_sections.begin(),
        g_app.chunk_sections.end(),
        [&](const arv::protocol::ClientboundChunkSectionData& existing) {
          return existing.world_id == batch.world_id && existing.coord == coord;
        });
    if (found == g_app.chunk_sections.end()) {
      continue;
    }

    const arv::usize index = arv::voxel::local_index(arv::block_to_local(update.pos));
    found->blocks[index] = update.new_state;
    if (update.has_fluid) {
      found->has_fluids = true;
      found->fluids[index] = update.new_fluid;
    }
    mark_web_section_content_changed(*found, update.server_tick);
  }
}

void collect_runtime_chunk_stream() {
  if (g_app.session == nullptr) {
    return;
  }

  std::vector<arv::protocol::ClientboundChunkSectionUnload> unloads =
      g_app.session->drain_received_chunk_section_unloads();
  for (const arv::protocol::ClientboundChunkSectionUnload& unload : unloads) {
    apply_web_chunk_section_unload(unload);
  }

  std::vector<arv::protocol::ClientboundChunkSectionData> chunks =
      g_app.session->drain_received_chunk_section_data();
  for (arv::protocol::ClientboundChunkSectionData& chunk : chunks) {
    apply_web_chunk_section_data(std::move(chunk));
  }

  std::vector<arv::protocol::ClientboundBlockUpdateBatch> block_updates =
      g_app.session->drain_received_block_updates();
  for (arv::protocol::ClientboundBlockUpdateBatch& batch : block_updates) {
    apply_web_block_update_batch(std::move(batch));
  }
}

void poll_session() {
  if (g_app.session == nullptr) {
    return;
  }

  const arv::net::ConnectionState state = g_app.session->connection_state();
  g_app.connected = state != arv::net::ConnectionState::Disconnected &&
                    state != arv::net::ConnectionState::Connecting;
  g_app.connecting = state == arv::net::ConnectionState::Connecting ||
                     state == arv::net::ConnectionState::ProtocolHandshake;
  set_shell_state(shell_state_for_connection(state));
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
  apply_web_world_time_updates();
  if (!g_app.flushed_after_pack_ready &&
      (state == arv::net::ConnectionState::WorldJoin ||
       state == arv::net::ConnectionState::InGame) &&
      !g_app.session->multiplayer_registry_snapshot_json().empty()) {
    g_app.flushed_after_pack_ready = true;
    set_status(
        "server registry verified; syncing " +
        std::to_string(g_app.session->multiplayer_pack_cache_paths().size()) +
        " cached pack root(s)");
    flush_storage();
  }
  if (state == arv::net::ConnectionState::Disconnected) {
    g_app.flushed_after_pack_ready = false;
    g_app.chunk_sections.clear();
    clear_block_render_assets();
    g_app.ui.screen_manager.clear();
    g_app.world_clock = arv::world::make_world_clock();
    reset_input(true);
  } else if (state == arv::net::ConnectionState::InGame &&
             g_app.ui.screen_manager.empty()) {
    reset_gameplay_screen_stack();
  }
}

[[nodiscard]] std::optional<arv::usize> inventory_slot_from_ui_action(
    const arv::ui::UiAction& action) noexcept {
  return arv::client::inventory_slot_from_ui_action(action);
}

[[nodiscard]] bool apply_inventory_action(
    const std::optional<arv::ui::UiAction>& action,
    arv::player::InventoryClickButton button) {
  if (g_app.session == nullptr || !action.has_value()) {
    return false;
  }
  const std::optional<arv::usize> slot =
      inventory_slot_from_ui_action(action.value());
  if (!slot.has_value()) {
    return false;
  }
  (void)g_app.session->request_inventory_slot_click(slot.value(), button);
  return true;
}

[[nodiscard]] bool apply_inventory_shift_transfer(
    const std::optional<arv::ui::UiAction>& action) {
  if (g_app.session == nullptr || !action.has_value()) {
    return false;
  }
  const std::optional<arv::usize> slot =
      inventory_slot_from_ui_action(action.value());
  if (!slot.has_value()) {
    return false;
  }
  (void)g_app.session->request_inventory_shift_transfer(slot.value());
  return true;
}

[[nodiscard]] bool route_chat_mouse_input() {
  if (g_app.session == nullptr || !g_app.session->chat_open()) {
    return false;
  }

  bool handled = false;
  const arv::Vec2f cursor{
      static_cast<float>(g_app.input.cursor_x),
      static_cast<float>(g_app.input.cursor_y)};
  (void)g_app.ui.tree.route_mouse_move(cursor);
  if (g_app.input.mouse_wheel_y != 0.0) {
    const arv::io::UiConfig::ChatConfig chat_config = web_chat_config();
    const arv::usize max_offset = arv::client::gameplay_chat_history_max_scroll_offset(
        g_app.session->chat_history().size(),
        std::max<arv::usize>(1U, chat_config.open_max_lines));
    g_app.ui.chat_history_scroll_offset =
        std::min(g_app.ui.chat_history_scroll_offset, max_offset);
    const arv::usize steps =
        arv::client::gameplay_chat_scroll_steps(
            static_cast<arv::f32>(g_app.input.mouse_wheel_y));
    if (steps > 0U) {
      if (g_app.input.mouse_wheel_y > 0.0) {
        const arv::usize remaining =
            max_offset > g_app.ui.chat_history_scroll_offset
                ? max_offset - g_app.ui.chat_history_scroll_offset
                : 0U;
        g_app.ui.chat_history_scroll_offset += std::min(steps, remaining);
      } else {
        g_app.ui.chat_history_scroll_offset -=
            std::min(steps, g_app.ui.chat_history_scroll_offset);
      }
      handled = true;
    }
  }

  if (g_app.input.mouse_left_pressed) {
    const std::optional<arv::ui::UiAction> action =
        g_app.ui.tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Left);
    if (action.has_value() && action->action_id == "chat_url_cancel") {
      g_app.session->cancel_chat_url_confirmation();
      handled = true;
    } else if (action.has_value() && action->action_id == "chat_url_confirm") {
      const std::optional<std::string> pending =
          g_app.session->pending_chat_url_confirmation();
      if (pending.has_value() &&
          arv::client::safe_chat_url_scheme(pending.value()) &&
          open_url(pending.value())) {
        g_app.session->cancel_chat_url_confirmation();
      } else {
        g_app.session->acknowledge_chat_url_open_unavailable();
      }
      handled = true;
    } else if (action.has_value()) {
      const std::optional<std::pair<arv::usize, arv::usize>> run_id =
          arv::client::chat_run_from_ui_action(action.value());
      if (run_id.has_value() &&
          run_id->first < g_app.ui.chat_lines.size() &&
          run_id->second < g_app.ui.chat_lines[run_id->first].runs.size()) {
        const arv::text::RichTextRun& run =
            g_app.ui.chat_lines[run_id->first].runs[run_id->second];
        if (run.click_event.has_value()) {
          const arv::text::ClickEvent& click = run.click_event.value();
          switch (click.action) {
            case arv::text::ClickAction::SuggestCommand:
              g_app.session->suggest_chat_command(click.value);
              handled = true;
              break;
            case arv::text::ClickAction::OpenUrl:
              if (arv::client::safe_chat_url_scheme(click.value)) {
                g_app.session->request_chat_url_confirmation(click.value);
              } else {
                g_app.session->add_chat_message(
                    g_app.ui.language.text("ui.chat.blocked_unsafe_url"));
              }
              handled = true;
              break;
            case arv::text::ClickAction::RunCommand: {
              const arv::net::NetworkResult result =
                  g_app.session->send_chat_message(click.value);
              if (!result) {
                g_app.session->add_chat_message(result.error().message);
              }
              handled = true;
              break;
            }
            case arv::text::ClickAction::CopyToClipboard:
              if (!copy_text_to_clipboard(click.value)) {
                g_app.session->add_chat_message(
                    g_app.ui.language.text("ui.chat.copy_unavailable"));
              }
              handled = true;
              break;
          }
        }
      }
    }
  }
  if (g_app.input.mouse_left_released) {
    (void)g_app.ui.tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Left);
  }
  if (g_app.input.mouse_right_pressed) {
    (void)g_app.ui.tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Right);
  }
  if (g_app.input.mouse_right_released) {
    (void)g_app.ui.tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Right);
  }
  return handled;
}

[[nodiscard]] bool route_inventory_mouse_input() {
  if (g_app.session == nullptr || !g_app.session->inventory_open()) {
    return false;
  }
  bool changed = false;
  const arv::Vec2f cursor{
      static_cast<float>(g_app.input.cursor_x),
      static_cast<float>(g_app.input.cursor_y)};
  (void)g_app.ui.tree.route_mouse_move(cursor);
  if (g_app.input.mouse_left_pressed) {
    const std::optional<arv::ui::UiNodeID> hit = g_app.ui.tree.hit_test(cursor);
    const std::optional<arv::ui::UiAction> action =
        g_app.ui.tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Left);
    if (g_app.input.shift_modifier_held && !g_app.session->container_open()) {
      changed = apply_inventory_shift_transfer(action);
    }
    changed = changed ||
              apply_inventory_action(
                  action,
                  arv::player::InventoryClickButton::Primary);
    if (!changed && !hit.has_value() &&
        g_app.session->inventory_cursor_stack().has_value()) {
      (void)g_app.session->request_inventory_drop_cursor(false);
      changed = true;
    }
  }
  if (g_app.input.mouse_right_pressed) {
    const std::optional<arv::ui::UiNodeID> hit = g_app.ui.tree.hit_test(cursor);
    changed = apply_inventory_action(
                  g_app.ui.tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Right),
                  arv::player::InventoryClickButton::Secondary) ||
              changed;
    if (!changed && !hit.has_value() &&
        g_app.session->inventory_cursor_stack().has_value()) {
      (void)g_app.session->request_inventory_drop_cursor(true);
      changed = true;
    }
  }
  if (g_app.input.mouse_left_released) {
    (void)g_app.ui.tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Left);
  }
  if (g_app.input.mouse_right_released) {
    (void)g_app.ui.tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Right);
  }
  return changed;
}

void route_screen_mouse_input(arv::ui::UIContext& context) {
  const arv::Vec2f cursor{
      static_cast<float>(g_app.input.cursor_x),
      static_cast<float>(g_app.input.cursor_y)};
  (void)g_app.ui.tree.route_mouse_move(cursor);
  if (g_app.input.mouse_left_pressed) {
    const std::optional<arv::ui::UiAction> action =
        g_app.ui.tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Left);
    if (action.has_value()) {
      (void)g_app.ui.screen_manager.route_action(action.value(), context);
    }
  }
  if (g_app.input.mouse_left_released) {
    (void)g_app.ui.tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Left);
  }
  if (g_app.input.mouse_right_pressed) {
    (void)g_app.ui.tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Right);
  }
  if (g_app.input.mouse_right_released) {
    (void)g_app.ui.tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Right);
  }
  (void)g_app.ui.screen_manager.handle_pointer(
      cursor,
      {
          static_cast<float>(g_app.input.mouse_dx),
          static_cast<float>(g_app.input.mouse_dy),
      },
      g_app.input.mouse_left_pressed,
      g_app.input.mouse_left_released,
      g_app.input.mouse_left_held,
      context);
  if (g_app.input.mouse_wheel_y != 0.0) {
    (void)g_app.ui.screen_manager.handle_scroll(
        cursor,
        static_cast<float>(g_app.input.mouse_wheel_y),
        context);
  }
}

void handle_pause_menu_input(arv::ui::UIContext& context) {
  if (!g_app.input.pause_menu_pressed) {
    return;
  }
  if (g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame) {
    g_app.input.pause_menu_pressed = false;
    return;
  }
  const bool has_client_overlay =
      g_app.session->inventory_open() || g_app.session->chat_open();
  if (g_app.ui.screen_manager.empty()) {
    reset_gameplay_screen_stack();
  }
  const arv::ui::UIScreen* current = g_app.ui.screen_manager.current();
  const bool on_gameplay_root =
      current != nullptr && current->type() == arv::ui::MenuScreenType::Gameplay;
  if (current != nullptr && !on_gameplay_root) {
    (void)g_app.ui.screen_manager.handle_cancel(context);
    g_app.input.text_cancel_pressed = false;
  } else if (on_gameplay_root && !has_client_overlay) {
    (void)g_app.ui.screen_manager.handle_cancel(context);
    g_app.input.text_cancel_pressed = false;
  }
  g_app.input.pause_menu_pressed = false;
}

void clear_input_after_captured_binding() {
  g_app.input.mouse_left_pressed = false;
  g_app.input.mouse_right_pressed = false;
  g_app.input.mouse_left_released = false;
  g_app.input.mouse_right_released = false;
  g_app.input.mouse_wheel_y = 0.0;
  g_app.input.primary_pressed = false;
  g_app.input.secondary_pressed = false;
  g_app.input.primary_animation_pressed = false;
  g_app.input.secondary_animation_pressed = false;
  g_app.input.jump_pressed = false;
  g_app.input.mount_pressed = false;
  g_app.input.drop_item_pressed = false;
  g_app.input.drop_item_held = false;
  g_app.input.toggle_fly_pressed = false;
  g_app.input.open_inventory_pressed = false;
  g_app.input.open_chat_pressed = false;
  g_app.input.text_backspace_pressed = false;
  g_app.input.text_submit_pressed = false;
  g_app.input.text_cancel_pressed = false;
  g_app.input.pause_menu_pressed = false;
  g_app.input.hotbar_select.reset();
  g_app.input.hotbar_scroll_steps = 0;
  g_app.input.captured_input.reset();
}

[[nodiscard]] bool route_captured_screen_input(arv::ui::UIContext& context) {
  if (!g_app.input.captured_input.has_value() ||
      !g_app.ui.screen_manager.has_active_screen()) {
    return false;
  }
  const arv::ui::CapturedInputView captured = g_app.input.captured_input.value();
  if (!g_app.ui.screen_manager.handle_captured_input(captured, context)) {
    return false;
  }
  clear_input_after_captured_binding();
  return true;
}

void route_web_ui_input() {
  arv::ui::UIContext context = make_web_ui_context();
  if (route_captured_screen_input(context)) {
    return;
  }
  handle_pause_menu_input(context);
  if (g_app.ui.screen_manager.blocks_gameplay_input()) {
    route_screen_mouse_input(context);
    return;
  }
  (void)route_chat_mouse_input();
  (void)route_inventory_mouse_input();
}

[[nodiscard]] arv::client::InputFrame build_browser_input_frame() {
  arv::client::InputFrame input;
  input.move_forward = g_app.input.forward;
  input.move_backward = g_app.input.back;
  input.move_left = g_app.input.left;
  input.move_right = g_app.input.right;
  input.jump_pressed = g_app.input.jump_pressed;
  input.jump_held = g_app.input.jump;
  input.sneak_held = g_app.input.sneak;
  input.sprint_held = g_app.input.sprint;
  input.mount_pressed = g_app.input.mount_pressed;
  input.vehicle_roll_left_held = g_app.input.vehicle_roll_left_held;
  input.vehicle_roll_right_held = g_app.input.vehicle_roll_right_held;
  input.toggle_fly_pressed = g_app.input.toggle_fly_pressed;
  input.open_inventory_pressed = g_app.input.open_inventory_pressed;
  input.open_chat_pressed = g_app.input.open_chat_pressed;
  input.tab_menu_held = g_app.input.tab_menu_held;
  input.primary_pressed = g_app.input.primary_pressed;
  input.primary_held = g_app.input.primary_held;
  input.secondary_pressed = g_app.input.secondary_pressed;
  input.secondary_held = g_app.input.secondary_held;
  input.drop_item_pressed = g_app.input.drop_item_pressed;
  input.drop_item_held = g_app.input.drop_item_held;
  input.hotbar_select = g_app.input.hotbar_select;
  input.hotbar_scroll_steps = g_app.input.hotbar_scroll_steps;
  input.mouse_delta_x = static_cast<float>(g_app.input.mouse_dx);
  input.mouse_delta_y = static_cast<float>(g_app.input.mouse_dy);
  input.text_input = g_app.input.text_input;
  input.text_backspace_pressed = g_app.input.text_backspace_pressed;
  input.text_submit_pressed = g_app.input.text_submit_pressed;
  input.text_cancel_pressed = g_app.input.text_cancel_pressed;
  return input;
}

void clear_per_frame_input_edges() {
  g_app.input.mouse_dx = 0.0;
  g_app.input.mouse_dy = 0.0;
  g_app.input.mouse_left_pressed = false;
  g_app.input.mouse_right_pressed = false;
  g_app.input.mouse_left_released = false;
  g_app.input.mouse_right_released = false;
  g_app.input.mouse_wheel_y = 0.0;
  g_app.input.primary_animation_pressed = false;
  g_app.input.secondary_animation_pressed = false;
  g_app.input.jump_pressed = false;
  g_app.input.mount_pressed = false;
  g_app.input.drop_item_pressed = false;
  g_app.input.toggle_fly_pressed = false;
  g_app.input.open_inventory_pressed = false;
  g_app.input.open_chat_pressed = false;
  g_app.input.text_input.clear();
  g_app.input.text_backspace_pressed = false;
  g_app.input.text_submit_pressed = false;
  g_app.input.text_cancel_pressed = false;
  g_app.input.pause_menu_pressed = false;
  g_app.input.captured_input.reset();
  g_app.input.hotbar_select.reset();
  g_app.input.hotbar_scroll_steps = 0;
  g_app.input.primary_pressed = false;
  g_app.input.secondary_pressed = false;
}

void submit_web_input_and_tick_runtime(double now) {
  if (g_app.session == nullptr) {
    return;
  }
  const bool wanted_cursor_before = ui_wants_cursor();
  const double dt_seconds = std::clamp(
      now - g_app.input.last_frame_time,
      0.0,
      0.1);
  g_app.input.last_frame_time = now;

  route_web_ui_input();
  if (g_app.session == nullptr) {
    clear_per_frame_input_edges();
    return;
  }
  arv::client::InputFrame input = build_browser_input_frame();
  if (g_app.ui.screen_manager.blocks_gameplay_input()) {
    input = {};
  }
  arv::client::InputFrame animation_input = input;
  animation_input.primary_pressed = g_app.input.primary_animation_pressed;
  animation_input.secondary_pressed = g_app.input.secondary_animation_pressed;
  const arv::Seconds gameplay_time_seconds =
      static_cast<arv::Seconds>(now - g_app.start_time);
  send_web_entity_interactions_from_input(input, gameplay_time_seconds);
  g_app.session->submit_input(std::move(input));
  const bool wants_cursor_after = ui_wants_cursor();
  if (wants_cursor_after) {
    release_pointer_lock();
  } else if (wanted_cursor_before && !g_app.pointer_locked) {
    request_pointer_lock();
  }
  update_held_item_animation_from_input(
      animation_input,
      gameplay_time_seconds);
  clear_per_frame_input_edges();

  g_app.session->advance_entity_interpolation(static_cast<arv::Seconds>(dt_seconds));
  g_app.session->advance_particles(static_cast<arv::Seconds>(dt_seconds));

  const arv::TickRate tick_rate =
      g_app.session->server_tick_rate() == 0U
          ? arv::DefaultBaseTps
          : g_app.session->server_tick_rate();
  const arv::Seconds tick_seconds = arv::tick_duration_seconds(tick_rate);
  g_app.input.movement_accumulator_seconds = std::min(
      g_app.input.movement_accumulator_seconds + dt_seconds,
      tick_seconds * 5.0);

  int fixed_steps = 0;
  while (g_app.input.movement_accumulator_seconds + 1.0e-9 >= tick_seconds &&
         fixed_steps < 5) {
    ++fixed_steps;
    g_app.input.movement_accumulator_seconds -= tick_seconds;

    const bool was_in_game =
        g_app.session->connection_state() == arv::net::ConnectionState::InGame;
    if (was_in_game) {
      if (g_app.input.client_tick == 0U) {
        g_app.input.client_tick = g_app.session->world_initial_tick();
      }
      ++g_app.input.client_tick;
    }

    std::optional<arv::client::ChunkSectionCollisionQuery> collision_query;
    if (g_app.block_assets.ready) {
      collision_query.emplace(
          g_app.chunk_sections,
          g_app.block_assets.loaded.block_registry);
    }
    g_app.session->fixed_tick(
        g_app.input.client_tick,
        collision_query.has_value() ? &collision_query.value() : nullptr);
    if (was_in_game) {
      arv::world::tick_world_clock(g_app.world_clock);
    }
    collect_runtime_chunk_stream();
    if (!was_in_game &&
        g_app.session->connection_state() == arv::net::ConnectionState::InGame) {
      g_app.input.client_tick = g_app.session->world_initial_tick();
    }
  }
}

void disconnect_web_session() {
  if (g_app.session != nullptr) {
    g_app.session->disconnect();
    g_app.session.reset();
  }
  clear_block_render_assets();
  g_app.chunk_sections.clear();
  g_app.ui.screen_manager.clear();
  g_app.world_clock = arv::world::make_world_clock();
  reset_input(true);
  flush_storage();
  g_app.connected = false;
  g_app.connecting = false;
  g_app.flushed_after_pack_ready = false;
  g_app.last_connection_state = arv::net::ConnectionState::Disconnected;
  set_shell_state(WebShellState::Connect);
  set_status("disconnected");
  if (g_app.context > 0) {
    glClearColor(0.09F, 0.12F, 0.14F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int arv_web_crypto_self_test() {
  if (!arv::crypto::initialize_crypto()) {
    return 1;
  }

  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity("WebSelfTest");
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
  poll_session();
  resize_canvas();
  submit_web_input_and_tick_runtime(now);
  const float pulse = static_cast<float>(0.5 + 0.5 * std::sin((now - g_app.start_time) * 0.85));
  const float base = g_app.connected ? 0.16F : (g_app.connecting ? 0.12F : 0.09F);
  glClearColor(base + pulse * 0.04F, 0.12F + pulse * 0.03F, 0.14F + pulse * 0.02F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  render_remote_world();
  render_gameplay_ui(static_cast<arv::Seconds>(now - g_app.start_time));
}

EMSCRIPTEN_KEEPALIVE void arv_web_connect(const char* url) {
  if (!storage_ready()) {
    set_status("browser storage is not ready");
    return;
  }
  if (url == nullptr || std::strlen(url) == 0U) {
    set_status("missing WebSocket URL");
    return;
  }

  if (g_app.session != nullptr) {
    disconnect_web_session();
  }
  clear_block_render_assets();
  g_app.chunk_sections.clear();
  g_app.ui.screen_manager.clear();
  g_app.world_clock = arv::world::make_world_clock();
  reset_input(true);
  auto transport = std::make_unique<arv::net::WebSocketClientTransport>();
  arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity("WebPlayer");
  identity.arvane_home = "/arvane_home";
  g_app.session = std::make_unique<arv::client::ClientRuntime>(std::move(transport));
  g_app.session->set_local_identity(std::move(identity));
  g_app.session->set_player_tuning(web_player_tuning(g_ui_bridge.controls_settings()));
  g_app.session->configure_chat({g_ui_bridge.interface_settings().chat_history_max});
  g_app.session->set_chunk_view_distance(g_ui_bridge.render_settings().stream_distance);
  arv::net::NetworkAddress address;
  address.host = url;
  address.port = 0U;
  auto connected = g_app.session->connect(address);
  if (!connected) {
    g_app.connected = false;
    g_app.connecting = false;
    g_app.session.reset();
    set_shell_state(WebShellState::Connect);
    set_status("WebSocket connect failed: " + connected.error().message);
    return;
  }

  g_app.connected = false;
  g_app.connecting = true;
  g_app.flushed_after_pack_ready = false;
  g_app.last_connection_state = arv::net::ConnectionState::Connecting;
  set_shell_state(WebShellState::Connecting);
  set_status("connecting WebSocket");
}

EMSCRIPTEN_KEEPALIVE void arv_web_disconnect() {
  disconnect_web_session();
}

} // extern "C"

int main() {
  g_app.start_time = emscripten_get_now() * 0.001;
  set_shell_state(WebShellState::Boot);
  set_status("starting Arvane web client");
  if (!initialize_webgl()) {
    return 1;
  }
  install_input_callbacks();
  note_runtime_ready();
  emscripten_set_main_loop(arv_web_frame, 0, EM_TRUE);
  return 0;
}
