#include "arv/platform/glfw_window.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace arv::platform {
namespace {

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
      input_bindings_(std::move(other.input_bindings_)),
      previous_action_active_(other.previous_action_active_) {
  other.previous_action_active_.fill(false);
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
    input_bindings_ = std::move(other.input_bindings_);
    previous_action_active_ = other.previous_action_active_;
    other.previous_action_active_.fill(false);
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
  window.make_context_current();
  glfwSetInputMode(raw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
    glfwSetInputMode(raw_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }

  if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) {
    return Expected<OpenGLWindow, PlatformError>::failure(PlatformError{"failed to load OpenGL functions"});
  }

  glfwSetWindowUserPointer(raw_window, &window);
  glfwSetScrollCallback(raw_window, glfw_scroll_callback);
  glfwSwapInterval(1);
  return Expected<OpenGLWindow, PlatformError>::success(std::move(window));
}

void OpenGLWindow::make_context_current() {
  glfwMakeContextCurrent(window_);
}

bool OpenGLWindow::should_close() const {
  return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void OpenGLWindow::request_close() {
  glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

InputSnapshot OpenGLWindow::poll_input() {
  InputSnapshot input;
  input.close_requested = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
  const i32 wheel_up_steps = static_cast<i32>(std::max(accumulated_scroll_y_, 0.0));
  const i32 wheel_down_steps = static_cast<i32>(std::max(-accumulated_scroll_y_, 0.0));

  input.move_forward = action_hold_active(window_, input_bindings_, LogicalAction::MoveForward);
  input.move_backward = action_hold_active(window_, input_bindings_, LogicalAction::MoveBackward);
  input.move_left = action_hold_active(window_, input_bindings_, LogicalAction::MoveLeft);
  input.move_right = action_hold_active(window_, input_bindings_, LogicalAction::MoveRight);
  input.jump_held = action_hold_active(window_, input_bindings_, LogicalAction::Jump);
  input.sneak_held = action_hold_active(window_, input_bindings_, LogicalAction::Sneak);
  input.sprint_held = action_hold_active(window_, input_bindings_, LogicalAction::Sprint);
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
  input.toggle_fly_pressed = action_press_count(
                                 window_,
                                 input_bindings_,
                                 LogicalAction::ToggleFly,
                                 previous_action_active_[logical_action_index(LogicalAction::ToggleFly)],
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
  if (has_last_cursor_) {
    input.mouse_delta_x = static_cast<f32>(cursor_x - last_cursor_x_);
    input.mouse_delta_y = static_cast<f32>(cursor_y - last_cursor_y_);
  }
  last_cursor_x_ = cursor_x;
  last_cursor_y_ = cursor_y;
  has_last_cursor_ = true;
  return input;
}

void OpenGLWindow::set_input_bindings(InputBindings bindings) noexcept {
  input_bindings_ = std::move(bindings);
  previous_action_active_.fill(false);
}

void OpenGLWindow::accumulate_scroll(f64 y_offset) noexcept {
  accumulated_scroll_y_ += y_offset;
}

FramebufferSize OpenGLWindow::framebuffer_size() const {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
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

void OpenGLWindow::destroy() {
  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
}

} // namespace arv::platform
