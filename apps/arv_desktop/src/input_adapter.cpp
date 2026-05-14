#include "input_adapter.hpp"

namespace arv::desktop {

client::InputFrame to_client_input(const platform::InputSnapshot& input) {
  return {
      input.move_forward,
      input.move_backward,
      input.move_left,
      input.move_right,
      input.jump_pressed,
      input.jump_held,
      input.sneak_held,
      input.sprint_held,
      input.primary_pressed,
      input.primary_held,
      input.secondary_pressed,
      input.secondary_held,
      input.toggle_fly_pressed,
      input.open_inventory_pressed,
      input.open_chat_pressed,
      input.tab_menu_held,
      input.hotbar_select,
      input.hotbar_scroll_steps,
      input.mouse_delta_x,
      input.mouse_delta_y,
      input.text_input,
      input.text_backspace_pressed,
      input.text_submit_pressed,
      input.text_cancel_pressed};
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
  std::string label;
  if (platform::has_input_modifier(binding.modifiers, platform::InputModifier::Control)) {
    label += "Ctrl + ";
  }
  if (platform::has_input_modifier(binding.modifiers, platform::InputModifier::Shift)) {
    label += "Shift + ";
  }
  if (platform::has_input_modifier(binding.modifiers, platform::InputModifier::Alt)) {
    label += "Alt + ";
  }
  switch (binding.device_type) {
    case platform::InputDeviceType::Keyboard:
      label += binding.input_code;
      break;
    case platform::InputDeviceType::MouseButton:
      label += "Mouse " + binding.input_code;
      break;
    case platform::InputDeviceType::MouseWheel:
      label += "Wheel " + binding.input_code;
      break;
  }
  return label;
}

ui::CapturedInputView captured_input_from(const platform::RawInputEvent& event) {
  return {
      ui_device_from(event.binding.device_type),
      event.binding.input_code,
      binding_label(event.binding),
      event.binding.modifiers};
}

} // namespace arv::desktop
