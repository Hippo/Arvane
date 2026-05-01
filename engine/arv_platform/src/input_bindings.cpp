#include "arv/platform/input_bindings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace arv::platform {
namespace {

using ActionNameEntry = std::pair<LogicalAction, std::string_view>;

constexpr std::array<ActionNameEntry, static_cast<usize>(LogicalAction::Count)> kActionNames{{
    {LogicalAction::MoveForward, "MoveForward"},
    {LogicalAction::MoveBackward, "MoveBackward"},
    {LogicalAction::MoveLeft, "MoveLeft"},
    {LogicalAction::MoveRight, "MoveRight"},
    {LogicalAction::Jump, "Jump"},
    {LogicalAction::Sneak, "Sneak"},
    {LogicalAction::Sprint, "Sprint"},
    {LogicalAction::PrimaryInteract, "PrimaryInteract"},
    {LogicalAction::SecondaryInteract, "SecondaryInteract"},
    {LogicalAction::Hotbar1, "Hotbar1"},
    {LogicalAction::Hotbar2, "Hotbar2"},
    {LogicalAction::Hotbar3, "Hotbar3"},
    {LogicalAction::Hotbar4, "Hotbar4"},
    {LogicalAction::Hotbar5, "Hotbar5"},
    {LogicalAction::Hotbar6, "Hotbar6"},
    {LogicalAction::Hotbar7, "Hotbar7"},
    {LogicalAction::Hotbar8, "Hotbar8"},
    {LogicalAction::Hotbar9, "Hotbar9"},
    {LogicalAction::NextHotbarSlot, "NextHotbarSlot"},
    {LogicalAction::PrevHotbarSlot, "PrevHotbarSlot"},
    {LogicalAction::ToggleFly, "ToggleFly"},
    {LogicalAction::OpenInventory, "OpenInventory"},
    {LogicalAction::OpenChat, "OpenChat"},
    {LogicalAction::ShowTabMenu, "ShowTabMenu"},
    {LogicalAction::TogglePauseMenu, "TogglePauseMenu"},
    {LogicalAction::ToggleDebugMenu, "ToggleDebugMenu"},
}};

constexpr std::array<std::string_view, 50> kKeyboardCodes{{"A", "B", "C", "D", "E", "F", "G",
                                                           "H", "I", "J", "K", "L", "M", "N",
                                                           "O", "P", "Q", "R", "S", "T", "U",
                                                           "V", "W", "X", "Y", "Z", "0", "1",
                                                           "2", "3", "4", "5", "6", "7", "8",
                                                           "9", "Space", "Tab", "Enter", "Escape", "Backspace", "Minus",
                                                           "Equal", "LeftBracket", "RightBracket", "Semicolon", "Apostrophe", "Comma", "Period",
                                                           "Grave"}};

constexpr std::array<std::pair<std::string_view, std::string_view>, 12> kKeyboardAliases{{
    {"LShift", "LShift"},
    {"LeftShift", "LShift"},
    {"RShift", "RShift"},
    {"RightShift", "RShift"},
    {"LCtrl", "LCtrl"},
    {"LeftCtrl", "LCtrl"},
    {"LControl", "LCtrl"},
    {"RCtrl", "RCtrl"},
    {"RightCtrl", "RCtrl"},
    {"RControl", "RCtrl"},
    {"LAlt", "LAlt"},
    {"RAlt", "RAlt"},
}};

constexpr std::array<std::string_view, 7> kExtendedKeyboardCodes{{
    "LShift",
    "RShift",
    "LCtrl",
    "RCtrl",
    "LAlt",
    "RAlt",
    "Slash",
}};

constexpr std::array<std::string_view, 3> kMouseButtons{{"Left", "Right", "Middle"}};
constexpr std::array<std::string_view, 2> kMouseWheelDirections{{"Up", "Down"}};

[[nodiscard]] bool equals_ignore_case(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (usize index = 0; index < lhs.size(); ++index) {
    const char left = static_cast<char>(std::tolower(static_cast<unsigned char>(lhs[index])));
    const char right = static_cast<char>(std::tolower(static_cast<unsigned char>(rhs[index])));
    if (left != right) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::string> canonical_keyboard_code(std::string_view code) noexcept {
  if (code.size() == 1 && std::isalpha(static_cast<unsigned char>(code.front())) != 0) {
    return std::string{static_cast<char>(std::toupper(static_cast<unsigned char>(code.front())))};
  }
  if (code.size() == 1 && std::isdigit(static_cast<unsigned char>(code.front())) != 0) {
    return std::string{code};
  }

  for (const auto& [alias, canonical] : kKeyboardAliases) {
    if (equals_ignore_case(alias, code)) {
      return std::string{canonical};
    }
  }

  for (std::string_view supported : kKeyboardCodes) {
    if (equals_ignore_case(supported, code)) {
      return std::string{supported};
    }
  }
  for (std::string_view supported : kExtendedKeyboardCodes) {
    if (equals_ignore_case(supported, code)) {
      return std::string{supported};
    }
  }

  return std::nullopt;
}

template<usize Count>
[[nodiscard]] std::optional<std::string> canonical_named_code(
    const std::array<std::string_view, Count>& names,
    std::string_view code) noexcept {
  for (std::string_view supported : names) {
    if (equals_ignore_case(supported, code)) {
      return std::string{supported};
    }
  }
  return std::nullopt;
}

void set_default_binding(
    InputBindings& bindings,
    LogicalAction action,
    InputDeviceType device_type,
    std::string_view input_code,
    u8 modifiers = 0U) {
  bindings_for_action(bindings, action).push_back(InputBinding{device_type, std::string{input_code}, modifiers});
}

} // namespace

const ActionBindingList& bindings_for_action(
    const InputBindings& bindings,
    LogicalAction action) noexcept {
  return bindings.actions[logical_action_index(action)];
}

ActionBindingList& bindings_for_action(InputBindings& bindings, LogicalAction action) noexcept {
  return bindings.actions[logical_action_index(action)];
}

std::string_view logical_action_name(LogicalAction action) noexcept {
  return kActionNames[logical_action_index(action)].second;
}

std::optional<LogicalAction> logical_action_from_name(std::string_view name) noexcept {
  for (const auto& [action, action_name] : kActionNames) {
    if (action_name == name) {
      return action;
    }
  }
  return std::nullopt;
}

std::string_view input_device_type_name(InputDeviceType type) noexcept {
  switch (type) {
    case InputDeviceType::Keyboard:
      return "keyboard";
    case InputDeviceType::MouseButton:
      return "mouse_button";
    case InputDeviceType::MouseWheel:
      return "mouse_wheel";
  }
  return "keyboard";
}

std::optional<InputDeviceType> input_device_type_from_name(std::string_view name) noexcept {
  if (name == "keyboard") {
    return InputDeviceType::Keyboard;
  }
  if (name == "mouse_button") {
    return InputDeviceType::MouseButton;
  }
  if (name == "mouse_wheel") {
    return InputDeviceType::MouseWheel;
  }
  return std::nullopt;
}

std::string_view input_modifier_name(InputModifier modifier) noexcept {
  switch (modifier) {
    case InputModifier::Shift:
      return "Shift";
    case InputModifier::Control:
      return "Control";
    case InputModifier::Alt:
      return "Alt";
  }
  return "Shift";
}

std::optional<InputModifier> input_modifier_from_name(std::string_view name) noexcept {
  if (equals_ignore_case(name, "Shift")) {
    return InputModifier::Shift;
  }
  if (equals_ignore_case(name, "Control") || equals_ignore_case(name, "Ctrl")) {
    return InputModifier::Control;
  }
  if (equals_ignore_case(name, "Alt")) {
    return InputModifier::Alt;
  }
  return std::nullopt;
}

std::optional<std::string> canonical_input_code(
    InputDeviceType type,
    std::string_view code) noexcept {
  switch (type) {
    case InputDeviceType::Keyboard:
      return canonical_keyboard_code(code);
    case InputDeviceType::MouseButton:
      return canonical_named_code(kMouseButtons, code);
    case InputDeviceType::MouseWheel:
      return canonical_named_code(kMouseWheelDirections, code);
  }
  return std::nullopt;
}

bool is_valid_input_binding(const InputBinding& binding) noexcept {
  return canonical_input_code(binding.device_type, binding.input_code).has_value();
}

bool is_required_logical_action(LogicalAction action) noexcept {
  switch (action) {
    case LogicalAction::MoveForward:
    case LogicalAction::MoveBackward:
    case LogicalAction::MoveLeft:
    case LogicalAction::MoveRight:
    case LogicalAction::Jump:
    case LogicalAction::Sneak:
    case LogicalAction::PrimaryInteract:
    case LogicalAction::SecondaryInteract:
      return true;
    default:
      return false;
  }
}

InputBindings default_input_bindings() {
  InputBindings bindings;
  set_default_binding(bindings, LogicalAction::MoveForward, InputDeviceType::Keyboard, "W");
  set_default_binding(bindings, LogicalAction::MoveBackward, InputDeviceType::Keyboard, "S");
  set_default_binding(bindings, LogicalAction::MoveLeft, InputDeviceType::Keyboard, "A");
  set_default_binding(bindings, LogicalAction::MoveRight, InputDeviceType::Keyboard, "D");
  set_default_binding(bindings, LogicalAction::Jump, InputDeviceType::Keyboard, "Space");
  set_default_binding(bindings, LogicalAction::Sneak, InputDeviceType::Keyboard, "LShift");
  set_default_binding(bindings, LogicalAction::Sprint, InputDeviceType::Keyboard, "LCtrl");
  set_default_binding(bindings, LogicalAction::PrimaryInteract, InputDeviceType::MouseButton, "Left");
  set_default_binding(bindings, LogicalAction::SecondaryInteract, InputDeviceType::MouseButton, "Right");
  set_default_binding(bindings, LogicalAction::Hotbar1, InputDeviceType::Keyboard, "1");
  set_default_binding(bindings, LogicalAction::Hotbar2, InputDeviceType::Keyboard, "2");
  set_default_binding(bindings, LogicalAction::Hotbar3, InputDeviceType::Keyboard, "3");
  set_default_binding(bindings, LogicalAction::Hotbar4, InputDeviceType::Keyboard, "4");
  set_default_binding(bindings, LogicalAction::Hotbar5, InputDeviceType::Keyboard, "5");
  set_default_binding(bindings, LogicalAction::Hotbar6, InputDeviceType::Keyboard, "6");
  set_default_binding(bindings, LogicalAction::Hotbar7, InputDeviceType::Keyboard, "7");
  set_default_binding(bindings, LogicalAction::Hotbar8, InputDeviceType::Keyboard, "8");
  set_default_binding(bindings, LogicalAction::Hotbar9, InputDeviceType::Keyboard, "9");
  set_default_binding(bindings, LogicalAction::NextHotbarSlot, InputDeviceType::MouseWheel, "Up");
  set_default_binding(bindings, LogicalAction::PrevHotbarSlot, InputDeviceType::MouseWheel, "Down");
  set_default_binding(bindings, LogicalAction::ToggleFly, InputDeviceType::Keyboard, "F");
  set_default_binding(bindings, LogicalAction::OpenInventory, InputDeviceType::Keyboard, "E");
  set_default_binding(bindings, LogicalAction::OpenChat, InputDeviceType::Keyboard, "T");
  set_default_binding(bindings, LogicalAction::ShowTabMenu, InputDeviceType::Keyboard, "Tab");
  set_default_binding(bindings, LogicalAction::TogglePauseMenu, InputDeviceType::Keyboard, "Escape");
  set_default_binding(bindings, LogicalAction::ToggleDebugMenu, InputDeviceType::Keyboard, "Grave");
  return bindings;
}

} // namespace arv::platform
