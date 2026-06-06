#include "arv/platform/glfw_window.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <arv/core/log.hpp>

namespace arv::platform {
namespace {

constexpr std::array<std::pair<int, std::string_view>, 57> kCaptureKeys{{
    {GLFW_KEY_A, "A"},
    {GLFW_KEY_B, "B"},
    {GLFW_KEY_C, "C"},
    {GLFW_KEY_D, "D"},
    {GLFW_KEY_E, "E"},
    {GLFW_KEY_F, "F"},
    {GLFW_KEY_G, "G"},
    {GLFW_KEY_H, "H"},
    {GLFW_KEY_I, "I"},
    {GLFW_KEY_J, "J"},
    {GLFW_KEY_K, "K"},
    {GLFW_KEY_L, "L"},
    {GLFW_KEY_M, "M"},
    {GLFW_KEY_N, "N"},
    {GLFW_KEY_O, "O"},
    {GLFW_KEY_P, "P"},
    {GLFW_KEY_Q, "Q"},
    {GLFW_KEY_R, "R"},
    {GLFW_KEY_S, "S"},
    {GLFW_KEY_T, "T"},
    {GLFW_KEY_U, "U"},
    {GLFW_KEY_V, "V"},
    {GLFW_KEY_W, "W"},
    {GLFW_KEY_X, "X"},
    {GLFW_KEY_Y, "Y"},
    {GLFW_KEY_Z, "Z"},
    {GLFW_KEY_0, "0"},
    {GLFW_KEY_1, "1"},
    {GLFW_KEY_2, "2"},
    {GLFW_KEY_3, "3"},
    {GLFW_KEY_4, "4"},
    {GLFW_KEY_5, "5"},
    {GLFW_KEY_6, "6"},
    {GLFW_KEY_7, "7"},
    {GLFW_KEY_8, "8"},
    {GLFW_KEY_9, "9"},
    {GLFW_KEY_SPACE, "Space"},
    {GLFW_KEY_TAB, "Tab"},
    {GLFW_KEY_ENTER, "Enter"},
    {GLFW_KEY_ESCAPE, "Escape"},
    {GLFW_KEY_BACKSPACE, "Backspace"},
    {GLFW_KEY_MINUS, "Minus"},
    {GLFW_KEY_EQUAL, "Equal"},
    {GLFW_KEY_LEFT_BRACKET, "LeftBracket"},
    {GLFW_KEY_RIGHT_BRACKET, "RightBracket"},
    {GLFW_KEY_SEMICOLON, "Semicolon"},
    {GLFW_KEY_APOSTROPHE, "Apostrophe"},
    {GLFW_KEY_COMMA, "Comma"},
    {GLFW_KEY_PERIOD, "Period"},
    {GLFW_KEY_GRAVE_ACCENT, "Grave"},
    {GLFW_KEY_SLASH, "Slash"},
    {GLFW_KEY_LEFT_SHIFT, "LShift"},
    {GLFW_KEY_RIGHT_SHIFT, "RShift"},
    {GLFW_KEY_LEFT_CONTROL, "LCtrl"},
    {GLFW_KEY_RIGHT_CONTROL, "RCtrl"},
    {GLFW_KEY_LEFT_ALT, "LAlt"},
    {GLFW_KEY_RIGHT_ALT, "RAlt"},
}};

[[nodiscard]] std::optional<int> glfw_key_from_code(std::string_view code) noexcept {
  if (code.size() == 1) {
    const char value = code.front();
    if (value >= 'A' && value <= 'Z') {
      return GLFW_KEY_A + (value - 'A');
    }
    if (value >= '0' && value <= '9') {
      return GLFW_KEY_0 + (value - '0');
    }
  }

  if (code == "Space") {
    return GLFW_KEY_SPACE;
  }
  if (code == "Tab") {
    return GLFW_KEY_TAB;
  }
  if (code == "Enter") {
    return GLFW_KEY_ENTER;
  }
  if (code == "Escape") {
    return GLFW_KEY_ESCAPE;
  }
  if (code == "Backspace") {
    return GLFW_KEY_BACKSPACE;
  }
  if (code == "Minus") {
    return GLFW_KEY_MINUS;
  }
  if (code == "Equal") {
    return GLFW_KEY_EQUAL;
  }
  if (code == "LeftBracket") {
    return GLFW_KEY_LEFT_BRACKET;
  }
  if (code == "RightBracket") {
    return GLFW_KEY_RIGHT_BRACKET;
  }
  if (code == "Semicolon") {
    return GLFW_KEY_SEMICOLON;
  }
  if (code == "Apostrophe") {
    return GLFW_KEY_APOSTROPHE;
  }
  if (code == "Grave") {
    return GLFW_KEY_GRAVE_ACCENT;
  }
  if (code == "Comma") {
    return GLFW_KEY_COMMA;
  }
  if (code == "Period") {
    return GLFW_KEY_PERIOD;
  }
  if (code == "Slash") {
    return GLFW_KEY_SLASH;
  }
  if (code == "LShift") {
    return GLFW_KEY_LEFT_SHIFT;
  }
  if (code == "RShift") {
    return GLFW_KEY_RIGHT_SHIFT;
  }
  if (code == "LCtrl") {
    return GLFW_KEY_LEFT_CONTROL;
  }
  if (code == "RCtrl") {
    return GLFW_KEY_RIGHT_CONTROL;
  }
  if (code == "LAlt") {
    return GLFW_KEY_LEFT_ALT;
  }
  if (code == "RAlt") {
    return GLFW_KEY_RIGHT_ALT;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<int> glfw_mouse_button_from_code(std::string_view code) noexcept {
  if (code == "Left") {
    return GLFW_MOUSE_BUTTON_LEFT;
  }
  if (code == "Right") {
    return GLFW_MOUSE_BUTTON_RIGHT;
  }
  if (code == "Middle") {
    return GLFW_MOUSE_BUTTON_MIDDLE;
  }
  return std::nullopt;
}

[[nodiscard]] bool modifier_group_active(GLFWwindow* window, InputModifier modifier) noexcept {
  switch (modifier) {
    case InputModifier::Shift:
      return glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
             glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    case InputModifier::Control:
      return glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
             glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    case InputModifier::Alt:
      return glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
             glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
  }
  return false;
}

[[nodiscard]] bool modifiers_active(GLFWwindow* window, u8 modifiers) noexcept {
  if (has_input_modifier(modifiers, InputModifier::Shift) &&
      !modifier_group_active(window, InputModifier::Shift)) {
    return false;
  }
  if (has_input_modifier(modifiers, InputModifier::Control) &&
      !modifier_group_active(window, InputModifier::Control)) {
    return false;
  }
  if (has_input_modifier(modifiers, InputModifier::Alt) &&
      !modifier_group_active(window, InputModifier::Alt)) {
    return false;
  }
  return true;
}

[[nodiscard]] bool key_is_modifier(int key, InputModifier modifier) noexcept {
  switch (modifier) {
    case InputModifier::Shift:
      return key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT;
    case InputModifier::Control:
      return key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL;
    case InputModifier::Alt:
      return key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT;
  }
  return false;
}

[[nodiscard]] u8 current_capture_modifiers(GLFWwindow* window, int primary_key = GLFW_KEY_UNKNOWN) noexcept {
  u8 modifiers = 0U;
  for (const InputModifier modifier :
       {InputModifier::Shift, InputModifier::Control, InputModifier::Alt}) {
    if (!key_is_modifier(primary_key, modifier) && modifier_group_active(window, modifier)) {
      modifiers |= input_modifier_mask(modifier);
    }
  }
  return modifiers;
}

[[nodiscard]] bool binding_hold_active(
    GLFWwindow* window,
    const InputBinding& binding) noexcept {
  if (!modifiers_active(window, binding.modifiers)) {
    return false;
  }

  switch (binding.device_type) {
    case InputDeviceType::Keyboard: {
      const auto key = glfw_key_from_code(binding.input_code);
      return key.has_value() && glfwGetKey(window, key.value()) == GLFW_PRESS;
    }
    case InputDeviceType::MouseButton: {
      const auto button = glfw_mouse_button_from_code(binding.input_code);
      return button.has_value() && glfwGetMouseButton(window, button.value()) == GLFW_PRESS;
    }
    case InputDeviceType::MouseWheel:
      return false;
  }
  return false;
}

[[nodiscard]] i32 binding_press_count(
    GLFWwindow* window,
    const InputBinding& binding,
    i32 wheel_up_steps,
    i32 wheel_down_steps) noexcept {
  if (!modifiers_active(window, binding.modifiers)) {
    return 0;
  }

  if (binding.device_type != InputDeviceType::MouseWheel) {
    return 0;
  }
  if (binding.input_code == "Up") {
    return wheel_up_steps;
  }
  if (binding.input_code == "Down") {
    return wheel_down_steps;
  }
  return 0;
}

[[nodiscard]] bool action_hold_active(
    GLFWwindow* window,
    const InputBindings& bindings,
    LogicalAction action) noexcept {
  for (const InputBinding& binding : bindings_for_action(bindings, action)) {
    if (binding_hold_active(window, binding)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] i32 action_press_count(
    GLFWwindow* window,
    const InputBindings& bindings,
    LogicalAction action,
    bool previously_active,
    i32 wheel_up_steps,
    i32 wheel_down_steps) noexcept {
  i32 press_count = 0;
  bool hold_active = false;
  for (const InputBinding& binding : bindings_for_action(bindings, action)) {
    hold_active = hold_active || binding_hold_active(window, binding);
    press_count += binding_press_count(window, binding, wheel_up_steps, wheel_down_steps);
  }
  if (hold_active && !previously_active) {
    ++press_count;
  }
  return press_count;
}

void glfw_scroll_callback(GLFWwindow* window, double x_offset, double y_offset) {
  (void)x_offset;
  auto* owner = static_cast<OpenGLWindow*>(glfwGetWindowUserPointer(window));
  if (owner != nullptr) {
    owner->accumulate_scroll(y_offset);
  }
}

void glfw_char_callback(GLFWwindow* window, unsigned int codepoint) {
  auto* owner = static_cast<OpenGLWindow*>(glfwGetWindowUserPointer(window));
  if (owner != nullptr) {
    owner->append_text_input(static_cast<u32>(codepoint));
  }
}

} // namespace

GlfwLibrary::GlfwLibrary() : initialized_(glfwInit() == GLFW_TRUE) {
}

GlfwLibrary::~GlfwLibrary() {
  if (initialized_) {
    glfwTerminate();
  }
}

OpenGLWindow::OpenGLWindow(OpenGLWindow&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)),
      has_last_cursor_(std::exchange(other.has_last_cursor_, false)),
      last_cursor_x_(std::exchange(other.last_cursor_x_, 0.0)),
      last_cursor_y_(std::exchange(other.last_cursor_y_, 0.0)),
      accumulated_scroll_y_(std::exchange(other.accumulated_scroll_y_, 0.0)),
      cursor_captured_(std::exchange(other.cursor_captured_, true)),
      previous_left_mouse_down_(std::exchange(other.previous_left_mouse_down_, false)),
      previous_right_mouse_down_(std::exchange(other.previous_right_mouse_down_, false)),
      previous_middle_mouse_down_(std::exchange(other.previous_middle_mouse_down_, false)),
      previous_backspace_down_(std::exchange(other.previous_backspace_down_, false)),
      previous_enter_down_(std::exchange(other.previous_enter_down_, false)),
      previous_left_arrow_down_(std::exchange(other.previous_left_arrow_down_, false)),
      previous_right_arrow_down_(std::exchange(other.previous_right_arrow_down_, false)),
      previous_delete_down_(std::exchange(other.previous_delete_down_, false)),
      previous_copy_shortcut_down_(std::exchange(other.previous_copy_shortcut_down_, false)),
      previous_paste_shortcut_down_(std::exchange(other.previous_paste_shortcut_down_, false)),
      previous_cut_shortcut_down_(std::exchange(other.previous_cut_shortcut_down_, false)),
      previous_select_all_shortcut_down_(
          std::exchange(other.previous_select_all_shortcut_down_, false)),
      pending_text_input_(std::move(other.pending_text_input_)),
      input_bindings_(std::move(other.input_bindings_)),
      previous_action_active_(other.previous_action_active_),
      previous_capture_key_down_(other.previous_capture_key_down_) {
  other.previous_action_active_.fill(false);
  other.previous_capture_key_down_.fill(false);
  if (window_ != nullptr) {
    glfwSetWindowUserPointer(window_, this);
  }
}

OpenGLWindow& OpenGLWindow::operator=(OpenGLWindow&& other) noexcept {
  if (this != &other) {
    destroy();
    window_ = std::exchange(other.window_, nullptr);
    has_last_cursor_ = std::exchange(other.has_last_cursor_, false);
    last_cursor_x_ = std::exchange(other.last_cursor_x_, 0.0);
    last_cursor_y_ = std::exchange(other.last_cursor_y_, 0.0);
    accumulated_scroll_y_ = std::exchange(other.accumulated_scroll_y_, 0.0);
    cursor_captured_ = std::exchange(other.cursor_captured_, true);
    previous_left_mouse_down_ = std::exchange(other.previous_left_mouse_down_, false);
    previous_right_mouse_down_ = std::exchange(other.previous_right_mouse_down_, false);
    previous_middle_mouse_down_ = std::exchange(other.previous_middle_mouse_down_, false);
    previous_backspace_down_ = std::exchange(other.previous_backspace_down_, false);
    previous_enter_down_ = std::exchange(other.previous_enter_down_, false);
    previous_left_arrow_down_ = std::exchange(other.previous_left_arrow_down_, false);
    previous_right_arrow_down_ = std::exchange(other.previous_right_arrow_down_, false);
    previous_delete_down_ = std::exchange(other.previous_delete_down_, false);
    previous_copy_shortcut_down_ = std::exchange(other.previous_copy_shortcut_down_, false);
    previous_paste_shortcut_down_ = std::exchange(other.previous_paste_shortcut_down_, false);
    previous_cut_shortcut_down_ = std::exchange(other.previous_cut_shortcut_down_, false);
    previous_select_all_shortcut_down_ =
        std::exchange(other.previous_select_all_shortcut_down_, false);
    pending_text_input_ = std::move(other.pending_text_input_);
    input_bindings_ = std::move(other.input_bindings_);
    previous_action_active_ = other.previous_action_active_;
    previous_capture_key_down_ = other.previous_capture_key_down_;
    other.previous_action_active_.fill(false);
    other.previous_capture_key_down_.fill(false);
    if (window_ != nullptr) {
      glfwSetWindowUserPointer(window_, this);
    }
  }
  return *this;
}

OpenGLWindow::~OpenGLWindow() {
  destroy();
}

Expected<OpenGLWindow, PlatformError> OpenGLWindow::create(const WindowConfig& config) {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* raw_window = glfwCreateWindow(
      static_cast<int>(config.width),
      static_cast<int>(config.height),
      config.title.c_str(),
      nullptr,
      nullptr);

  if (raw_window == nullptr) {
    return Expected<OpenGLWindow, PlatformError>::failure(PlatformError{"failed to create GLFW window"});
  }

  OpenGLWindow window;
  window.window_ = raw_window;
  glfwSetInputMode(raw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
    glfwSetInputMode(raw_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }

  glfwSetWindowUserPointer(raw_window, &window);
  glfwSetScrollCallback(raw_window, glfw_scroll_callback);
  glfwSetCharCallback(raw_window, glfw_char_callback);
  (void)config;
  return Expected<OpenGLWindow, PlatformError>::success(std::move(window));
}

void OpenGLWindow::make_context_current() {
  glfwMakeContextCurrent(window_);
}

bool OpenGLWindow::load_gl_functions() noexcept {
  return gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0;
}

bool OpenGLWindow::should_close() const {
  return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void OpenGLWindow::request_close() {
  glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

InputSnapshot OpenGLWindow::poll_input() {
  InputSnapshot input;
  const bool backspace_down = glfwGetKey(window_, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
  const bool enter_down = glfwGetKey(window_, GLFW_KEY_ENTER) == GLFW_PRESS;
  const bool left_arrow_down = glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS;
  const bool right_arrow_down = glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS;
  const bool up_arrow_down = glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS;
  const bool down_arrow_down = glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS;
  const bool delete_down = glfwGetKey(window_, GLFW_KEY_DELETE) == GLFW_PRESS;
  const bool shift_down =
      glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
      glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
  const bool command_down =
      glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(window_, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
      glfwGetKey(window_, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
  const bool copy_shortcut_down = command_down && glfwGetKey(window_, GLFW_KEY_C) == GLFW_PRESS;
  const bool paste_shortcut_down = command_down && glfwGetKey(window_, GLFW_KEY_V) == GLFW_PRESS;
  const bool cut_shortcut_down = command_down && glfwGetKey(window_, GLFW_KEY_X) == GLFW_PRESS;
  const bool select_all_shortcut_down = command_down && glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS;
  const bool left_mouse_down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool right_mouse_down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  const bool middle_mouse_down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
  input.text_backspace_pressed = backspace_down && !previous_backspace_down_;
  input.text_submit_pressed = enter_down && !previous_enter_down_;
  if (left_arrow_down && !previous_left_arrow_down_) {
    input.text_edit_events.push_back({TextEditCommand::MoveLeft, shift_down});
  }
  if (right_arrow_down && !previous_right_arrow_down_) {
    input.text_edit_events.push_back({TextEditCommand::MoveRight, shift_down});
  }
  if (up_arrow_down && !previous_up_arrow_down_) {
    input.text_edit_events.push_back({TextEditCommand::MoveUp, shift_down});
  }
  if (down_arrow_down && !previous_down_arrow_down_) {
    input.text_edit_events.push_back({TextEditCommand::MoveDown, shift_down});
  }
  if (delete_down && !previous_delete_down_) {
    input.text_edit_events.push_back({TextEditCommand::DeleteForward, false});
  }
  if (copy_shortcut_down && !previous_copy_shortcut_down_) {
    input.text_edit_events.push_back({TextEditCommand::Copy, false});
  }
  if (paste_shortcut_down && !previous_paste_shortcut_down_) {
    input.text_edit_events.push_back({TextEditCommand::Paste, false, clipboard_text()});
  }
  if (cut_shortcut_down && !previous_cut_shortcut_down_) {
    input.text_edit_events.push_back({TextEditCommand::Cut, false});
  }
  if (select_all_shortcut_down && !previous_select_all_shortcut_down_) {
    input.text_edit_events.push_back({TextEditCommand::SelectAll, false});
  }
  input.mouse_left_pressed = left_mouse_down && !previous_left_mouse_down_;
  input.mouse_left_released = !left_mouse_down && previous_left_mouse_down_;
  input.mouse_left_held = left_mouse_down;
  input.mouse_right_pressed = right_mouse_down && !previous_right_mouse_down_;
  input.mouse_right_released = !right_mouse_down && previous_right_mouse_down_;
  input.mouse_right_held = right_mouse_down;
  input.text_input = std::move(pending_text_input_);
  pending_text_input_.clear();
  if (copy_shortcut_down || paste_shortcut_down || cut_shortcut_down ||
      select_all_shortcut_down) {
    input.text_input.clear();
  }
  const i32 wheel_up_steps = static_cast<i32>(std::max(accumulated_scroll_y_, 0.0));
  const i32 wheel_down_steps = static_cast<i32>(std::max(-accumulated_scroll_y_, 0.0));

  for (usize index = 0; index < kCaptureKeys.size(); ++index) {
    const auto& [key, code] = kCaptureKeys[index];
    const bool down = glfwGetKey(window_, key) == GLFW_PRESS;
    if (!input.raw_input.has_value() && down && !previous_capture_key_down_[index]) {
      input.raw_input = RawInputEvent{
          InputBinding{
              InputDeviceType::Keyboard,
              std::string{code},
              current_capture_modifiers(window_, key)}};
    }
    previous_capture_key_down_[index] = down;
  }
  if (!input.raw_input.has_value() && input.mouse_left_pressed) {
    input.raw_input = RawInputEvent{
        InputBinding{InputDeviceType::MouseButton, "Left", current_capture_modifiers(window_)}};
  }
  if (!input.raw_input.has_value() && input.mouse_right_pressed) {
    input.raw_input = RawInputEvent{
        InputBinding{InputDeviceType::MouseButton, "Right", current_capture_modifiers(window_)}};
  }
  if (!input.raw_input.has_value() && middle_mouse_down && !previous_middle_mouse_down_) {
    input.raw_input = RawInputEvent{
        InputBinding{InputDeviceType::MouseButton, "Middle", current_capture_modifiers(window_)}};
  }
  if (!input.raw_input.has_value() && wheel_up_steps > 0) {
    input.raw_input = RawInputEvent{
        InputBinding{InputDeviceType::MouseWheel, "Up", current_capture_modifiers(window_)}};
  } else if (!input.raw_input.has_value() && wheel_down_steps > 0) {
    input.raw_input = RawInputEvent{
        InputBinding{InputDeviceType::MouseWheel, "Down", current_capture_modifiers(window_)}};
  }

  input.move_forward = action_hold_active(window_, input_bindings_, LogicalAction::MoveForward);
  input.move_backward = action_hold_active(window_, input_bindings_, LogicalAction::MoveBackward);
  input.move_left = action_hold_active(window_, input_bindings_, LogicalAction::MoveLeft);
  input.move_right = action_hold_active(window_, input_bindings_, LogicalAction::MoveRight);
  input.jump_held = action_hold_active(window_, input_bindings_, LogicalAction::Jump);
  input.sneak_held = action_hold_active(window_, input_bindings_, LogicalAction::Sneak);
  input.sprint_held = action_hold_active(window_, input_bindings_, LogicalAction::Sprint);
  input.vehicle_roll_left_held =
      action_hold_active(window_, input_bindings_, LogicalAction::VehicleRollLeft);
  input.vehicle_roll_right_held =
      action_hold_active(window_, input_bindings_, LogicalAction::VehicleRollRight);
  input.primary_held = action_hold_active(window_, input_bindings_, LogicalAction::PrimaryInteract);
  input.secondary_held = action_hold_active(window_, input_bindings_, LogicalAction::SecondaryInteract);

  input.jump_pressed = action_press_count(
                           window_,
                           input_bindings_,
                           LogicalAction::Jump,
                           previous_action_active_[logical_action_index(LogicalAction::Jump)],
                           wheel_up_steps,
                           wheel_down_steps) > 0;
  input.primary_pressed = action_press_count(
                              window_,
                              input_bindings_,
                              LogicalAction::PrimaryInteract,
                              previous_action_active_[logical_action_index(LogicalAction::PrimaryInteract)],
                              wheel_up_steps,
                              wheel_down_steps) > 0;
  input.secondary_pressed = action_press_count(
                                window_,
                                input_bindings_,
                                LogicalAction::SecondaryInteract,
                                previous_action_active_[logical_action_index(LogicalAction::SecondaryInteract)],
                                wheel_up_steps,
                                wheel_down_steps) > 0;
  input.mount_pressed = action_press_count(
                            window_,
                            input_bindings_,
                            LogicalAction::Mount,
                            previous_action_active_[logical_action_index(LogicalAction::Mount)],
                            wheel_up_steps,
                            wheel_down_steps) > 0;
  input.open_inventory_pressed = action_press_count(
                                     window_,
                                     input_bindings_,
                                     LogicalAction::OpenInventory,
                                     previous_action_active_[logical_action_index(LogicalAction::OpenInventory)],
                                     wheel_up_steps,
                                     wheel_down_steps) > 0;
  input.open_chat_pressed = action_press_count(
                                window_,
                                input_bindings_,
                                LogicalAction::OpenChat,
                                previous_action_active_[logical_action_index(LogicalAction::OpenChat)],
                                wheel_up_steps,
                                wheel_down_steps) > 0;
  input.tab_menu_held = action_hold_active(window_, input_bindings_, LogicalAction::ShowTabMenu);
  input.pause_menu_pressed =
      action_press_count(
          window_,
          input_bindings_,
          LogicalAction::TogglePauseMenu,
          previous_action_active_[logical_action_index(LogicalAction::TogglePauseMenu)],
          wheel_up_steps,
          wheel_down_steps) > 0;
  input.text_cancel_pressed = input.pause_menu_pressed;
  input.debug_menu_toggle_pressed =
      action_press_count(
          window_,
          input_bindings_,
          LogicalAction::ToggleDebugMenu,
          previous_action_active_[logical_action_index(LogicalAction::ToggleDebugMenu)],
          wheel_up_steps,
          wheel_down_steps) > 0;
  input.mouse_wheel_y = static_cast<f32>(accumulated_scroll_y_);

  for (usize slot = 0; slot < 9U; ++slot) {
    const LogicalAction action = static_cast<LogicalAction>(
        static_cast<u8>(LogicalAction::Hotbar1) + static_cast<u8>(slot));
    if (action_press_count(
            window_,
            input_bindings_,
            action,
            previous_action_active_[logical_action_index(action)],
            wheel_up_steps,
            wheel_down_steps) > 0) {
      input.hotbar_select = static_cast<u8>(slot);
      break;
    }
  }

  const i32 next_scroll = action_press_count(
      window_,
      input_bindings_,
      LogicalAction::NextHotbarSlot,
      previous_action_active_[logical_action_index(LogicalAction::NextHotbarSlot)],
      wheel_up_steps,
      wheel_down_steps);
  const i32 prev_scroll = action_press_count(
      window_,
      input_bindings_,
      LogicalAction::PrevHotbarSlot,
      previous_action_active_[logical_action_index(LogicalAction::PrevHotbarSlot)],
      wheel_up_steps,
      wheel_down_steps);
  input.hotbar_scroll_steps = static_cast<i8>(std::clamp(next_scroll - prev_scroll, -127, 127));
  accumulated_scroll_y_ = 0.0;

  for (usize action_index = 0; action_index < previous_action_active_.size(); ++action_index) {
    previous_action_active_[action_index] = action_hold_active(
        window_,
        input_bindings_,
        static_cast<LogicalAction>(action_index));
  }

  f64 cursor_x = 0.0;
  f64 cursor_y = 0.0;
  glfwGetCursorPos(window_, &cursor_x, &cursor_y);
  input.cursor_x = static_cast<f32>(cursor_x);
  input.cursor_y = static_cast<f32>(cursor_y);
  if (cursor_captured_ && has_last_cursor_) {
    input.mouse_delta_x = static_cast<f32>(cursor_x - last_cursor_x_);
    input.mouse_delta_y = static_cast<f32>(cursor_y - last_cursor_y_);
  }
  last_cursor_x_ = cursor_x;
  last_cursor_y_ = cursor_y;
  has_last_cursor_ = true;
  previous_left_mouse_down_ = left_mouse_down;
  previous_right_mouse_down_ = right_mouse_down;
  previous_middle_mouse_down_ = middle_mouse_down;
  previous_backspace_down_ = backspace_down;
  previous_enter_down_ = enter_down;
  previous_left_arrow_down_ = left_arrow_down;
  previous_right_arrow_down_ = right_arrow_down;
  previous_up_arrow_down_ = up_arrow_down;
  previous_down_arrow_down_ = down_arrow_down;
  previous_delete_down_ = delete_down;
  previous_copy_shortcut_down_ = copy_shortcut_down;
  previous_paste_shortcut_down_ = paste_shortcut_down;
  previous_cut_shortcut_down_ = cut_shortcut_down;
  previous_select_all_shortcut_down_ = select_all_shortcut_down;
  return input;
}

std::string OpenGLWindow::clipboard_text() const {
  if (window_ == nullptr) {
    return {};
  }
  const char* text = glfwGetClipboardString(window_);
  return text == nullptr ? std::string{} : std::string{text};
}

void OpenGLWindow::set_clipboard_text(std::string_view text) {
  if (window_ == nullptr) {
    return;
  }
  const std::string owned{text};
  glfwSetClipboardString(window_, owned.c_str());
}

void OpenGLWindow::set_input_bindings(InputBindings bindings) noexcept {
  input_bindings_ = std::move(bindings);
  previous_action_active_.fill(false);
}

void OpenGLWindow::set_cursor_captured(bool captured) noexcept {
  if (window_ == nullptr || cursor_captured_ == captured) {
    return;
  }
  cursor_captured_ = captured;
  glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  if (captured && glfwRawMouseMotionSupported() == GLFW_TRUE) {
    glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }
  has_last_cursor_ = false;
}

void OpenGLWindow::set_vsync(bool enabled) noexcept {
  if (window_ == nullptr) {
    return;
  }
  glfwSwapInterval(enabled ? 1 : 0);
}

void OpenGLWindow::accumulate_scroll(f64 y_offset) noexcept {
  accumulated_scroll_y_ += y_offset;
}

void OpenGLWindow::append_text_input(u32 codepoint) {
  if (codepoint == 0U || codepoint > 0x10FFFFU ||
      (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
    return;
  }

  if (codepoint <= 0x7FU) {
    pending_text_input_.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FFU) {
    pending_text_input_.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
    pending_text_input_.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else if (codepoint <= 0xFFFFU) {
    pending_text_input_.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
    pending_text_input_.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    pending_text_input_.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else {
    pending_text_input_.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
    pending_text_input_.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
    pending_text_input_.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    pending_text_input_.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  }
}

FramebufferSize OpenGLWindow::framebuffer_size() const {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
  return {
      static_cast<u32>(width < 0 ? 0 : width),
      static_cast<u32>(height < 0 ? 0 : height)};
}

WindowSize OpenGLWindow::window_size() const {
  int width = 0;
  int height = 0;
  glfwGetWindowSize(window_, &width, &height);
  return {
      static_cast<u32>(width < 0 ? 0 : width),
      static_cast<u32>(height < 0 ? 0 : height)};
}

void OpenGLWindow::swap_buffers() {
  glfwSwapBuffers(window_);
}

void OpenGLWindow::poll_events() {
  glfwPollEvents();
}

void OpenGLWindow::clear_current_context() noexcept {
  glfwMakeContextCurrent(nullptr);
}

void OpenGLWindow::destroy() {
  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
}

} // namespace arv::platform
