#include "web_input.hpp"

#include "web_application.hpp"
#include "web_platform.hpp"

#include <emscripten/html5.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace {

inline constexpr double BrowserCssPixelToDesktopMouseUnitScale = 800.0 / 96.0;

EM_BOOL on_resize(int, const EmscriptenUiEvent*, void*) {
  arv::web::resize_canvas();
  return EM_FALSE;
}

EM_BOOL on_focus(int event_type, const EmscriptenFocusEvent*, void*) {
  arv::web::g_app.focused = event_type == EMSCRIPTEN_EVENT_FOCUS;
  if (!arv::web::g_app.focused) {
    arv::web::reset_input(true);
  }
  arv::web::set_status(arv::web::g_app.focused ? "focused" : "focus lost");
  return EM_FALSE;
}

EM_BOOL on_visibility(int, const EmscriptenVisibilityChangeEvent* event, void*) {
  arv::web::g_app.visible = event != nullptr && !event->hidden;
  arv::web::set_status(arv::web::g_app.visible ? "visible" : "paused while hidden");
  return EM_FALSE;
}

EM_BOOL on_pointerlock(int, const EmscriptenPointerlockChangeEvent* event, void*) {
  arv::web::g_app.pointer_locked = event != nullptr && event->isActive;
  arv::web::g_app.input.mouse_dx = 0.0;
  arv::web::g_app.input.mouse_dy = 0.0;
  arv::web::set_status(arv::web::g_app.pointer_locked ? "pointer locked" : "pointer released");
  return EM_FALSE;
}

EM_BOOL on_canvas_click(int, const EmscriptenMouseEvent*, void*) {
  if (arv::web::ui_wants_cursor()) {
    return EM_FALSE;
  }
  if (!arv::web::g_app.pointer_locked) {
    arv::web::request_pointer_lock();
  }
  return EM_TRUE;
}

void update_cursor_from_mouse(const EmscriptenMouseEvent& event) {
  arv::web::g_app.input.cursor_x = static_cast<double>(event.targetX);
  arv::web::g_app.input.cursor_y = static_cast<double>(event.targetY);
  arv::web::g_app.input.cursor_initialized = true;
}

[[nodiscard]] std::optional<std::string> browser_keyboard_code(
    const EmscriptenKeyboardEvent& event) {
  const std::string_view code{event.code};
  const auto canonical = [](std::string_view value) {
    return arv::platform::canonical_input_code(
        arv::platform::InputDeviceType::Keyboard,
        value);
  };
  if (code.starts_with("Key") && code.size() == 4U) {
    return canonical(code.substr(3U, 1U));
  }
  if (code.starts_with("Digit") && code.size() == 6U) {
    return canonical(code.substr(5U, 1U));
  }
  if (code == "ShiftLeft") {
    return std::string{"LShift"};
  }
  if (code == "ShiftRight") {
    return std::string{"RShift"};
  }
  if (code == "ControlLeft") {
    return std::string{"LCtrl"};
  }
  if (code == "ControlRight") {
    return std::string{"RCtrl"};
  }
  if (code == "AltLeft") {
    return std::string{"LAlt"};
  }
  if (code == "AltRight") {
    return std::string{"RAlt"};
  }
  if (code == "Backquote") {
    return std::string{"Grave"};
  }
  if (code == "BracketLeft") {
    return std::string{"LeftBracket"};
  }
  if (code == "BracketRight") {
    return std::string{"RightBracket"};
  }
  if (code == "Quote") {
    return std::string{"Apostrophe"};
  }
  if (std::optional<std::string> from_code = canonical(code); from_code.has_value()) {
    return from_code;
  }
  return canonical(event.key);
}

[[nodiscard]] arv::u8 keyboard_modifiers(const EmscriptenKeyboardEvent& event) noexcept {
  arv::u8 modifiers = 0U;
  if (event.shiftKey) {
    modifiers |= arv::platform::input_modifier_mask(arv::platform::InputModifier::Shift);
  }
  if (event.ctrlKey) {
    modifiers |= arv::platform::input_modifier_mask(arv::platform::InputModifier::Control);
  }
  if (event.altKey) {
    modifiers |= arv::platform::input_modifier_mask(arv::platform::InputModifier::Alt);
  }
  return modifiers;
}

