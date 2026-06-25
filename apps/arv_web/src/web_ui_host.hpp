#pragma once

#include "arv/base/ui/screens/screen.hpp"
#include "arv/io/config.hpp"
#include "arv/ui/core/ui.hpp"

namespace arv::client {
class ClientRuntime;
} // namespace arv::client

namespace arv::asset {
class PackManager;
} // namespace arv::asset

namespace arv::web {

using WebUiDisconnectCallback = void (*)();

void set_web_ui_disconnect_callback(WebUiDisconnectCallback callback) noexcept;

[[nodiscard]] arv::ui::RenderSettingsView default_web_render_settings() noexcept;

[[nodiscard]] arv::ui::RenderSettingsView web_render_settings();
[[nodiscard]] arv::ui::ControlsSettingsView web_controls_settings();
[[nodiscard]] arv::ui::InterfaceSettingsView web_interface_settings();
void apply_web_ui_settings_to_session(arv::client::ClientRuntime& session);

[[nodiscard]] arv::io::UiConfig::ChatConfig web_chat_config() noexcept;
[[nodiscard]] arv::io::RenderConfig::SkyConfig web_sky_config() noexcept;

[[nodiscard]] arv::ui::UiDisplayMetrics web_ui_metrics();
[[nodiscard]] arv::ui::UIContext make_web_ui_context();
void route_web_ui_input();

[[nodiscard]] bool load_web_language(const arv::asset::PackManager& packs);
[[nodiscard]] bool ensure_web_ui_ready();
[[nodiscard]] bool sync_web_ui_font_atlas();

} // namespace arv::web
