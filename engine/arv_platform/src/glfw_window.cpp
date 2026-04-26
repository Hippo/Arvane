#include "arv/platform/glfw_window.hpp"

#include <utility>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace arv::platform {

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
      last_cursor_y_(std::exchange(other.last_cursor_y_, 0.0)) {
}

OpenGLWindow& OpenGLWindow::operator=(OpenGLWindow&& other) noexcept {
  if (this != &other) {
    destroy();
    window_ = std::exchange(other.window_, nullptr);
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
  input.jump = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
  input.move_down = glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
  input.fast = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
               glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

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
