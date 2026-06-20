#pragma once

#include "arv/base/ui/screens/screen.hpp"
#include "arv/io/config.hpp"
#include "arv/platform/input_bindings.hpp"
#include "arv/player/player_constants.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::client {

[[nodiscard]] ui::RenderSettingsView normalized_render_settings(
    ui::RenderSettingsView settings) noexcept;

[[nodiscard]] ui::ControlsSettingsView normalized_controls_settings(
    ui::ControlsSettingsView settings) noexcept;

[[nodiscard]] ui::InterfaceSettingsView normalized_interface_settings(
    ui::InterfaceSettingsView settings) noexcept;

[[nodiscard]] f32 mouse_sensitivity_radians_from_percent(
    f32 percent,
    f32 baseline_radians) noexcept;

[[nodiscard]] f32 mouse_sensitivity_percent_from_radians(
    f32 radians,
    f32 baseline_radians) noexcept;

[[nodiscard]] io::UiConfig::ChatConfig chat_config_from_interface_settings(
    ui::InterfaceSettingsView settings) noexcept;

[[nodiscard]] io::RenderConfig::SkyConfig sky_config_from_render_settings(
    ui::RenderSettingsView settings) noexcept;

[[nodiscard]] io::RenderConfig::BlockOutlineConfig block_outline_config_from_render_settings(
    ui::RenderSettingsView settings) noexcept;

[[nodiscard]] player::PlayerTuning player_tuning_from_controls_settings(
    ui::ControlsSettingsView settings,
    player::PlayerTuning base_tuning = player::default_player_tuning(),
    f32 baseline_radians = player::DefaultMouseSensitivityRadians) noexcept;

[[nodiscard]] platform::InputDeviceType platform_input_device_from_ui(
    ui::InputBindingDevice type) noexcept;

[[nodiscard]] std::optional<platform::InputBinding> platform_binding_from_ui(
    const ui::InputBindingView& binding);

[[nodiscard]] ui::InputBindingView input_binding_view_from_platform(
    const platform::InputBinding& binding);

[[nodiscard]] std::string key_bind_conflict_key(
    const platform::InputBinding& binding);

[[nodiscard]] std::string key_bind_display_name(std::string_view action_name);

[[nodiscard]] std::vector<ui::KeyBindEntryView> key_bind_entries_from_input_bindings(
    const platform::InputBindings& input_bindings);

bool apply_key_bind(
    platform::InputBindings& input_bindings,
    const ui::SetKeyBindCommand& command);

bool reset_key_bind(
    platform::InputBindings& input_bindings,
    const ui::ResetKeyBindCommand& command);

} // namespace arv::client
