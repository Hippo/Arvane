#include "web_ui_host.hpp"

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/base/ui/widgets/gameplay.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/client_input_mapping.hpp"
#include "arv/client/client_settings.hpp"
#include "arv/client/client_ui_assets.hpp"
#include "arv/client/client_ui_routing.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/net/transport.hpp"
#include "arv/render/render_types.hpp"
#include "arv/text/language.hpp"
#include "web_application.hpp"
#include "web_client_assets.hpp"
#include "web_platform.hpp"
#include "web_render_host.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace arv::web {
namespace {

WebUiDisconnectCallback g_disconnect_callback{};

class WebUiBridge final : public arv::ui::UICommandSink,
                          public arv::ui::UIScreenDataSource,
                          public arv::ui::UIClipboard {
public:
  void submit(arv::ui::UICommand command) override {
    if (std::holds_alternative<arv::ui::BackToMainMenuCommand>(command) ||
        std::holds_alternative<arv::ui::QuitGameCommand>(command)) {
      if (g_disconnect_callback != nullptr) {
        g_disconnect_callback();
      }
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
    return arv::client::key_bind_entries_from_input_bindings(g_app.input_bindings);
  }

  [[nodiscard]] std::string text() const override {
    return clipboard_;
  }

  void set_text(std::string text) override {
    clipboard_ = std::move(text);
  }

private:
  void apply_render_settings(arv::ui::RenderSettingsView settings) {
    render_settings_ = arv::client::normalized_render_settings(settings);
    if (g_app.session != nullptr) {
      g_app.session->set_chunk_view_distance(render_settings_.stream_distance);
    }
  }

  void apply_controls_settings(arv::ui::ControlsSettingsView settings) {
    controls_settings_ = arv::client::normalized_controls_settings(settings);
    if (g_app.session != nullptr) {
      g_app.session->set_player_tuning(
          arv::client::player_tuning_from_controls_settings(controls_settings_));
    }
  }

  void apply_interface_settings(arv::ui::InterfaceSettingsView settings) {
    interface_settings_ = arv::client::normalized_interface_settings(settings);
    if (g_app.session != nullptr) {
      const arv::io::UiConfig::ChatConfig chat_config =
          arv::client::chat_config_from_interface_settings(interface_settings_);
      g_app.session->configure_chat({chat_config.max_history_messages});
    }
  }

  void apply_key_bind(const arv::ui::SetKeyBindCommand& command) {
    (void)arv::client::apply_key_bind(g_app.input_bindings, command);
  }

  void apply_key_bind_reset(const arv::ui::ResetKeyBindCommand& command) {
    (void)arv::client::reset_key_bind(g_app.input_bindings, command);
  }

  arv::ui::RenderSettingsView render_settings_{
      arv::client::normalized_render_settings(default_web_render_settings())};
  arv::ui::ControlsSettingsView controls_settings_{
      arv::client::normalized_controls_settings({})};
  arv::ui::InterfaceSettingsView interface_settings_{
      arv::client::normalized_interface_settings({})};
  std::string clipboard_{};
};

WebUiBridge g_ui_bridge;

void report_font_upload_status(std::string_view status) {
  set_status(std::string{status});
}

[[nodiscard]] bool upload_font_texture_array(
    const arv::render::TextureArrayUploadData& upload) {
  const auto upload_backend =
      [](const arv::render::TextureArrayUploadData& data) -> bool {
    return set_web_ui_font_atlas(data);
  };
  return arv::client::upload_texture_array_with_status(
      upload,
      "font atlas upload was empty",
      upload_backend,
      report_font_upload_status);
}

} // namespace

bool load_web_language(const arv::asset::PackManager& packs) {
  auto language = arv::client::load_pack_language(packs);
  if (!language) {
    set_status("failed to load web language: " + language.error().message);
    return false;
  }
  g_app.ui.language = std::move(language.value());
  if (g_app.session != nullptr) {
    g_app.session->configure_language(&g_app.ui.language);
  }
  return true;
}

void set_web_ui_disconnect_callback(WebUiDisconnectCallback callback) noexcept {
  g_disconnect_callback = callback;
}

arv::ui::RenderSettingsView default_web_render_settings() noexcept {
  arv::ui::RenderSettingsView settings;
  settings.load_distance = 4;
  settings.view_distance = 4;
  settings.stream_distance = 4;
  return settings;
}

arv::ui::RenderSettingsView web_render_settings() {
  return g_ui_bridge.render_settings();
}

arv::ui::ControlsSettingsView web_controls_settings() {
  return g_ui_bridge.controls_settings();
}

arv::ui::InterfaceSettingsView web_interface_settings() {
  return g_ui_bridge.interface_settings();
}

void apply_web_ui_settings_to_session(arv::client::ClientRuntime& session) {
  session.set_player_tuning(
      arv::client::player_tuning_from_controls_settings(
          g_ui_bridge.controls_settings()));
  const arv::io::UiConfig::ChatConfig chat_config =
      arv::client::chat_config_from_interface_settings(
          g_ui_bridge.interface_settings());
  session.configure_chat({chat_config.max_history_messages});
  session.set_chunk_view_distance(g_ui_bridge.render_settings().stream_distance);
}

arv::io::UiConfig::ChatConfig web_chat_config() noexcept {
  return arv::client::chat_config_from_interface_settings(
      g_ui_bridge.interface_settings());
}

arv::io::RenderConfig::SkyConfig web_sky_config() noexcept {
  return arv::client::sky_config_from_render_settings(g_ui_bridge.render_settings());
}

arv::ui::UiDisplayMetrics web_ui_metrics() {
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

arv::ui::UIContext make_web_ui_context() {
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

namespace {

[[nodiscard]] const arv::platform::InputSnapshot& browser_input_snapshot() {
  return g_app.input.snapshot;
}

[[nodiscard]] bool route_chat_mouse_input() {
  if (g_app.session == nullptr || !g_app.session->chat_open()) {
    return false;
  }
  const auto open_confirmed_url = [](std::string_view url) {
    return open_url(std::string{url});
  };
  const auto copy_chat_text = [](std::string_view text) {
    return copy_text_to_clipboard(std::string{text});
  };
  const auto run_chat_command = [](std::string_view command_text) {
    const arv::net::NetworkResult result =
        g_app.session->send_chat_message(std::string{command_text});
    if (!result) {
      g_app.session->add_chat_message(result.error().message);
    }
  };
  const arv::client::ChatMouseRoutingCallbacks callbacks{
      .open_url = open_confirmed_url,
      .copy_to_clipboard = copy_chat_text,
      .run_command = run_chat_command,
      .blocked_unsafe_url_message =
          g_app.ui.language.text("ui.chat.blocked_unsafe_url"),
      .copy_unavailable_message =
          g_app.ui.language.text("ui.chat.copy_unavailable"),
  };
  const arv::io::UiConfig::ChatConfig chat_config = web_chat_config();
  return arv::client::route_chat_mouse_input(
      g_app.ui.tree,
      *g_app.session,
      browser_input_snapshot(),
      std::span<const arv::ui::ChatLineView>{
          g_app.ui.chat_lines.data(),
          g_app.ui.chat_lines.size()},
      g_app.session->chat_history().size(),
      chat_config.open_max_lines,
      g_app.ui.chat_history_scroll_offset,
      callbacks);
}

[[nodiscard]] bool route_inventory_mouse_input() {
  if (g_app.session == nullptr || !g_app.session->inventory_open()) {
    return false;
  }
  arv::ui::UIContext context = make_web_ui_context();
  return arv::client::route_inventory_mouse_input(
      g_app.ui.tree,
      *g_app.session,
      browser_input_snapshot(),
      g_app.session->inventory_cursor_stack().has_value(),
      context.command_sink,
      context.data_source);
}

void route_screen_mouse_input(arv::ui::UIContext& context) {
  (void)arv::client::route_blocking_screen_input(
      g_app.ui.screen_manager,
      context,
      browser_input_snapshot());
}

void clear_input_after_captured_binding() {
  arv::platform::InputSnapshot& input = g_app.input.snapshot;
  arv::client::clear_platform_input_captured_binding_edges(input);
  input.mouse_left_pressed = false;
  input.mouse_right_pressed = false;
  input.mouse_left_released = false;
  input.mouse_right_released = false;
  input.primary_pressed = false;
  input.secondary_pressed = false;
  g_app.input.primary_animation_pressed = false;
  g_app.input.secondary_animation_pressed = false;
  input.jump_pressed = false;
  input.mount_pressed = false;
  input.drop_item_pressed = false;
  input.drop_item_held = false;
  input.toggle_fly_pressed = false;
}

[[nodiscard]] bool route_captured_screen_input(arv::ui::UIContext& context) {
  if (g_app.session != nullptr &&
      arv::client::route_player_menu_captured_input(
          g_app.ui.tree,
          *g_app.session,
          context.command_sink,
          browser_input_snapshot())) {
    clear_input_after_captured_binding();
    return true;
  }
  if (!arv::client::route_captured_screen_input(
          g_app.ui.screen_manager,
          context,
          browser_input_snapshot())) {
    return false;
  }
  clear_input_after_captured_binding();
  return true;
}

} // namespace

void route_web_ui_input() {
  arv::ui::UIContext context = make_web_ui_context();
  if (route_captured_screen_input(context)) {
    return;
  }
  if (g_app.ui.screen_manager.blocks_gameplay_input()) {
    route_screen_mouse_input(context);
    return;
  }
  (void)route_chat_mouse_input();
  (void)route_inventory_mouse_input();
}

bool ensure_web_ui_ready() {
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
  if (!load_web_language(packs)) {
    return false;
  }
  auto font_stacks = arv::client::load_ui_font_stacks(
      resources,
      g_app.ui.fonts,
      g_app.ui.theme,
      metrics);
  if (!font_stacks) {
    set_status("failed to load web UI font: " + font_stacks.error().message);
    return false;
  }
  g_app.ui.font_stacks = std::move(font_stacks.value());
  g_app.ui.font_framebuffer_scale = framebuffer_scale;
  g_app.ui.text_measurer.emplace(
      g_app.ui.fonts,
      g_app.ui.font_stacks,
      framebuffer_scale);
  g_app.ui.ready = true;
  return true;
}

bool sync_web_ui_font_atlas() {
  const auto upload_font_atlas =
      [](const arv::render::TextureArrayUploadData& upload) -> bool {
    return upload_font_texture_array(upload);
  };
  return arv::client::sync_ui_font_atlas(g_app.ui.fonts, upload_font_atlas);
}

} // namespace arv::web
