#pragma once

#include "arv/platform/input_bindings.hpp"

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <array>
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

struct RawInputEvent {
  InputBinding binding{};
};

struct InputSnapshot {
  bool close_requested{};
  bool move_forward{};
  bool move_backward{};
  bool move_left{};
  bool move_right{};
  bool jump_pressed{};
  bool jump_held{};
  bool sneak_held{};
  bool sprint_held{};
  bool primary_pressed{};
  bool primary_held{};
  bool secondary_pressed{};
  bool secondary_held{};
  bool toggle_fly_pressed{};
  bool open_inventory_pressed{};
  bool open_chat_pressed{};
  bool tab_menu_held{};
  bool pause_menu_pressed{};
  bool debug_menu_toggle_pressed{};
  std::optional<u8> hotbar_select{};
  i8 hotbar_scroll_steps{};
  bool mouse_left_pressed{};
  bool mouse_left_released{};
  bool mouse_left_held{};
  bool mouse_right_pressed{};
  bool mouse_right_released{};
  bool mouse_right_held{};
  f32 cursor_x{};
  f32 cursor_y{};
  f32 mouse_delta_x{};
  f32 mouse_delta_y{};
  f32 mouse_wheel_y{};
  std::string text_input{};
  bool text_backspace_pressed{};
  bool text_submit_pressed{};
  bool text_cancel_pressed{};
  std::optional<RawInputEvent> raw_input{};
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
  std::string pending_text_input_{};
  InputBindings input_bindings_{default_input_bindings()};
  std::array<bool, static_cast<usize>(LogicalAction::Count)> previous_action_active_{};
  std::array<bool, 64> previous_capture_key_down_{};
};

} // namespace arv::platform
