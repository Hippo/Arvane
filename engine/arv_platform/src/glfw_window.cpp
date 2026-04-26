#include "arv/platform/glfw_window.hpp"

#include <algorithm>
#include <utility>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace arv::platform {
namespace {

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
      previous_jump_held_(std::exchange(other.previous_jump_held_, false)),
      previous_primary_held_(std::exchange(other.previous_primary_held_, false)),
      previous_secondary_held_(std::exchange(other.previous_secondary_held_, false)),
      previous_toggle_fly_held_(std::exchange(other.previous_toggle_fly_held_, false)) {
  for (usize index = 0; index < 9U; ++index) {
    previous_hotbar_keys_[index] = std::exchange(other.previous_hotbar_keys_[index], false);
  }
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
    previous_jump_held_ = std::exchange(other.previous_jump_held_, false);
    previous_primary_held_ = std::exchange(other.previous_primary_held_, false);
    previous_secondary_held_ = std::exchange(other.previous_secondary_held_, false);
    previous_toggle_fly_held_ = std::exchange(other.previous_toggle_fly_held_, false);
    for (usize index = 0; index < 9U; ++index) {
      previous_hotbar_keys_[index] = std::exchange(other.previous_hotbar_keys_[index], false);
    }
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
  input.move_forward = glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS;
  input.move_backward = glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS;
  input.move_left = glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS;
  input.move_right = glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS;
  input.jump_held = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
  input.jump_pressed = input.jump_held && !previous_jump_held_;
  previous_jump_held_ = input.jump_held;
  input.move_down_held = glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                         glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
  input.sprint_held = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                      glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
  input.primary_held = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  input.primary_pressed = input.primary_held && !previous_primary_held_;
  previous_primary_held_ = input.primary_held;
  input.secondary_held = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  input.secondary_pressed = input.secondary_held && !previous_secondary_held_;
  previous_secondary_held_ = input.secondary_held;

  const bool toggle_fly_held = glfwGetKey(window_, GLFW_KEY_F) == GLFW_PRESS;
  input.toggle_fly_pressed = toggle_fly_held && !previous_toggle_fly_held_;
  previous_toggle_fly_held_ = toggle_fly_held;

  for (int key = 0; key < 9; ++key) {
    const bool held = glfwGetKey(window_, GLFW_KEY_1 + key) == GLFW_PRESS;
    if (held && !previous_hotbar_keys_[key]) {
      input.hotbar_select = static_cast<u8>(key);
    }
    previous_hotbar_keys_[key] = held;
  }
  input.hotbar_scroll_steps = static_cast<i8>(std::clamp(accumulated_scroll_y_, -127.0, 127.0));
  accumulated_scroll_y_ = 0.0;

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