[[nodiscard]] bool modifiers_match(arv::u8 active, arv::u8 required) noexcept {
  return (active & required) == required;
}

[[nodiscard]] bool action_matches(
    arv::platform::LogicalAction action,
    arv::platform::InputDeviceType device,
    std::string_view code,
    arv::u8 modifiers) {
  for (const arv::platform::InputBinding& binding :
       arv::platform::bindings_for_action(arv::web::g_app.input_bindings, action)) {
    if (binding.device_type == device &&
        binding.input_code == code &&
        modifiers_match(modifiers, binding.modifiers)) {
      return true;
    }
  }
  return false;
}

void apply_logical_action(
    arv::platform::LogicalAction action,
    bool down,
    bool repeat) {
  using arv::platform::LogicalAction;
  switch (action) {
    case LogicalAction::MoveForward:
      arv::web::g_app.input.forward = down;
      break;
    case LogicalAction::MoveBackward:
      arv::web::g_app.input.back = down;
      break;
    case LogicalAction::MoveLeft:
      arv::web::g_app.input.left = down;
      break;
    case LogicalAction::MoveRight:
      arv::web::g_app.input.right = down;
      break;
    case LogicalAction::Jump:
      if (down && !repeat && !arv::web::g_app.input.jump) {
        arv::web::g_app.input.jump_pressed = true;
      }
      arv::web::g_app.input.jump = down;
      break;
    case LogicalAction::Sneak:
      arv::web::g_app.input.sneak = down;
      break;
    case LogicalAction::Sprint:
      arv::web::g_app.input.sprint = down;
      break;
    case LogicalAction::Mount:
      if (down && !repeat) {
        arv::web::g_app.input.mount_pressed = true;
      }
      break;
    case LogicalAction::VehicleRollLeft:
      arv::web::g_app.input.vehicle_roll_left_held = down;
      break;
    case LogicalAction::VehicleRollRight:
      arv::web::g_app.input.vehicle_roll_right_held = down;
      break;
    case LogicalAction::PrimaryInteract:
      if (down && !arv::web::g_app.input.primary_held) {
        arv::web::g_app.input.primary_pressed = true;
        arv::web::g_app.input.primary_animation_pressed = true;
      }
      arv::web::g_app.input.primary_held = down;
      break;
    case LogicalAction::SecondaryInteract:
      if (down && !arv::web::g_app.input.secondary_held) {
        arv::web::g_app.input.secondary_pressed = true;
        arv::web::g_app.input.secondary_animation_pressed = true;
      }
      arv::web::g_app.input.secondary_held = down;
      break;
    case LogicalAction::OpenInventory:
      if (down && !repeat) {
        arv::web::g_app.input.open_inventory_pressed = true;
      }
      break;
    case LogicalAction::OpenChat:
      if (down && !repeat) {
        arv::web::g_app.input.open_chat_pressed = true;
      }
      break;
    case LogicalAction::ShowTabMenu:
      arv::web::g_app.input.tab_menu_held = down;
      break;
    case LogicalAction::TogglePauseMenu:
      if (down && !repeat) {
        arv::web::g_app.input.text_cancel_pressed = true;
        arv::web::g_app.input.pause_menu_pressed = true;
      }
      break;
    case LogicalAction::Hotbar1:
    case LogicalAction::Hotbar2:
    case LogicalAction::Hotbar3:
    case LogicalAction::Hotbar4:
    case LogicalAction::Hotbar5:
    case LogicalAction::Hotbar6:
    case LogicalAction::Hotbar7:
    case LogicalAction::Hotbar8:
    case LogicalAction::Hotbar9:
      if (down && !repeat) {
        const auto slot = static_cast<arv::u8>(
            static_cast<arv::u8>(action) -
            static_cast<arv::u8>(LogicalAction::Hotbar1));
        arv::web::g_app.input.hotbar_select = slot;
      }
      break;
    case LogicalAction::NextHotbarSlot:
    case LogicalAction::PrevHotbarSlot:
    case LogicalAction::ToggleDebugMenu:
    case LogicalAction::Count:
      break;
  }
}

