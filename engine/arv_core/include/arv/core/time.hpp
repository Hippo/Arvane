#pragma once

#include "arv/core/types.hpp"

#include <chrono>

namespace arv {

using Seconds = f32;
using Milliseconds = u32;
using TickIndex = u64;
using TickRate = u32;

inline constexpr TickRate DefaultBaseTps = 60;

[[nodiscard]] constexpr Seconds tick_duration_seconds(TickRate tick_rate) noexcept {
  return tick_rate == 0 ? 0.0F : 1.0F / static_cast<Seconds>(tick_rate);
}

template<typename Clock, typename Duration>
[[nodiscard]] bool cooldown_due(
    std::chrono::time_point<Clock> last,
    std::chrono::time_point<Clock> now,
    Duration cooldown) noexcept {
  return last.time_since_epoch().count() == 0 || now - last >= cooldown;
}

} // namespace arv
