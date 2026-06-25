#include "arv/client/client_settings.hpp"

#include "arv/client/client_input_mapping.hpp"

#include "arv/core/types.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace arv::client {

namespace {

inline constexpr i32 MaxConfigurableChunkDistance = 32;

} // namespace

ui::RenderSettingsView normalized_render_settings(
    ui::RenderSettingsView settings) noexcept {
  settings.load_distance =
      std::clamp(settings.load_distance, 1, MaxConfigurableChunkDistance);
  settings.simulation_distance =
      std::clamp(settings.simulation_distance, 0, settings.load_distance);
  settings.stream_distance =
      std::clamp(settings.stream_distance, 1, settings.load_distance);
  settings.view_distance =
      std::clamp(settings.view_distance, 1, settings.stream_distance);
  settings.fps_cap = std::clamp<u32>(settings.fps_cap, 30U, 240U);
  settings.mesh_upload_budget =
      std::clamp<u32>(settings.mesh_upload_budget, 1U, 32U);
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

ui::ControlsSettingsView normalized_controls_settings(
    ui::ControlsSettingsView settings) noexcept {
  settings.mouse_sensitivity_percent =
      std::clamp(settings.mouse_sensitivity_percent, 10.0F, 300.0F);
  return settings;
}

ui::InterfaceSettingsView normalized_interface_settings(
    ui::InterfaceSettingsView settings) noexcept {
  settings.chat_history_max =
      std::clamp<u32>(settings.chat_history_max, 100U, 5000U);
  settings.chat_open_lines =
      std::clamp<u32>(settings.chat_open_lines, 1U, 16U);
  settings.chat_passive_lines =
      std::clamp<u32>(settings.chat_passive_lines, 0U, 16U);
  settings.chat_passive_visible_seconds =
      std::clamp(settings.chat_passive_visible_seconds, 0.0F, 30.0F);
  settings.chat_passive_fade_seconds =
      std::clamp(settings.chat_passive_fade_seconds, 0.0F, 30.0F);
  return settings;
}

f32 mouse_sensitivity_radians_from_percent(
    f32 percent,
    f32 baseline_radians) noexcept {
  const ui::ControlsSettingsView settings =
      normalized_controls_settings({percent});
  return baseline_radians * (settings.mouse_sensitivity_percent / 100.0F);
}

f32 mouse_sensitivity_percent_from_radians(
    f32 radians,
    f32 baseline_radians) noexcept {
  if (baseline_radians <= 0.0F) {
    return 100.0F;
  }
  return normalized_controls_settings({(radians / baseline_radians) * 100.0F})
      .mouse_sensitivity_percent;
}

io::UiConfig::ChatConfig chat_config_from_interface_settings(
    ui::InterfaceSettingsView settings) noexcept {
  settings = normalized_interface_settings(settings);
  io::UiConfig::ChatConfig config;
  config.max_history_messages = settings.chat_history_max;
  config.open_max_lines = settings.chat_open_lines;
  config.passive_max_lines = settings.chat_passive_lines;
  config.passive_visible_seconds = settings.chat_passive_visible_seconds;
  config.passive_fade_seconds = settings.chat_passive_fade_seconds;
  return config;
}

io::RenderConfig::SkyConfig sky_config_from_render_settings(
    ui::RenderSettingsView settings) noexcept {
  settings = normalized_render_settings(settings);
  io::RenderConfig::SkyConfig config;
  config.star_intensity = settings.star_intensity;
  config.cloud_intensity = settings.cloud_intensity;
  config.cirrus_amount = settings.cirrus_amount;
  config.cumulus_amount = settings.cumulus_amount;
  return config;
}

io::RenderConfig::BlockOutlineConfig block_outline_config_from_render_settings(
    ui::RenderSettingsView settings) noexcept {
  settings = normalized_render_settings(settings);
  io::RenderConfig::BlockOutlineConfig config;
  config.color_r = settings.outline_red;
  config.color_g = settings.outline_green;
  config.color_b = settings.outline_blue;
  config.thickness = settings.outline_thickness;
  return config;
}

player::PlayerTuning player_tuning_from_controls_settings(
    ui::ControlsSettingsView settings,
    player::PlayerTuning base_tuning,
    f32 baseline_radians) noexcept {
  settings = normalized_controls_settings(settings);
  base_tuning.mouse_sensitivity_radians =
      mouse_sensitivity_radians_from_percent(
          settings.mouse_sensitivity_percent,
          baseline_radians);
  return base_tuning;
}

platform::InputDeviceType platform_input_device_from_ui(
    ui::InputBindingDevice type) noexcept {
  switch (type) {
    case ui::InputBindingDevice::Keyboard:
      return platform::InputDeviceType::Keyboard;
    case ui::InputBindingDevice::MouseButton:
      return platform::InputDeviceType::MouseButton;
    case ui::InputBindingDevice::MouseWheel:
      return platform::InputDeviceType::MouseWheel;
  }
  return platform::InputDeviceType::Keyboard;
}

std::optional<platform::InputBinding> platform_binding_from_ui(
    const ui::InputBindingView& binding) {
  const platform::InputDeviceType device = platform_input_device_from_ui(binding.device);
  const std::optional<std::string> canonical =
      platform::canonical_input_code(device, binding.input_code);
  if (!canonical.has_value()) {
    return std::nullopt;
  }
  return platform::InputBinding{device, canonical.value(), binding.modifiers};
}

ui::InputBindingView input_binding_view_from_platform(
    const platform::InputBinding& binding) {
  return {
      true,
      ui_input_device_from_platform(binding.device_type),
      binding.input_code,
      arv::client::input_binding_label(binding),
      binding.modifiers};
}

std::string key_bind_conflict_key(const platform::InputBinding& binding) {
  return std::string{platform::input_device_type_name(binding.device_type)} + ":" +
         binding.input_code + ":" + std::to_string(binding.modifiers);
}

std::string key_bind_display_name(std::string_view action_name) {
  if (action_name == "Cancel") {
    return "Back / Inventory";
  }
  std::string display;
  display.reserve(action_name.size() + 4U);
  for (usize index = 0; index < action_name.size(); ++index) {
    const char ch = action_name[index];
    if (index > 0U && ch >= 'A' && ch <= 'Z') {
      display.push_back(' ');
    }
    display.push_back(ch);
  }
  return display;
}

std::vector<ui::KeyBindEntryView> key_bind_entries_from_input_bindings(
    const platform::InputBindings& input_bindings) {
  std::vector<ui::KeyBindEntryView> entries;
  HashMap<std::string, u32> binding_counts;
  for (usize index = 0; index < static_cast<usize>(platform::LogicalAction::Count); ++index) {
    const auto action = static_cast<platform::LogicalAction>(index);
    for (const platform::InputBinding& binding :
         platform::bindings_for_action(input_bindings, action)) {
      ++binding_counts[key_bind_conflict_key(binding)];
    }
  }

  entries.reserve(static_cast<usize>(platform::LogicalAction::Count));
  for (usize index = 0; index < static_cast<usize>(platform::LogicalAction::Count); ++index) {
    const auto action = static_cast<platform::LogicalAction>(index);
    ui::KeyBindEntryView entry;
    entry.action_name = std::string{platform::logical_action_name(action)};
    entry.display_name = key_bind_display_name(entry.action_name);
    const auto& bindings = platform::bindings_for_action(input_bindings, action);
    if (!bindings.empty()) {
      entry.primary.has_binding = true;
      entry.primary.binding = input_binding_view_from_platform(bindings[0]);
      entry.primary.conflict = binding_counts[key_bind_conflict_key(bindings[0])] > 1U;
    }
    if (bindings.size() > 1U) {
      entry.secondary.has_binding = true;
      entry.secondary.binding = input_binding_view_from_platform(bindings[1]);
      entry.secondary.conflict = binding_counts[key_bind_conflict_key(bindings[1])] > 1U;
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

bool apply_key_bind(
    platform::InputBindings& input_bindings,
    const ui::SetKeyBindCommand& command) {
  const std::optional<platform::LogicalAction> action =
      platform::logical_action_from_name(command.action_name);
  if (!action.has_value() || command.slot > 1U) {
    return false;
  }

  auto& action_bindings = platform::bindings_for_action(input_bindings, action.value());
  if (command.binding.has_value()) {
    const std::optional<platform::InputBinding> binding =
        platform_binding_from_ui(command.binding.value());
    if (!binding.has_value()) {
      return false;
    }
    if (action_bindings.size() <= command.slot) {
      const platform::InputBindings defaults = platform::default_input_bindings();
      const auto& default_bindings =
          platform::bindings_for_action(defaults, action.value());
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
    return false;
  }

  if (action_bindings.size() > 2U) {
    action_bindings.resize(2U);
  }
  return true;
}

bool reset_key_bind(
    platform::InputBindings& input_bindings,
    const ui::ResetKeyBindCommand& command) {
  const std::optional<platform::LogicalAction> action =
      platform::logical_action_from_name(command.action_name);
  if (!action.has_value()) {
    return false;
  }
  const platform::InputBindings defaults = platform::default_input_bindings();
  auto& action_bindings = platform::bindings_for_action(input_bindings, action.value());
  action_bindings = platform::bindings_for_action(defaults, action.value());
  if (action_bindings.size() > 2U) {
    action_bindings.resize(2U);
  }
  return true;
}

} // namespace arv::client
