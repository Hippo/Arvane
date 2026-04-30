#pragma once

#include "arv/core/time.hpp"
#include "arv/platform/glfw_window.hpp"

#include <glad/glad.h>

struct GLFWwindow;

namespace arv::desktop {

class DebugImGuiLayer {
public:
  DebugImGuiLayer() = default;
  DebugImGuiLayer(const DebugImGuiLayer&) = delete;
  DebugImGuiLayer& operator=(const DebugImGuiLayer&) = delete;
  ~DebugImGuiLayer();

  [[nodiscard]] bool initialize(GLFWwindow* window);
  void shutdown();
  void begin_frame(
      GLFWwindow* window,
      const platform::InputSnapshot& input,
      platform::FramebufferSize framebuffer,
      Seconds delta_seconds);
  void render();

private:
  [[nodiscard]] bool create_device_objects();
  void destroy_device_objects();
  void update_input(
      GLFWwindow* window,
      const platform::InputSnapshot& input,
      platform::FramebufferSize framebuffer,
      Seconds delta_seconds);

  bool initialized_{};
  GLuint font_texture_{};
  GLuint shader_program_{};
  GLuint vertex_shader_{};
  GLuint fragment_shader_{};
  GLint texture_uniform_{-1};
  GLint projection_uniform_{-1};
  GLuint vertex_array_{};
  GLuint vertex_buffer_{};
  GLuint index_buffer_{};
};

} // namespace arv::desktop
