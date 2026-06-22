#include "arv/client/client_ui_routing.hpp"

#include "arv/client/client_input_mapping.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/gameplay_ui_model.hpp"
#include "arv/client/ui_interactions.hpp"

#include "arv/player/inventory.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::client {
namespace {

constexpr i32 MaxConfigurableChunkDistance = 32;
constexpr f32 PlayerMenuSettingsViewportHeight = 344.0F;
constexpr f32 PlayerMenuSettingsRowHeight = 42.0F;
constexpr f32 PlayerMenuKeyBindRowHeight = 54.0F;

[[nodiscard]] bool shift_modifier_pressed(const platform::InputSnapshot& input) noexcept {
  return input.shift_modifier_held ||
         (input.raw_input.has_value() &&
          platform::has_input_modifier(
              input.raw_input->binding.modifiers,
              platform::InputModifier::Shift));
}

[[nodiscard]] ui::UITextEditCommand ui_text_edit_command_from_platform(
    platform::TextEditCommand command) noexcept {
  switch (command) {
    case platform::TextEditCommand::MoveLeft:
      return ui::UITextEditCommand::MoveLeft;
    case platform::TextEditCommand::MoveRight:
      return ui::UITextEditCommand::MoveRight;
    case platform::TextEditCommand::MoveUp:
      return ui::UITextEditCommand::MoveUp;
    case platform::TextEditCommand::MoveDown:
      return ui::UITextEditCommand::MoveDown;
    case platform::TextEditCommand::DeleteForward:
      return ui::UITextEditCommand::DeleteForward;
    case platform::TextEditCommand::Copy:
      return ui::UITextEditCommand::Copy;
    case platform::TextEditCommand::Paste:
      return ui::UITextEditCommand::Paste;
    case platform::TextEditCommand::Cut:
      return ui::UITextEditCommand::Cut;
    case platform::TextEditCommand::SelectAll:
      return ui::UITextEditCommand::SelectAll;
  }
  return ui::UITextEditCommand::MoveLeft;
}

[[nodiscard]] bool captured_mouse_hit_is_cancel_capture(
    ui::UiTree& ui_tree,
    const platform::InputSnapshot& input) {
  if (!input.raw_input.has_value() ||
      input.raw_input->binding.device_type != platform::InputDeviceType::MouseButton) {
    return false;
  }
  const std::optional<ui::UiNodeID> hit =
      ui_tree.hit_test({input.cursor_x, input.cursor_y});
  if (!hit.has_value()) {
    return false;
  }
  const ui::UiElement* element = ui_tree.element(hit.value());
  return element != nullptr && element->action == "key_bind.cancel_capture";
}

[[nodiscard]] bool apply_inventory_action(
    ClientRuntime& client,
    const std::optional<ui::UiAction>& action,
    player::InventoryClickButton button) {
  if (!action.has_value()) {
    return false;
  }
  if (const std::optional<usize> slot =
          inventory_slot_from_ui_action(action.value());
      slot.has_value()) {
    (void)client.request_inventory_slot_click(slot.value(), button);
    return true;
  }
  return false;
}

[[nodiscard]] bool apply_inventory_shift_transfer(
    ClientRuntime& client,
    const std::optional<ui::UiAction>& action) {
  if (!action.has_value()) {
    return false;
  }
  if (const std::optional<usize> slot =
          inventory_slot_from_ui_action(action.value());
      slot.has_value()) {
    (void)client.request_inventory_shift_transfer(slot.value());
    return true;
  }
  return false;
}

[[nodiscard]] bool apply_container_action(
    ClientRuntime& client,
    const std::optional<ui::UiAction>& action,
    player::InventoryClickButton button) {
  if (!action.has_value()) {
    return false;
  }
  if (const std::optional<ContainerSlotActionRef> slot =
          container_slot_from_ui_action(action.value());
      slot.has_value()) {
    (void)client.request_container_slot_click(
        slot->source,
        slot->slot,
        button);
    return true;
  }
  return false;
}

[[nodiscard]] bool consume_prefix(
    std::string_view value,
    std::string_view prefix,
    std::string_view& suffix) noexcept {
  if (!value.starts_with(prefix)) {
    return false;
  }
  suffix = value.substr(prefix.size());
  return true;
}

[[nodiscard]] bool consume_suffix(
    std::string_view value,
    std::string_view suffix,
    std::string_view& prefix) noexcept {
  if (!value.ends_with(suffix)) {
    return false;
  }
  prefix = value.substr(0U, value.size() - suffix.size());
  return true;
}

[[nodiscard]] bool parse_index_after_prefix(
    std::string_view value,
    std::string_view prefix,
    usize& index) noexcept {
  if (!value.starts_with(prefix)) {
    return false;
  }
  value = value.substr(prefix.size());
  const usize dot = value.find('.');
  if (dot != std::string_view::npos) {
    value = value.substr(0U, dot);
  }
  if (value.empty()) {
    return false;
  }
  usize parsed = 0U;
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed = parsed * 10U + static_cast<usize>(ch - '0');
  }
  index = parsed;
  return true;
}

