#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <optional>
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
  bool jump_pressed{};
  bool jump_held{};
  bool move_down_held{};
  bool sprint_held{};
  bool primary_pressed{};
  bool primary_held{};
  bool secondary_pressed{};
  bool secondary_held{};
  bool toggle_fly_pressed{};
  std::optional<u8> hotbar_select{};
  i8 hotbar_scroll_steps{};
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

  void accumulate_scroll(f64 y_offset) noexcept;

private:
  void destroy();

  GLFWwindow* window_{};
  bool has_last_cursor_{};
  f64 last_cursor_x_{};
  f64 last_cursor_y_{};
  f64 accumulated_scroll_y_{};
  bool previous_jump_held_{};
  bool previous_primary_held_{};
  bool previous_secondary_held_{};
  bool previous_toggle_fly_held_{};
  bool previous_hotbar_keys_[9]{};
};

} // namespace arv::platform
