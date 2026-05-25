#pragma once

#include "arv/core/types.hpp"

#include <chrono>
#include <cmath>
#include <limits>

namespace arv {

using Seconds = f32;
using Milliseconds = u32;
using TickIndex = u64;
using TickRate = u32;

inline constexpr TickRate DefaultBaseTps = 60;

[[nodiscard]] constexpr Seconds tick_duration_seconds(TickRate tick_rate) noexcept {
  return tick_rate == 0 ? 0.0F : 1.0F / static_cast<Seconds>(tick_rate);
}

[[nodiscard]] inline u32 seconds_to_ticks_ceil(
    Seconds seconds,
    TickRate tick_rate = DefaultBaseTps) noexcept {
  if (!std::isfinite(seconds) || seconds <= 0.0F || tick_rate == 0U) {
    return 0U;
  }
  const f32 ticks = std::ceil(seconds * static_cast<f32>(tick_rate));
  if (ticks >= static_cast<f32>(std::numeric_limits<u32>::max())) {
    return std::numeric_limits<u32>::max();
  }
  return static_cast<u32>(ticks);
}

template<typename Clock, typename Duration>
[[nodiscard]] bool cooldown_due(
    std::chrono::time_point<Clock> last,
    std::chrono::time_point<Clock> now,
    Duration cooldown) noexcept {
  return last.time_since_epoch().count() == 0 || now - last >= cooldown;
}

} // namespace arv