[[nodiscard]] std::optional<PlayerMenuTab> player_menu_tab_from_id(
    std::string_view id) noexcept {
  if (id == "inventory") {
    return PlayerMenuTab::Inventory;
  }
  if (id == "crafting") {
    return PlayerMenuTab::Crafting;
  }
  if (id == "settings") {
    return PlayerMenuTab::Settings;
  }
  if (id == "exit") {
    return PlayerMenuTab::ExitGame;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PlayerMenuSettingsSection> settings_section_from_action(
    std::string_view action_id) noexcept {
  if (action_id == "settings.overview") {
    return PlayerMenuSettingsSection::Overview;
  }
  if (action_id == "settings.render") {
    return PlayerMenuSettingsSection::Render;
  }
  if (action_id == "settings.key_binds") {
    return PlayerMenuSettingsSection::KeyBinds;
  }
  if (action_id == "settings.controls") {
    return PlayerMenuSettingsSection::Controls;
  }
  if (action_id == "settings.interface") {
    return PlayerMenuSettingsSection::Interface;
  }
  return std::nullopt;
}

void normalize_render_chunk_distances(ui::RenderSettingsView& settings) noexcept {
  settings.load_distance =
      std::clamp(settings.load_distance, 1, MaxConfigurableChunkDistance);
  settings.simulation_distance =
      std::clamp(settings.simulation_distance, 0, settings.load_distance);
  settings.stream_distance =
      std::clamp(settings.stream_distance, 1, settings.load_distance);
  settings.view_distance =
      std::clamp(settings.view_distance, 1, settings.stream_distance);
}

[[nodiscard]] bool set_if_changed(i32& target, i32 next) noexcept {
  if (target == next) {
    return false;
  }
  target = next;
  return true;
}

[[nodiscard]] bool set_if_changed(u32& target, u32 next) noexcept {
  if (target == next) {
    return false;
  }
  target = next;
  return true;
}

[[nodiscard]] bool set_if_changed(f32& target, f32 next) noexcept {
  if (std::abs(target - next) <= 0.0001F) {
    return false;
  }
  target = next;
  return true;
}

[[nodiscard]] f32 stepped(f32 value, f32 delta, f32 min, f32 max) noexcept {
  return std::clamp(value + delta, min, max);
}

[[nodiscard]] bool apply_render_step(
    ui::RenderSettingsView& settings,
    std::string_view action_id) {
  std::string_view prefix;
  f32 direction = 0.0F;
  if (consume_suffix(action_id, ".inc", prefix)) {
    direction = 1.0F;
  } else if (consume_suffix(action_id, ".dec", prefix)) {
    direction = -1.0F;
  }
  if (direction == 0.0F) {
    return false;
  }

  bool changed = false;
  if (prefix == "render.fps_cap") {
    changed = set_if_changed(
        settings.fps_cap,
        static_cast<u32>(std::lround(stepped(
            static_cast<f32>(settings.fps_cap),
            direction * 5.0F,
            30.0F,
            240.0F))));
  } else if (prefix == "render.load_distance") {
    changed = set_if_changed(
        settings.load_distance,
        static_cast<i32>(std::lround(stepped(
            static_cast<f32>(settings.load_distance),
            direction,
            1.0F,
            static_cast<f32>(MaxConfigurableChunkDistance)))));
    normalize_render_chunk_distances(settings);
  } else if (prefix == "render.simulation_distance") {
    changed = set_if_changed(
        settings.simulation_distance,
        static_cast<i32>(std::lround(stepped(
            static_cast<f32>(settings.simulation_distance),
            direction,
            0.0F,
            static_cast<f32>(settings.load_distance)))));
    normalize_render_chunk_distances(settings);
  } else if (prefix == "render.view_distance") {
    changed = set_if_changed(
        settings.view_distance,
        static_cast<i32>(std::lround(stepped(
            static_cast<f32>(settings.view_distance),
            direction,
            1.0F,
            static_cast<f32>(std::max(1, settings.stream_distance))))));
    settings.stream_distance = std::max(settings.stream_distance, settings.view_distance);
    normalize_render_chunk_distances(settings);
  } else if (prefix == "render.stream_distance") {
    changed = set_if_changed(
        settings.stream_distance,
        static_cast<i32>(std::lround(stepped(
            static_cast<f32>(settings.stream_distance),
            direction,
            static_cast<f32>(std::max(1, settings.view_distance)),
            static_cast<f32>(settings.load_distance)))));
    settings.view_distance = std::min(settings.view_distance, settings.stream_distance);
    normalize_render_chunk_distances(settings);
  } else if (prefix == "render.mesh_upload_budget") {
    changed = set_if_changed(
        settings.mesh_upload_budget,
        static_cast<u32>(std::lround(stepped(
            static_cast<f32>(settings.mesh_upload_budget),
            direction,
            1.0F,
            32.0F))));
  } else if (prefix == "render.star_intensity") {
    changed = set_if_changed(settings.star_intensity, stepped(settings.star_intensity, direction * 0.05F, 0.0F, 3.0F));
  } else if (prefix == "render.cloud_intensity") {
    changed = set_if_changed(settings.cloud_intensity, stepped(settings.cloud_intensity, direction * 0.05F, 0.0F, 3.0F));
  } else if (prefix == "render.cirrus") {
    changed = set_if_changed(settings.cirrus_amount, stepped(settings.cirrus_amount, direction * 0.05F, 0.0F, 1.0F));
  } else if (prefix == "render.cumulus") {
    changed = set_if_changed(settings.cumulus_amount, stepped(settings.cumulus_amount, direction * 0.05F, 0.0F, 1.0F));
  } else if (prefix == "render.outline_red") {
    changed = set_if_changed(settings.outline_red, stepped(settings.outline_red, direction * 0.05F, 0.0F, 1.0F));
  } else if (prefix == "render.outline_green") {
    changed = set_if_changed(settings.outline_green, stepped(settings.outline_green, direction * 0.05F, 0.0F, 1.0F));
  } else if (prefix == "render.outline_blue") {
    changed = set_if_changed(settings.outline_blue, stepped(settings.outline_blue, direction * 0.05F, 0.0F, 1.0F));
  } else if (prefix == "render.outline_thickness") {
    changed = set_if_changed(settings.outline_thickness, stepped(settings.outline_thickness, direction * 0.05F, 0.1F, 2.0F));
  }
  return changed;
}

[[nodiscard]] bool apply_controls_step(
    ui::ControlsSettingsView& settings,
    std::string_view action_id) {
  std::string_view prefix;
  f32 direction = 0.0F;
  if (consume_suffix(action_id, ".inc", prefix)) {
    direction = 1.0F;
  } else if (consume_suffix(action_id, ".dec", prefix)) {
    direction = -1.0F;
  }
  if (prefix != "controls.mouse_sensitivity" || direction == 0.0F) {
    return false;
  }
  return set_if_changed(
      settings.mouse_sensitivity_percent,
      stepped(settings.mouse_sensitivity_percent, direction * 5.0F, 10.0F, 300.0F));
}

[[nodiscard]] bool apply_interface_step(
    ui::InterfaceSettingsView& settings,
    std::string_view action_id) {
  std::string_view prefix;
  f32 direction = 0.0F;
  if (consume_suffix(action_id, ".inc", prefix)) {
    direction = 1.0F;
  } else if (consume_suffix(action_id, ".dec", prefix)) {
    direction = -1.0F;
  }
  if (direction == 0.0F) {
    return false;
  }
  if (prefix == "interface.chat_history") {
    return set_if_changed(
        settings.chat_history_max,
        static_cast<u32>(std::lround(stepped(
            static_cast<f32>(settings.chat_history_max),
            direction * 100.0F,
            100.0F,
            5000.0F))));
  }
  if (prefix == "interface.open_lines") {
    return set_if_changed(
        settings.chat_open_lines,
        static_cast<u32>(std::lround(stepped(
            static_cast<f32>(settings.chat_open_lines),
            direction,
            1.0F,
            16.0F))));
  }
  if (prefix == "interface.passive_lines") {
    return set_if_changed(
        settings.chat_passive_lines,
        static_cast<u32>(std::lround(stepped(
            static_cast<f32>(settings.chat_passive_lines),
            direction,
            0.0F,
            16.0F))));
  }
  if (prefix == "interface.visible_seconds") {
    return set_if_changed(
        settings.chat_passive_visible_seconds,
        stepped(settings.chat_passive_visible_seconds, direction * 0.5F, 0.0F, 30.0F));
  }
  if (prefix == "interface.fade_seconds") {
    return set_if_changed(
        settings.chat_passive_fade_seconds,
        stepped(settings.chat_passive_fade_seconds, direction * 0.5F, 0.0F, 30.0F));
  }
  return false;
}

[[nodiscard]] bool submit_player_menu_settings_action(
    const ui::UiAction& action,
    ui::UICommandSink* command_sink,
    const ui::UIScreenDataSource* data_source) {
  if (command_sink == nullptr || data_source == nullptr) {
    return false;
  }
  if (action.action_id == "render.frame_sync.off" ||
      action.action_id == "render.frame_sync.vsync" ||
      action.action_id == "render.frame_sync.fps_cap" ||
      action.action_id == "render.local_player_body" ||
      action.action_id.starts_with("render.")) {
    ui::RenderSettingsView settings = data_source->render_settings();
    bool changed = true;
    if (action.action_id == "render.frame_sync.off") {
      settings.frame_sync = ui::SettingsFrameSyncMode::Off;
    } else if (action.action_id == "render.frame_sync.vsync") {
      settings.frame_sync = ui::SettingsFrameSyncMode::VSync;
    } else if (action.action_id == "render.frame_sync.fps_cap") {
      settings.frame_sync = ui::SettingsFrameSyncMode::FpsCap;
    } else if (action.action_id == "render.local_player_body") {
      settings.show_local_player_body = !settings.show_local_player_body;
    } else {
      changed = apply_render_step(settings, action.action_id);
    }
    if (changed) {
      command_sink->submit(ui::SetRenderSettingsCommand{settings});
    }
    return changed;
  }
  if (action.action_id.starts_with("controls.")) {
    ui::ControlsSettingsView settings = data_source->controls_settings();
    if (!apply_controls_step(settings, action.action_id)) {
      return false;
    }
    command_sink->submit(ui::SetControlsSettingsCommand{settings});
    return true;
  }
  if (action.action_id.starts_with("interface.")) {
    ui::InterfaceSettingsView settings = data_source->interface_settings();
    if (!apply_interface_step(settings, action.action_id)) {
      return false;
    }
    command_sink->submit(ui::SetInterfaceSettingsCommand{settings});
    return true;
  }
  return false;
}

[[nodiscard]] bool apply_player_menu_key_bind_action(
    ClientRuntime& client,
    const ui::UiAction& action,
    ui::UICommandSink* command_sink,
    const ui::UIScreenDataSource* data_source) {
  if (action.action_id == "key_bind.cancel_capture") {
    client.clear_player_menu_key_bind_capture();
    return true;
  }

  usize index = 0U;
  if (!parse_index_after_prefix(action.element_id, "key_binds.row.", index) ||
      data_source == nullptr) {
    return false;
  }
  const std::vector<ui::KeyBindEntryView> entries = data_source->key_bind_entries();
  if (index >= entries.size()) {
    return false;
  }
  const std::string& action_name = entries[index].action_name;
  if (action.action_id == "key_bind.capture_primary") {
    client.set_player_menu_key_bind_capture({action_name, 0U});
    return true;
  }
  if (action.action_id == "key_bind.capture_secondary") {
    client.set_player_menu_key_bind_capture({action_name, 1U});
    return true;
  }
  if (command_sink == nullptr) {
    return false;
  }
  if (action.action_id == "key_bind.clear_secondary") {
    command_sink->submit(ui::SetKeyBindCommand{action_name, 1U, std::nullopt});
    client.clear_player_menu_key_bind_capture();
    return true;
  }
  if (action.action_id == "key_bind.reset") {
    command_sink->submit(ui::ResetKeyBindCommand{action_name});
    client.clear_player_menu_key_bind_capture();
    return true;
  }
  return false;
}

[[nodiscard]] usize player_menu_settings_row_count(
    PlayerMenuSettingsSection section,
    const ui::UIScreenDataSource* data_source) {
  switch (section) {
    case PlayerMenuSettingsSection::Render:
      return 16U;
    case PlayerMenuSettingsSection::KeyBinds:
      return data_source == nullptr ? 0U : data_source->key_bind_entries().size();
    case PlayerMenuSettingsSection::Controls:
      return 1U;
    case PlayerMenuSettingsSection::Interface:
      return 5U;
    case PlayerMenuSettingsSection::Overview:
      return 0U;
  }
  return 0U;
}

[[nodiscard]] f32 player_menu_settings_row_height(PlayerMenuSettingsSection section) noexcept {
  return section == PlayerMenuSettingsSection::KeyBinds
             ? PlayerMenuKeyBindRowHeight
             : PlayerMenuSettingsRowHeight;
}

[[nodiscard]] bool apply_player_menu_action(
    ClientRuntime& client,
    const std::optional<ui::UiAction>& action,
    ui::UICommandSink* command_sink,
    const ui::UIScreenDataSource* data_source) {
  if (!action.has_value()) {
    return false;
  }
  std::string_view suffix;
  if (consume_prefix(action->action_id, "player_menu.tab:", suffix)) {
    if (const std::optional<PlayerMenuTab> tab = player_menu_tab_from_id(suffix);
        tab.has_value()) {
      client.set_player_menu_tab(tab.value());
      return true;
    }
  }
  if (consume_prefix(action->action_id, "crafting.category:", suffix)) {
    client.set_player_menu_tab(PlayerMenuTab::Crafting);
    client.select_crafting_category(std::string{suffix});
    return true;
  }
  if (consume_prefix(action->action_id, "crafting.recipe:", suffix)) {
    client.set_player_menu_tab(PlayerMenuTab::Crafting);
    client.select_crafting_recipe(std::string{suffix});
    return true;
  }
  if (consume_prefix(action->action_id, "crafting.craft:", suffix)) {
    if (!suffix.empty()) {
      (void)client.request_craft_recipe(std::string{suffix}, 1U);
      return true;
    }
  }
  if (action->action_id == "crafting.search.clear") {
    client.clear_player_menu_search();
    return true;
  }
  if (action->action_id == "crafting.search.focus") {
    client.set_player_menu_tab(PlayerMenuTab::Crafting);
    return true;
  }
  if (const std::optional<PlayerMenuSettingsSection> section =
          settings_section_from_action(action->action_id);
      section.has_value()) {
    client.set_player_menu_tab(PlayerMenuTab::Settings);
    client.set_player_menu_settings_section(section.value());
    return true;
  }
  if (action->action_id == "player_menu.exit.main_menu") {
    if (command_sink != nullptr) {
      command_sink->submit(ui::BackToMainMenuCommand{});
      return true;
    }
    return false;
  }
  if (action->action_id == "player_menu.exit.quit") {
    if (command_sink != nullptr) {
      command_sink->submit(ui::QuitGameCommand{});
      return true;
    }
    return false;
  }
  if (action->action_id.starts_with("key_bind.")) {
    return apply_player_menu_key_bind_action(client, action.value(), command_sink, data_source);
  }
  if (submit_player_menu_settings_action(action.value(), command_sink, data_source)) {
    return true;
  }
  return false;
}

} // namespace

GameplayPauseMenuRoutingResult route_gameplay_pause_menu_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    bool pause_menu_pressed,
    bool gameplay_stack_active,
    bool has_active_gameplay_session,
    bool has_client_overlay,
    const GameplayPauseMenuRoutingCallbacks& callbacks) {
  GameplayPauseMenuRoutingResult result{};
  if (!pause_menu_pressed) {
    return result;
  }
  result.consumed_pause_menu = true;
  if (!gameplay_stack_active) {
    return result;
  }

  if (screen_manager.empty() && callbacks.reset_gameplay_screen_stack) {
    callbacks.reset_gameplay_screen_stack();
    result.reset_gameplay_screen_stack = true;
  }

  const ui::UIScreen* current_screen = screen_manager.current();
  const bool on_gameplay_root =
      current_screen != nullptr &&
      current_screen->type() == ui::MenuScreenType::Gameplay;
  if (current_screen != nullptr && !on_gameplay_root) {
    result.routed_cancel = true;
    result.consumed_text_cancel = true;
    result.handled_cancel = screen_manager.handle_cancel(ui_context);
  } else if (on_gameplay_root && has_active_gameplay_session &&
             !has_client_overlay) {
    result.routed_cancel = true;
    result.consumed_text_cancel = true;
    result.handled_cancel = screen_manager.handle_cancel(ui_context);
  }
  return result;
}

