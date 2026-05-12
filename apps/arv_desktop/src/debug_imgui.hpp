#pragma once

#include "arv/core/types.hpp"
#include "arv/core/time.hpp"
#include "arv/platform/glfw_window.hpp"

#include <imgui.h>

#include <vector>

struct GLFWwindow;

namespace arv::desktop {

inline constexpr ImTextureID DebugImGuiFontTextureID = static_cast<ImTextureID>(1U);

struct DebugImGuiFontAtlas {
  std::vector<unsigned char> rgba_pixels;
  i32 width{};
  i32 height{};
};

struct DebugImGuiDrawCommand {
  ImVec4 clip_rect{};
  ImTextureID texture_id{};
  u32 elem_count{};
  u32 idx_offset{};
  i32 vtx_offset{};
};

struct DebugImGuiDrawList {
  std::vector<ImDrawVert> vertices;
  std::vector<ImDrawIdx> indices;
  std::vector<DebugImGuiDrawCommand> commands;
};

struct DebugImGuiDrawData {
  ImVec2 display_pos{};
  ImVec2 display_size{};
  ImVec2 framebuffer_scale{1.0F, 1.0F};
  std::vector<DebugImGuiDrawList> command_lists;

  [[nodiscard]] bool empty() const noexcept {
    return command_lists.empty();
  }
};

class DebugImGuiLayer {
public:
  DebugImGuiLayer() = default;
  DebugImGuiLayer(const DebugImGuiLayer&) = delete;
  DebugImGuiLayer& operator=(const DebugImGuiLayer&) = delete;
  ~DebugImGuiLayer();

  [[nodiscard]] bool initialize(GLFWwindow* window);
  void shutdown();
  [[nodiscard]] DebugImGuiFontAtlas font_atlas() const;
  void begin_frame(
      GLFWwindow* window,
      const platform::InputSnapshot& input,
      platform::FramebufferSize framebuffer,
      Seconds delta_seconds);
  [[nodiscard]] DebugImGuiDrawData capture_draw_data();

private:
  void update_input(
      GLFWwindow* window,
      const platform::InputSnapshot& input,
      platform::FramebufferSize framebuffer,
      Seconds delta_seconds);

  bool initialized_{};
};

class DebugImGuiOpenGLRenderer {
public:
  DebugImGuiOpenGLRenderer() = default;
  DebugImGuiOpenGLRenderer(const DebugImGuiOpenGLRenderer&) = delete;
  DebugImGuiOpenGLRenderer& operator=(const DebugImGuiOpenGLRenderer&) = delete;
  ~DebugImGuiOpenGLRenderer();

  [[nodiscard]] bool initialize(const DebugImGuiFontAtlas& font_atlas);
  void shutdown();
  void render(const DebugImGuiDrawData& draw_data);

private:
  [[nodiscard]] bool create_device_objects(const DebugImGuiFontAtlas& font_atlas);
  void destroy_device_objects();

  bool initialized_{};
  u32 unknown_texture_warning_count_{};
  unsigned int font_texture_{};
  unsigned int shader_program_{};
  unsigned int vertex_shader_{};
  unsigned int fragment_shader_{};
  int texture_uniform_{-1};
  int projection_uniform_{-1};
  unsigned int vertex_array_{};
  unsigned int vertex_buffer_{};
  unsigned int index_buffer_{};
};

} // namespace arv::desktop
