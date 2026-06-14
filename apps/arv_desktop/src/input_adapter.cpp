#include "input_adapter.hpp"

namespace arv::desktop {
namespace {

[[nodiscard]] arv::core::TextEditCommand client_text_edit_command_from(
    platform::TextEditCommand command) noexcept {
  switch (command) {
    case platform::TextEditCommand::MoveLeft:
      return arv::core::TextEditCommand::MoveLeft;
    case platform::TextEditCommand::MoveRight:
      return arv::core::TextEditCommand::MoveRight;
    case platform::TextEditCommand::MoveUp:
      return arv::core::TextEditCommand::MoveUp;
    case platform::TextEditCommand::MoveDown:
      return arv::core::TextEditCommand::MoveDown;
    case platform::TextEditCommand::DeleteForward:
      return arv::core::TextEditCommand::DeleteForward;
    case platform::TextEditCommand::Copy:
      return arv::core::TextEditCommand::Copy;
    case platform::TextEditCommand::Paste:
      return arv::core::TextEditCommand::Paste;
    case platform::TextEditCommand::Cut:
      return arv::core::TextEditCommand::Cut;
    case platform::TextEditCommand::SelectAll:
      return arv::core::TextEditCommand::SelectAll;
  }
  return arv::core::TextEditCommand::MoveLeft;
}

[[nodiscard]] std::vector<arv::core::TextEditInput> client_text_edit_events_from(
    const std::vector<platform::TextEditEvent>& events) {
  std::vector<arv::core::TextEditInput> result;
  result.reserve(events.size());
  for (const platform::TextEditEvent& event : events) {
    result.push_back(
        {
            client_text_edit_command_from(event.command),
            event.extend_selection,
            event.clipboard_text,
        });
  }
  return result;
}

} // namespace

client::InputFrame to_client_input(const platform::InputSnapshot& input) {
  client::InputFrame frame;
  frame.move_forward = input.move_forward;
  frame.move_backward = input.move_backward;
  frame.move_left = input.move_left;
  frame.move_right = input.move_right;
  frame.jump_pressed = input.jump_pressed;
  frame.jump_held = input.jump_held;
  frame.sneak_held = input.sneak_held;
  frame.sprint_held = input.sprint_held;
  frame.mount_pressed = input.mount_pressed;
  frame.vehicle_roll_left_held = input.vehicle_roll_left_held;
  frame.vehicle_roll_right_held = input.vehicle_roll_right_held;
  frame.primary_pressed = input.primary_pressed;
  frame.primary_held = input.primary_held;
  frame.secondary_pressed = input.secondary_pressed;
  frame.secondary_held = input.secondary_held;
  frame.open_inventory_pressed = input.open_inventory_pressed;
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
  frame.text_edit_events = client_text_edit_events_from(input.text_edit_events);
  return frame;
}

ui::InputBindingDevice ui_device_from(platform::InputDeviceType type) noexcept {
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

std::string binding_label(const platform::InputBinding& binding) {
  return platform::input_binding_label(binding);
}

ui::CapturedInputView captured_input_from(const platform::RawInputEvent& event) {
  return {
      ui_device_from(event.binding.device_type),
      event.binding.input_code,
      binding_label(event.binding),
      event.binding.modifiers};
}

} // namespace arv::desktop
