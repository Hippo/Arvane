#pragma once

#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"

namespace arv::world {

inline constexpr Seconds DefaultDayLengthSeconds = 1200.0F;
inline constexpr f32 DefaultStartDayFraction = 0.25F;
inline constexpr f32 DefaultTimeScale = 1.0F;

struct WorldClock {
  TickIndex world_tick{};
  u32 day_time_tick{};
  u32 day_length_ticks{};
  Seconds day_length_seconds{DefaultDayLengthSeconds};
  double day_time_accumulator{};
  f32 time_scale{DefaultTimeScale};
  bool daylight_cycle{true};
};

struct ClockTime24 {
  u32 hour{};
  u32 minute{};
};

struct CelestialState {
  f32 day_fraction{};
  Vec3f sun_direction{0.0F, -1.0F, 0.0F};
  Vec3f moon_direction{0.0F, 1.0F, 0.0F};
  f32 sun_height{-1.0F};
  f32 moon_height{1.0F};
  f32 daylight_strength{0.08F};
  f32 moonlight_strength{0.16F};
  f32 star_visibility{1.0F};
  Vec3f daylight_color{1.0F, 0.96F, 0.88F};
  Vec3f ambient_color{0.025F, 0.035F, 0.075F};
  Vec3f fog_color{0.025F, 0.035F, 0.08F};
};

[[nodiscard]] u32 day_length_ticks_from_seconds(
    Seconds day_length_seconds,
    TickRate fixed_tick_rate) noexcept;

[[nodiscard]] WorldClock make_world_clock(
    TickRate fixed_tick_rate = DefaultBaseTps,
    Seconds day_length_seconds = DefaultDayLengthSeconds,
    f32 start_day_fraction = DefaultStartDayFraction,
    bool daylight_cycle = true,
    f32 time_scale = DefaultTimeScale) noexcept;

void retime_world_clock(WorldClock& clock, TickRate fixed_tick_rate) noexcept;
void tick_world_clock(WorldClock& clock) noexcept;

[[nodiscard]] f32 day_fraction(const WorldClock& clock) noexcept;
[[nodiscard]] Seconds day_time_seconds(const WorldClock& clock) noexcept;
[[nodiscard]] ClockTime24 to_clock_time_24(f32 day_fraction) noexcept;
[[nodiscard]] u32 day_tick_from_hour_minute(
    u32 hour,
    u32 minute,
    u32 day_length_ticks) noexcept;

void set_day_time_tick(WorldClock& clock, u32 day_time_tick) noexcept;
void set_day_fraction(WorldClock& clock, f32 day_fraction) noexcept;
void add_day_ticks(WorldClock& clock, i64 delta_ticks) noexcept;

[[nodiscard]] CelestialState build_celestial_state(f32 day_fraction) noexcept;
[[nodiscard]] CelestialState build_celestial_state(const WorldClock& clock) noexcept;

} // namespace arv::world
