#pragma once

#include "arv/platform/input_bindings.hpp"
#include "arv/platform/input_snapshot.hpp"

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <array>
#include <string>
#include <string_view>

struct GLFWwindow;

namespace arv::platform {

struct PlatformError {
  std::string message{};
};

struct WindowConfig {
  u32 width{1280};
  u32 height{720};
  std::string title{"Arvane"};
  bool vsync{false};
};

struct FramebufferSize {
  u32 width{};
  u32 height{};
};

struct WindowSize {
  u32 width{};
  u32 height{};
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
  [[nodiscard]] bool load_gl_functions() noexcept;

  [[nodiscard]] bool should_close() const;

  void request_close();

  InputSnapshot poll_input();
  [[nodiscard]] std::string clipboard_text() const;
  void set_clipboard_text(std::string_view text);
  void set_input_bindings(InputBindings bindings) noexcept;
  void set_cursor_captured(bool captured) noexcept;
  void set_vsync(bool enabled) noexcept;

  [[nodiscard]] FramebufferSize framebuffer_size() const;
  [[nodiscard]] WindowSize window_size() const;
  [[nodiscard]] GLFWwindow* native_handle() noexcept {
    return window_;
  }
  [[nodiscard]] const GLFWwindow* native_handle() const noexcept {
    return window_;
  }

  void swap_buffers();

  static void poll_events();
  static void clear_current_context() noexcept;

  void accumulate_scroll(f64 y_offset) noexcept;
  void append_text_input(u32 codepoint);

private:
  void destroy();

  GLFWwindow* window_{};
  bool has_last_cursor_{};
  f64 last_cursor_x_{};
  f64 last_cursor_y_{};
  f64 accumulated_scroll_y_{};
  bool cursor_captured_{true};
  bool previous_left_mouse_down_{};
  bool previous_right_mouse_down_{};
  bool previous_middle_mouse_down_{};
  bool previous_backspace_down_{};
  bool previous_enter_down_{};
  bool previous_left_arrow_down_{};
  bool previous_right_arrow_down_{};
  bool previous_up_arrow_down_{};
  bool previous_down_arrow_down_{};
  bool previous_delete_down_{};
  bool previous_copy_shortcut_down_{};
  bool previous_paste_shortcut_down_{};
  bool previous_cut_shortcut_down_{};
  bool previous_select_all_shortcut_down_{};
  std::string pending_text_input_{};
  InputBindings input_bindings_{default_input_bindings()};
  std::array<bool, static_cast<usize>(LogicalAction::Count)> previous_action_active_{};
  std::array<bool, 64> previous_capture_key_down_{};
};

} // namespace arv::platform