bool route_inventory_mouse_input(
    ui::UiTree& ui_tree,
    ClientRuntime& client,
    const platform::InputSnapshot& platform_input,
    bool has_carried_stack,
    ui::UICommandSink* command_sink,
    const ui::UIScreenDataSource* data_source) {
  bool changed = false;
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree.route_mouse_move(cursor);
  if (client.inventory_open() &&
      client.player_menu_tab() == PlayerMenuTab::Settings &&
      platform_input.mouse_wheel_y != 0.0F) {
    const PlayerMenuSettingsSection section = client.player_menu_settings_section();
    client.scroll_player_menu_settings(
        platform_input.mouse_wheel_y,
        player_menu_settings_row_count(section, data_source),
        player_menu_settings_row_height(section),
        PlayerMenuSettingsViewportHeight);
    changed = true;
  }
  if (platform_input.mouse_left_pressed) {
    const std::optional<ui::UiNodeID> hit = ui_tree.hit_test(cursor);
    const std::optional<ui::UiAction> action =
        ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Left);
    if (apply_player_menu_action(client, action, command_sink, data_source)) {
      return true;
    }
    if (client.container_open() && shift_modifier_pressed(platform_input)) {
      std::optional<ContainerSlotActionRef> slot;
      if (action.has_value()) {
        slot = container_slot_from_ui_action(action.value());
      }
      if (slot.has_value()) {
        (void)client.request_container_shift_transfer(slot->source, slot->slot);
        changed = true;
      }
    } else if (!client.container_open() && shift_modifier_pressed(platform_input)) {
      changed = apply_inventory_shift_transfer(client, action);
    }
    changed =
        changed ||
        (client.container_open()
             ? apply_container_action(client, action, player::InventoryClickButton::Primary)
             : apply_inventory_action(client, action, player::InventoryClickButton::Primary));
    if (!changed && !hit.has_value() && has_carried_stack) {
      (void)client.request_inventory_drop_cursor(false);
      changed = true;
    }
  }
  if (platform_input.mouse_right_pressed) {
    const std::optional<ui::UiNodeID> hit = ui_tree.hit_test(cursor);
    const std::optional<ui::UiAction> action =
        ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Right);
    changed =
        (client.container_open()
             ? apply_container_action(client, action, player::InventoryClickButton::Secondary)
             : apply_inventory_action(client, action, player::InventoryClickButton::Secondary)) ||
        changed;
    if (!changed && !hit.has_value() && has_carried_stack) {
      (void)client.request_inventory_drop_cursor(true);
      changed = true;
    }
  }
  if (platform_input.mouse_left_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Right);
  }
  return changed;
}

