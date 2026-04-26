#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <string>

struct GLFWwindow;

namespace arv::platform {

struct PlatformError {
  std::string message{};
};

struct WindowConfig {
  u32 width{1280};
  u32 height{720};
  std::string title{"Arvane"};
};

struct FramebufferSize {
  u32 width{};
  u32 height{};
};

struct InputSnapshot {
  bool close_requested{};
  bool move_forward{};
  bool move_backward{};
  bool move_left{};
  bool move_right{};
  bool jump{};
  bool move_down{};
  bool fast{};
  f32 mouse_delta_x{};
  f32 mouse_delta_y{};
};

class GlfwLibrary {
public:
  GlfwLibrary();

  GlfwLibrary(const GlfwLibrary&) = delete;
  GlfwLibrary& operator=(const GlfwLibrary&) = delete;

  ~GlfwLibrary();

  [[nodiscard]] bool initialized() const noexcept {
    return initialized_;
  }

private:
  bool initialized_{};
};

class OpenGLWindow {
public:
  OpenGLWindow() = default;

  OpenGLWindow(const OpenGLWindow&) = delete;
  OpenGLWindow& operator=(const OpenGLWindow&) = delete;

  OpenGLWindow(OpenGLWindow&& other) noexcept;

  OpenGLWindow& operator=(OpenGLWindow&& other) noexcept;

  ~OpenGLWindow();

  static Expected<OpenGLWindow, PlatformError> create(const WindowConfig& config);

  void make_context_current();

  [[nodiscard]] bool should_close() const;

  void request_close();

  InputSnapshot poll_input();

  [[nodiscard]] FramebufferSize framebuffer_size() const;

  void swap_buffers();

  static void poll_events();

private:
  void destroy();

  GLFWwindow* window_{};
  bool has_last_cursor_{};
  f64 last_cursor_x_{};
  f64 last_cursor_y_{};
};

} // namespace arv::platform
