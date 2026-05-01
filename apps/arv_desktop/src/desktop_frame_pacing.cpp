#include "desktop_frame_pacing.hpp"

#include "arv/core/profiling.hpp"

#include <thread>

namespace arv::desktop {

void pace_frame(
    const io::RenderConfig& render,
    std::chrono::steady_clock::time_point frame_start) {
  if (!render.frame_pacing_enabled || render.frame_pacing_target_fps == 0U) {
    return;
  }

  ARV_PROFILE_ZONE("GPU wait time");
  const auto target_frame =
      std::chrono::duration<double>(1.0 / static_cast<double>(render.frame_pacing_target_fps));
  const auto target_end =
      frame_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(target_frame);
  const auto spin_margin = std::chrono::microseconds(render.frame_pacing_spin_us);

  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (now + spin_margin >= target_end) {
      break;
    }
    std::this_thread::sleep_until(target_end - spin_margin);
  }

  while (std::chrono::steady_clock::now() < target_end) {
  }
}

} // namespace arv::desktop