bool route_player_menu_captured_input(
    ClientRuntime& client,
    ui::UICommandSink* command_sink,
    const std::optional<ui::CapturedInputView>& captured_input) {
  if (command_sink == nullptr ||
      !captured_input.has_value() ||
      !client.inventory_open() ||
      client.player_menu_tab() != PlayerMenuTab::Settings) {
    return false;
  }
  const std::optional<PlayerMenuKeyBindCapture>& capture =
      client.player_menu_key_bind_capture();
  if (!capture.has_value()) {
    return false;
  }

  ui::InputBindingView binding;
  binding.valid = true;
  binding.device = captured_input->device;
  binding.input_code = captured_input->input_code;
  binding.label = captured_input->label.empty()
                      ? captured_input->input_code
                      : captured_input->label;
  binding.modifiers = captured_input->modifiers;
  command_sink->submit(
      ui::SetKeyBindCommand{capture->action_name, capture->slot, std::move(binding)});
  client.clear_player_menu_key_bind_capture();
  return true;
}

bool route_player_menu_captured_input(
    ui::UiTree& ui_tree,
    ClientRuntime& client,
    ui::UICommandSink* command_sink,
    const platform::InputSnapshot& platform_input) {
  if (!platform_input.raw_input.has_value()) {
    return false;
  }
  if (platform_input.raw_input->binding.device_type ==
      platform::InputDeviceType::MouseButton) {
    const std::optional<ui::UiNodeID> hit =
        ui_tree.hit_test({platform_input.cursor_x, platform_input.cursor_y});
    if (hit.has_value()) {
      const ui::UiElement* element = ui_tree.element(hit.value());
      if (element != nullptr && element->action == "key_bind.cancel_capture") {
        return false;
      }
    }
  }
  return route_player_menu_captured_input(
      client,
      command_sink,
      captured_input_from_platform(platform_input.raw_input.value()));
}

