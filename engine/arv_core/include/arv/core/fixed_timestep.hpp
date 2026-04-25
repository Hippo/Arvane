#pragma once

#include "arv/core/time.hpp"

namespace arv::core {

class FixedTimestepAccumulator {
public:
  explicit constexpr FixedTimestepAccumulator(TickRate tick_rate = DefaultBaseTps)
      : tick_rate_(tick_rate), tick_duration_(tick_duration_seconds(tick_rate)) {}

  [[nodiscard]] constexpr TickRate tick_rate() const noexcept {
    return tick_rate_;
  }

  [[nodiscard]] constexpr Seconds tick_duration() const noexcept {
    return tick_duration_;
  }

  [[nodiscard]] constexpr Seconds accumulated_seconds() const noexcept {
    return accumulated_;
  }

  constexpr void add_time(Seconds delta_seconds) noexcept {
    if (delta_seconds > 0.0F) {
      accumulated_ += delta_seconds;
    }
  }

  constexpr bool consume_tick() noexcept {
    if (tick_duration_ <= 0.0F || accumulated_ + Epsilon < tick_duration_) {
      return false;
    }
    accumulated_ -= tick_duration_;
    return true;
  }

  [[nodiscard]] constexpr f32 interpolation_alpha() const noexcept {
    if (tick_duration_ <= 0.0F) {
      return 0.0F;
    }
    return accumulated_ / tick_duration_;
  }

private:
  static constexpr Seconds Epsilon = 0.000001F;

  TickRate tick_rate_{DefaultBaseTps};
  Seconds tick_duration_{tick_duration_seconds(DefaultBaseTps)};
  Seconds accumulated_{};
};

} // namespace arv::core