bool set_key_state(const EmscriptenKeyboardEvent& event, bool down) {
  const std::optional<std::string> code = browser_keyboard_code(event);
  if (!code.has_value()) {
    return false;
  }
  const arv::u8 modifiers = keyboard_modifiers(event);
  bool handled = false;
  for (arv::usize index = 0;
       index < static_cast<arv::usize>(arv::platform::LogicalAction::Count);
       ++index) {
    const auto action = static_cast<arv::platform::LogicalAction>(index);
    if (!action_matches(
            action,
            arv::platform::InputDeviceType::Keyboard,
            code.value(),
            modifiers)) {
      continue;
    }
    apply_logical_action(action, down, event.repeat);
    handled = true;
  }
  return handled;
}

[[nodiscard]] bool key_matches_action(
    const EmscriptenKeyboardEvent& event,
    arv::platform::LogicalAction action) {
  const std::optional<std::string> code = browser_keyboard_code(event);
  if (!code.has_value()) {
    return false;
  }
  return action_matches(
      action,
      arv::platform::InputDeviceType::Keyboard,
      code.value(),
      keyboard_modifiers(event));
}

[[nodiscard]] bool chat_text_key(const EmscriptenKeyboardEvent& event) {
  if (std::strcmp(event.key, "Backspace") == 0) {
    arv::web::g_app.input.text_backspace_pressed = true;
    return true;
  }
  if (std::strcmp(event.key, "Enter") == 0) {
    arv::web::g_app.input.text_submit_pressed = true;
    return true;
  }
  if (std::strcmp(event.key, "Escape") == 0) {
    arv::web::g_app.input.text_cancel_pressed = true;
    return true;
  }
  if (event.ctrlKey || event.metaKey || event.altKey) {
    return false;
  }
  const std::string_view text{event.charValue[0] != '\0' ? event.charValue : event.key};
  if (!text.empty() && text.size() <= 4U) {
    arv::web::g_app.input.text_input.append(text);
    return true;
  }
  return false;
}

EM_BOOL on_key(int event_type, const EmscriptenKeyboardEvent* event, void*) {
  if (event == nullptr) {
    return EM_FALSE;
  }
  const bool down = event_type == EMSCRIPTEN_EVENT_KEYDOWN;
  const bool has_cursor_overlay =
      arv::web::g_app.session != nullptr &&
      (arv::web::g_app.session->inventory_open() ||
       arv::web::g_app.session->chat_open());
  const bool should_recapture_pointer =
      down &&
      !event->repeat &&
      has_cursor_overlay &&
      (key_matches_action(*event, arv::platform::LogicalAction::OpenInventory) ||
       key_matches_action(*event, arv::platform::LogicalAction::TogglePauseMenu) ||
       (arv::web::g_app.session->chat_open() &&
        std::strcmp(event->key, "Enter") == 0));
  if (down && arv::web::g_app.session != nullptr &&
      arv::web::g_app.session->chat_open() &&
      chat_text_key(*event)) {
    if (should_recapture_pointer && !arv::web::g_app.pointer_locked) {
      arv::web::request_pointer_lock();
    }
    return EM_TRUE;
  }
  if (set_key_state(*event, down)) {
    if (should_recapture_pointer && !arv::web::g_app.pointer_locked) {
      arv::web::request_pointer_lock();
    }
    return EM_TRUE;
  }
  return EM_FALSE;
}

EM_BOOL on_mouse_move(int, const EmscriptenMouseEvent* event, void*) {
  if (event == nullptr) {
    return EM_FALSE;
  }
  update_cursor_from_mouse(*event);
  if (!arv::web::g_app.pointer_locked || arv::web::ui_wants_cursor()) {
    return arv::web::ui_wants_cursor() ? EM_TRUE : EM_FALSE;
  }
  const double scale = arv::web::pointer_lock_uses_raw_movement()
      ? 1.0
      : BrowserCssPixelToDesktopMouseUnitScale;
  arv::web::g_app.input.mouse_dx += static_cast<double>(event->movementX) * scale;
  arv::web::g_app.input.mouse_dy += static_cast<double>(event->movementY) * scale;
  return EM_TRUE;
}

