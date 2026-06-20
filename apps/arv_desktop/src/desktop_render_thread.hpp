#pragma once

#include "debug_imgui.hpp"

#include "arv/platform/glfw_window.hpp"
#include "arv/render/opengl_render_commands.hpp"
#include "arv/render/opengl_renderer.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/sched/owner_thread.hpp"

namespace arv::desktop {

class DesktopRenderThread {
public:
  explicit DesktopRenderThread(platform::OpenGLWindow& window);
  DesktopRenderThread(const DesktopRenderThread&) = delete;
  DesktopRenderThread& operator=(const DesktopRenderThread&) = delete;
  ~DesktopRenderThread();

  [[nodiscard]] bool start(bool vsync_enabled, DebugImGuiFontAtlas font_atlas);
  void shutdown();

  [[nodiscard]] render::RenderCommandQueue& commands() noexcept {
    return render_commands_;
  }

  [[nodiscard]] const render::RenderCommandQueue& commands() const noexcept {
    return render_commands_;
  }

  void set_vsync(bool enabled);
  [[nodiscard]] bool present_frame(
      render::FramePacket frame,
      DebugImGuiDrawData debug_draw_data);

private:
  platform::OpenGLWindow& window_;
  sched::RenderThread render_thread_;
  render::OpenGLRenderer renderer_;
  render::OpenGLRenderCommandTarget render_target_;
  render::RenderCommandQueue render_commands_;
  DebugImGuiOpenGLRenderer debug_renderer_;
  bool started_{};
};

} // namespace arv::desktop
