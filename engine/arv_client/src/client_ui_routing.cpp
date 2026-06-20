#include "arv/client/client_ui_routing.hpp"

#include "arv/client/client_input_mapping.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/gameplay_ui_model.hpp"
#include "arv/client/ui_interactions.hpp"

#include "arv/player/inventory.hpp"

#include <algorithm>
#include <optional>

namespace arv::client {
namespace {

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
    bool has_carried_stack) {
  bool changed = false;
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    const std::optional<ui::UiNodeID> hit = ui_tree.hit_test(cursor);
    const std::optional<ui::UiAction> action =
        ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Left);
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