bool route_screen_mouse_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const platform::InputSnapshot& platform_input) {
  bool handled = false;
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_context.tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    const std::optional<ui::UiAction> action =
        ui_context.tree.route_mouse_down(cursor, ui::UiMouseButton::Left);
    if (action.has_value()) {
      handled = screen_manager.route_action(action.value(), ui_context) || handled;
    }
  }
  if (platform_input.mouse_left_released) {
    (void)ui_context.tree.route_mouse_up(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_pressed) {
    (void)ui_context.tree.route_mouse_down(cursor, ui::UiMouseButton::Right);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_context.tree.route_mouse_up(cursor, ui::UiMouseButton::Right);
  }
  return handled;
}

bool route_blocking_screen_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const platform::InputSnapshot& platform_input) {
  bool handled =
      route_screen_mouse_input(screen_manager, ui_context, platform_input);
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  handled =
      screen_manager.handle_pointer(
          cursor,
          {platform_input.mouse_delta_x, platform_input.mouse_delta_y},
          platform_input.mouse_left_pressed,
          platform_input.mouse_left_released,
          platform_input.mouse_left_held,
          ui_context) ||
      handled;
  if (platform_input.mouse_wheel_y != 0.0F) {
    handled =
        screen_manager.handle_scroll(
            cursor,
            platform_input.mouse_wheel_y,
            ui_context) ||
        handled;
  }
  for (const platform::TextEditEvent& event : platform_input.text_edit_events) {
    handled =
        screen_manager.handle_text_edit(
            {ui_text_edit_command_from_platform(event.command),
             event.extend_selection},
            ui_context) ||
        handled;
  }
  if (!platform_input.text_input.empty()) {
    handled =
        screen_manager.handle_text_input(platform_input.text_input, ui_context) ||
        handled;
  }
  if (platform_input.text_backspace_pressed) {
    handled = screen_manager.handle_backspace(ui_context) || handled;
  }
  if (platform_input.text_submit_pressed) {
    handled = screen_manager.handle_submit(ui_context) || handled;
  }
  return handled;
}