EM_BOOL on_mouse_button(int event_type, const EmscriptenMouseEvent* event, void*) {
  if (event == nullptr) {
    return EM_FALSE;
  }
  update_cursor_from_mouse(*event);
  const bool down = event_type == EMSCRIPTEN_EVENT_MOUSEDOWN;
  if (arv::web::ui_wants_cursor()) {
    if (event->button == 0) {
      if (down) {
        arv::web::g_app.input.mouse_left_pressed = true;
        arv::web::g_app.input.mouse_left_held = true;
      } else {
        arv::web::g_app.input.mouse_left_released = true;
        arv::web::g_app.input.mouse_left_held = false;
      }
      return EM_TRUE;
    }
    if (event->button == 2) {
      if (down) {
        arv::web::g_app.input.mouse_right_pressed = true;
        arv::web::g_app.input.mouse_right_held = true;
      } else {
        arv::web::g_app.input.mouse_right_released = true;
        arv::web::g_app.input.mouse_right_held = false;
      }
      return EM_TRUE;
    }
  }
  if (!arv::web::g_app.pointer_locked) {
    return EM_FALSE;
  }
  const std::string_view code =
      event->button == 0 ? "Left" : (event->button == 2 ? "Right" : "Middle");
  bool handled = false;
  for (arv::usize index = 0;
       index < static_cast<arv::usize>(arv::platform::LogicalAction::Count);
       ++index) {
    const auto action = static_cast<arv::platform::LogicalAction>(index);
    if (action_matches(
            action,
            arv::platform::InputDeviceType::MouseButton,
            code,
            0U)) {
      apply_logical_action(action, down, false);
      handled = true;
    }
  }
  if (handled) {
    return EM_TRUE;
  }
  if (event->button == 0) {
    if (down && !arv::web::g_app.input.primary_held) {
      arv::web::g_app.input.primary_pressed = true;
      arv::web::g_app.input.primary_animation_pressed = true;
    }
    arv::web::g_app.input.primary_held = down;
    return EM_TRUE;
  }
  if (event->button == 2) {
    if (down && !arv::web::g_app.input.secondary_held) {
      arv::web::g_app.input.secondary_pressed = true;
      arv::web::g_app.input.secondary_animation_pressed = true;
    }
    arv::web::g_app.input.secondary_held = down;
    return EM_TRUE;
  }
  return EM_FALSE;
}

EM_BOOL on_wheel(int, const EmscriptenWheelEvent* event, void*) {
  if (event == nullptr || event->deltaY == 0.0) {
    return EM_FALSE;
  }
  if (arv::web::ui_wants_cursor()) {
    arv::web::g_app.input.mouse_wheel_y += event->deltaY < 0.0 ? 1.0 : -1.0;
    return EM_TRUE;
  }
  if (!arv::web::g_app.pointer_locked) {
    return EM_FALSE;
  }
  const std::string_view code = event->deltaY > 0.0 ? "Down" : "Up";
  int step = 0;
  if (action_matches(
          arv::platform::LogicalAction::NextHotbarSlot,
          arv::platform::InputDeviceType::MouseWheel,
          code,
          0U)) {
    ++step;
  }
  if (action_matches(
          arv::platform::LogicalAction::PrevHotbarSlot,
          arv::platform::InputDeviceType::MouseWheel,
          code,
          0U)) {
    --step;
  }
  if (step == 0) {
    return EM_FALSE;
  }
  const int combined = std::clamp(
      static_cast<int>(arv::web::g_app.input.hotbar_scroll_steps) + step,
      -127,
      127);
  arv::web::g_app.input.hotbar_scroll_steps = static_cast<arv::i8>(combined);
  return EM_TRUE;
}

} // namespace

namespace arv::web {

void install_input_callbacks() {
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_resize);
  emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_focus);
  emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, on_focus);
  emscripten_set_visibilitychange_callback(nullptr, EM_TRUE, on_visibility);
  emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_pointerlock);
  emscripten_set_click_callback("#arvane-canvas", nullptr, EM_TRUE, on_canvas_click);
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_key);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_key);
  emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_mouse_move);
  emscripten_set_mousedown_callback("#arvane-canvas", nullptr, EM_TRUE, on_mouse_button);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_mouse_button);
  emscripten_set_wheel_callback("#arvane-canvas", nullptr, EM_TRUE, on_wheel);
}

} // namespace arv::web
