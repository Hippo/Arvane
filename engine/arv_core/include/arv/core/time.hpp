#pragma once

#include "arv/core/types.hpp"

namespace arv {

using Seconds = f32;
using Milliseconds = u32;
using TickIndex = u64;
using TickRate = u32;

inline constexpr TickRate DefaultBaseTps = 60;

[[nodiscard]] constexpr Seconds tick_duration_seconds(TickRate tick_rate) noexcept {
  return tick_rate == 0 ? 0.0F : 1.0F / static_cast<Seconds>(tick_rate);
}

} // namespace arv