bool route_captured_screen_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const platform::InputSnapshot& platform_input) {
  if (!screen_manager.has_active_screen() ||
      !platform_input.raw_input.has_value() ||
      captured_mouse_hit_is_cancel_capture(ui_context.tree, platform_input)) {
    return false;
  }
  return screen_manager.handle_captured_input(
      captured_input_from_platform(platform_input.raw_input.value()),
      ui_context);
}

bool route_captured_screen_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const std::optional<ui::CapturedInputView>& captured_input) {
  if (!screen_manager.has_active_screen() || !captured_input.has_value()) {
    return false;
  }
  return screen_manager.handle_captured_input(captured_input.value(), ui_context);
}

bool route_chat_mouse_input(
    ui::UiTree& ui_tree,
    ClientRuntime& client,
    const platform::InputSnapshot& platform_input,
    std::span<const ui::ChatLineView> chat_lines,
    usize chat_history_size,
    usize open_max_lines,
    usize& chat_history_scroll_offset,
    const ChatMouseRoutingCallbacks& callbacks) {
  bool handled = false;
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree.route_mouse_move(cursor);
  if (platform_input.mouse_wheel_y != 0.0F) {
    const usize max_offset = gameplay_chat_history_max_scroll_offset(
        chat_history_size,
        std::max<usize>(1U, open_max_lines));
    chat_history_scroll_offset = std::min(chat_history_scroll_offset, max_offset);
    const usize steps =
        gameplay_chat_scroll_steps(platform_input.mouse_wheel_y);
    if (steps > 0U) {
      if (platform_input.mouse_wheel_y > 0.0F) {
        const usize remaining = max_offset > chat_history_scroll_offset
            ? max_offset - chat_history_scroll_offset
            : 0U;
        chat_history_scroll_offset += std::min(steps, remaining);
      } else {
        chat_history_scroll_offset -=
            std::min(steps, chat_history_scroll_offset);
      }
      handled = true;
    }
  }

  if (platform_input.mouse_left_pressed) {
    const std::optional<ui::UiAction> action =
        ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Left);
    if (action.has_value() && action->action_id == "chat_url_cancel") {
      client.cancel_chat_url_confirmation();
      handled = true;
    } else if (action.has_value() && action->action_id == "chat_url_confirm") {
      const std::optional<std::string> pending =
          client.pending_chat_url_confirmation();
      if (pending.has_value() &&
          safe_chat_url_scheme(pending.value()) &&
          callbacks.open_url &&
          callbacks.open_url(pending.value())) {
        client.cancel_chat_url_confirmation();
      } else {
        client.acknowledge_chat_url_open_unavailable();
      }
      handled = true;
    } else if (action.has_value()) {
      const std::optional<std::pair<usize, usize>> run_id =
          chat_run_from_ui_action(action.value());
      if (run_id.has_value() &&
          run_id->first < chat_lines.size() &&
          run_id->second < chat_lines[run_id->first].runs.size()) {
        const text::RichTextRun& run =
            chat_lines[run_id->first].runs[run_id->second];
        if (run.click_event.has_value()) {
          const text::ClickEvent& click = run.click_event.value();
          switch (click.action) {
            case text::ClickAction::SuggestCommand:
              client.suggest_chat_command(click.value);
              handled = true;
              break;
            case text::ClickAction::OpenUrl:
              if (safe_chat_url_scheme(click.value)) {
                client.request_chat_url_confirmation(click.value);
              } else {
                client.add_chat_message(callbacks.blocked_unsafe_url_message);
              }
              handled = true;
              break;
            case text::ClickAction::RunCommand:
              if (callbacks.run_command) {
                callbacks.run_command(click.value);
              }
              handled = true;
              break;
            case text::ClickAction::CopyToClipboard:
              if (!callbacks.copy_to_clipboard ||
                  !callbacks.copy_to_clipboard(click.value)) {
                client.add_chat_message(callbacks.copy_unavailable_message);
              }
              handled = true;
              break;
          }
        }
      }
    }
  }
  if (platform_input.mouse_left_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_pressed) {
    (void)ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Right);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Right);
  }
  return handled;
}

void route_ui_mouse_focus(
    ui::UiTree& ui_tree,
    const platform::InputSnapshot& platform_input) {
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    (void)ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_pressed) {
    (void)ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Right);
  }
  if (platform_input.mouse_left_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Right);
  }
}

} // namespace arv::client
