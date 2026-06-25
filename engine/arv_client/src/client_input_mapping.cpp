#include "arv/client/client_input_mapping.hpp"

namespace arv::client {
namespace {

void clear_platform_input_action_edges(platform::InputSnapshot& input) noexcept {
  input.mouse_wheel_y = 0.0F;
  input.open_inventory_pressed = false;
  input.open_chat_pressed = false;
  input.text_backspace_pressed = false;
  input.text_submit_pressed = false;
  input.text_cancel_pressed = false;
  input.debug_menu_toggle_pressed = false;
  input.hotbar_select.reset();
  input.hotbar_scroll_steps = 0;
  input.raw_input.reset();
}

} // namespace

core::TextEditCommand text_edit_command_from_platform(
    platform::TextEditCommand command) noexcept {
  switch (command) {
    case platform::TextEditCommand::MoveLeft:
      return core::TextEditCommand::MoveLeft;
    case platform::TextEditCommand::MoveRight:
      return core::TextEditCommand::MoveRight;
    case platform::TextEditCommand::MoveUp:
      return core::TextEditCommand::MoveUp;
    case platform::TextEditCommand::MoveDown:
      return core::TextEditCommand::MoveDown;
    case platform::TextEditCommand::DeleteForward:
      return core::TextEditCommand::DeleteForward;
    case platform::TextEditCommand::Copy:
      return core::TextEditCommand::Copy;
    case platform::TextEditCommand::Paste:
      return core::TextEditCommand::Paste;
    case platform::TextEditCommand::Cut:
      return core::TextEditCommand::Cut;
    case platform::TextEditCommand::SelectAll:
      return core::TextEditCommand::SelectAll;
  }
  return core::TextEditCommand::MoveLeft;
}

std::vector<core::TextEditInput> text_edit_events_from_platform(
    const std::vector<platform::TextEditEvent>& events) {
  std::vector<core::TextEditInput> result;
  result.reserve(events.size());
  for (const platform::TextEditEvent& event : events) {
    result.push_back(
        {
            text_edit_command_from_platform(event.command),
            event.extend_selection,
            event.clipboard_text,
        });
  }
  return result;
}

InputFrame input_frame_from_platform_snapshot(const platform::InputSnapshot& input) {
  InputFrame frame;
  frame.move_forward = input.move_forward;
  frame.move_backward = input.move_backward;
  frame.move_left = input.move_left;
  frame.move_right = input.move_right;
  frame.jump_pressed = input.jump_pressed;
  frame.jump_held = input.jump_held;
  frame.sneak_held = input.sneak_held;
  frame.sprint_held = input.sprint_held;
  frame.toggle_fly_pressed = input.toggle_fly_pressed;
  frame.mount_pressed = input.mount_pressed;
  frame.vehicle_roll_left_held = input.vehicle_roll_left_held;
  frame.vehicle_roll_right_held = input.vehicle_roll_right_held;
  frame.primary_pressed = input.primary_pressed;
  frame.primary_held = input.primary_held;
  frame.secondary_pressed = input.secondary_pressed;
  frame.secondary_held = input.secondary_held;
  frame.drop_item_pressed = input.drop_item_pressed;
  frame.drop_item_held = input.drop_item_held;
  frame.open_inventory_pressed = input.open_inventory_pressed || input.text_cancel_pressed;
  frame.open_chat_pressed = input.open_chat_pressed;
  frame.tab_menu_held = input.tab_menu_held;
  frame.hotbar_select = input.hotbar_select;
  frame.hotbar_scroll_steps = input.hotbar_scroll_steps;
  frame.mouse_delta_x = input.mouse_delta_x;
  frame.mouse_delta_y = input.mouse_delta_y;
  frame.text_input = input.text_input;
  frame.text_backspace_pressed = input.text_backspace_pressed;
  frame.text_submit_pressed = input.text_submit_pressed;
  frame.text_cancel_pressed = input.text_cancel_pressed;
  frame.text_edit_events = text_edit_events_from_platform(input.text_edit_events);
  return frame;
}

void clear_platform_input_frame_edges(platform::InputSnapshot& input) noexcept {
  input.mouse_delta_x = 0.0F;
  input.mouse_delta_y = 0.0F;
  input.mouse_left_pressed = false;
  input.mouse_right_pressed = false;
  input.mouse_left_released = false;
  input.mouse_right_released = false;
  input.primary_pressed = false;
  input.secondary_pressed = false;
  input.jump_pressed = false;
  input.mount_pressed = false;
  input.drop_item_pressed = false;
  input.toggle_fly_pressed = false;
  input.text_input.clear();
  input.text_edit_events.clear();
  clear_platform_input_action_edges(input);
}

void clear_platform_input_captured_binding_edges(
    platform::InputSnapshot& input) noexcept {
  clear_platform_input_action_edges(input);
}

ui::InputBindingDevice ui_input_device_from_platform(
    platform::InputDeviceType type) noexcept {
  switch (type) {
    case platform::InputDeviceType::Keyboard:
      return ui::InputBindingDevice::Keyboard;
    case platform::InputDeviceType::MouseButton:
      return ui::InputBindingDevice::MouseButton;
    case platform::InputDeviceType::MouseWheel:
      return ui::InputBindingDevice::MouseWheel;
  }
  return ui::InputBindingDevice::Keyboard;
}

std::string input_binding_label(const platform::InputBinding& binding) {
  return platform::input_binding_label(binding);
}

std::string first_action_binding_label(
    const platform::InputBindings& input_bindings,
    platform::LogicalAction action) {
  const platform::ActionBindingList& bindings =
      platform::bindings_for_action(input_bindings, action);
  return bindings.empty()
             ? std::string{}
             : arv::client::input_binding_label(bindings.front());
}

ui::CapturedInputView captured_input_from_platform(
    const platform::RawInputEvent& event) {
  return {
      ui_input_device_from_platform(event.binding.device_type),
      event.binding.input_code,
      arv::client::input_binding_label(event.binding),
      event.binding.modifiers};
}

} // namespace arv::client
