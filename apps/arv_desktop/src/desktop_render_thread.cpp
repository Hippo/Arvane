#include "desktop_render_thread.hpp"

#include "arv/core/log.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"

#include <chrono>
#include <exception>
#include <utility>

namespace arv::desktop {

DesktopRenderThread::DesktopRenderThread(platform::OpenGLWindow& window) : window_(window),
                                                                           render_target_(renderer_),
                                                                           render_commands_(render_target_, render_thread_) {
}

DesktopRenderThread::~DesktopRenderThread() {
  shutdown();
}

bool DesktopRenderThread::start(bool vsync_enabled, DebugImGuiFontAtlas font_atlas) {
  if (started_) {
    return true;
  }
  if (!render_thread_.start()) {
    core::error("render", "failed to start render thread");
    return false;
  }

  try {
    const bool initialized = render_thread_.call(
        [this, vsync_enabled, font_atlas = std::move(font_atlas)]() mutable {
          window_.make_context_current();
          if (!window_.load_gl_functions()) {
            core::error("render", "failed to load OpenGL functions on render thread");
            platform::OpenGLWindow::clear_current_context();
            return false;
          }
          window_.set_vsync(vsync_enabled);
          if (!debug_renderer_.initialize(font_atlas)) {
            core::error("debug_ui", "failed to initialize ImGui OpenGL renderer");
            debug_renderer_.shutdown();
            platform::OpenGLWindow::clear_current_context();
            return false;
          }
          return true;
        });
    if (!initialized) {
      render_thread_.request_stop();
      render_thread_.join();
      return false;
    }
  } catch (const std::exception& exception) {
    core::error("render", "render thread startup failed: {}", exception.what());
    render_thread_.request_stop();
    render_thread_.join();
    return false;
  } catch (...) {
    core::error("render", "render thread startup failed with an unknown exception");
    render_thread_.request_stop();
    render_thread_.join();
    return false;
  }

  started_ = true;
  return true;
}

void DesktopRenderThread::shutdown() {
  if (!started_) {
    return;
  }

  try {
    render_thread_.call([this] {
      render_target_.begin_shutdown();
      debug_renderer_.shutdown();
      platform::OpenGLWindow::clear_current_context();
    });
  } catch (const std::exception& exception) {
    core::error("render", "render thread shutdown failed: {}", exception.what());
  } catch (...) {
    core::error("render", "render thread shutdown failed with an unknown exception");
  }

  render_thread_.request_stop();
  render_thread_.join();
  started_ = false;
}

void DesktopRenderThread::set_vsync(bool enabled) {
  if (!started_) {
    return;
  }
  try {
    render_thread_.call([this, enabled] {
      window_.set_vsync(enabled);
    });
  } catch (const std::exception& exception) {
    core::error("render", "failed to update vsync on render thread: {}", exception.what());
  }
}

bool DesktopRenderThread::present_frame(
    render::FramePacket frame,
    DebugImGuiDrawData debug_draw_data) {
  if (!started_) {
    return false;
  }

  try {
    render_thread_.call(
        [this,
         frame = std::move(frame),
         debug_draw_data = std::move(debug_draw_data)]() mutable {
          std::chrono::steady_clock::time_point render_submit_start{};
          if constexpr (core::profiling_enabled()) {
            render_submit_start = std::chrono::steady_clock::now();
          }

          render_target_.submit_frame(std::move(frame));
          render_target_.render();
          debug_renderer_.render(debug_draw_data);
          render_commands_.refresh_metrics_snapshot();

          if constexpr (core::profiling_enabled()) {
            const auto render_submit_end = std::chrono::steady_clock::now();
            core::record_thread_timing(
                core::PerfMetric::RenderSubmit,
                std::chrono::duration<double, std::milli>(
                    render_submit_end - render_submit_start)
                    .count(),
                "render");
          }

          ARV_PROFILE_ZONE("GPU wait time");
          std::chrono::steady_clock::time_point gpu_wait_start{};
          if constexpr (core::profiling_enabled()) {
            gpu_wait_start = std::chrono::steady_clock::now();
          }
          window_.swap_buffers();
          if constexpr (core::profiling_enabled()) {
            const auto gpu_wait_end = std::chrono::steady_clock::now();
            core::record_thread_timing(
                core::PerfMetric::GpuWaitTime,
                std::chrono::duration<double, std::milli>(
                    gpu_wait_end - gpu_wait_start)
                    .count(),
                "render");
          }
        });
  } catch (const std::exception& exception) {
    core::error("render", "render thread present failed: {}", exception.what());
    return false;
  } catch (...) {
    core::error("render", "render thread present failed with an unknown exception");
    return false;
  }
  return true;
}

} // namespace arv::desktop
