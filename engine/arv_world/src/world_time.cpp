#include "arv/world/world_time.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace arv::world {
namespace {

[[nodiscard]] f32 clamp01(f32 value) noexcept {
  return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] f32 smooth01(f32 value) noexcept {
  const f32 x = clamp01(value);
  return x * x * (3.0F - 2.0F * x);
}

[[nodiscard]] f32 mix(f32 from, f32 to, f32 amount) noexcept {
  return from + (to - from) * amount;
}

[[nodiscard]] Vec3f mix(Vec3f from, Vec3f to, f32 amount) noexcept {
  return {
      mix(from.x, to.x, amount),
      mix(from.y, to.y, amount),
      mix(from.z, to.z, amount)};
}

[[nodiscard]] f32 wrap_fraction(f32 value) noexcept {
  if (!std::isfinite(value)) {
    return 0.0F;
  }
  f32 wrapped = std::fmod(value, 1.0F);
  if (wrapped < 0.0F) {
    wrapped += 1.0F;
  }
  return wrapped;
}

[[nodiscard]] u32 wrapped_tick(i64 tick, u32 day_length_ticks) noexcept {
  if (day_length_ticks == 0U) {
    return 0U;
  }
  const i64 length = static_cast<i64>(day_length_ticks);
  i64 wrapped = tick % length;
  if (wrapped < 0) {
    wrapped += length;
  }
  return static_cast<u32>(wrapped);
}

} // namespace

u32 day_length_ticks_from_seconds(
    Seconds day_length_seconds,
    TickRate fixed_tick_rate) noexcept {
  if (!std::isfinite(day_length_seconds)) {
    day_length_seconds = DefaultDayLengthSeconds;
  }
  const f64 seconds = std::max<f64>(static_cast<f64>(day_length_seconds), 1.0);
  const f64 tick_rate = std::max<f64>(static_cast<f64>(fixed_tick_rate), 1.0);
  const f64 ticks = std::round(seconds * tick_rate);
  return static_cast<u32>(std::clamp<f64>(ticks, 1.0, static_cast<f64>(~u32{0})));
}

WorldClock make_world_clock(
    TickRate fixed_tick_rate,
    Seconds day_length_seconds,
    f32 start_day_fraction,
    bool daylight_cycle,
    f32 time_scale) noexcept {
  WorldClock clock;
  clock.day_length_seconds =
      std::isfinite(day_length_seconds) ? std::max(day_length_seconds, 1.0F)
                                        : DefaultDayLengthSeconds;
  clock.day_length_ticks =
      day_length_ticks_from_seconds(clock.day_length_seconds, fixed_tick_rate);
  clock.daylight_cycle = daylight_cycle;
  clock.time_scale = std::isfinite(time_scale) ? std::max(time_scale, 0.0F)
                                               : DefaultTimeScale;
  set_day_fraction(clock, start_day_fraction);
  return clock;
}

void retime_world_clock(WorldClock& clock, TickRate fixed_tick_rate) noexcept {
  const f32 fraction = day_fraction(clock);
  clock.day_length_seconds =
      std::isfinite(clock.day_length_seconds)
          ? std::max(clock.day_length_seconds, 1.0F)
          : DefaultDayLengthSeconds;
  clock.day_length_ticks =
      day_length_ticks_from_seconds(clock.day_length_seconds, fixed_tick_rate);
  clock.time_scale = std::isfinite(clock.time_scale) ? std::max(clock.time_scale, 0.0F)
                                                     : DefaultTimeScale;
  set_day_fraction(clock, fraction);
}

void tick_world_clock(WorldClock& clock) noexcept {
  ++clock.world_tick;

  if (!clock.daylight_cycle || clock.day_length_ticks == 0U) {
    return;
  }

  if (!std::isfinite(clock.time_scale)) {
    clock.time_scale = DefaultTimeScale;
  }
  clock.time_scale = std::max(clock.time_scale, 0.0F);
  clock.day_time_accumulator += static_cast<double>(clock.time_scale);

  while (clock.day_time_accumulator >= 1.0) {
    clock.day_time_tick =
        (clock.day_time_tick + 1U) % clock.day_length_ticks;
    clock.day_time_accumulator -= 1.0;
  }
}

f32 day_fraction(const WorldClock& clock) noexcept {
  if (clock.day_length_ticks == 0U) {
    return 0.0F;
  }
  return static_cast<f32>(clock.day_time_tick) /
         static_cast<f32>(clock.day_length_ticks);
}

Seconds day_time_seconds(const WorldClock& clock) noexcept {
  return day_fraction(clock) * std::max(clock.day_length_seconds, 1.0F);
}

ClockTime24 to_clock_time_24(f32 day_fraction_value) noexcept {
  const f32 wrapped = wrap_fraction(day_fraction_value);
  const auto total_minutes = static_cast<u32>(
      std::floor(wrapped * 24.0F * 60.0F)) % (24U * 60U);
  return {total_minutes / 60U, total_minutes % 60U};
}

u32 day_tick_from_hour_minute(
    u32 hour,
    u32 minute,
    u32 day_length_ticks) noexcept {
  if (day_length_ticks == 0U) {
    return 0U;
  }
  hour %= 24U;
  minute %= 60U;
  const u32 total_minutes = hour * 60U + minute;
  const f64 fraction =
      static_cast<f64>(total_minutes) / static_cast<f64>(24U * 60U);
  return static_cast<u32>(
      std::round(fraction * static_cast<f64>(day_length_ticks))) %
      day_length_ticks;
}

void set_day_time_tick(WorldClock& clock, u32 day_time_tick) noexcept {
  clock.day_time_tick =
      clock.day_length_ticks == 0U ? 0U : day_time_tick % clock.day_length_ticks;
  clock.day_time_accumulator = 0.0;
}

void set_day_fraction(WorldClock& clock, f32 day_fraction_value) noexcept {
  if (clock.day_length_ticks == 0U) {
    clock.day_time_tick = 0U;
    clock.day_time_accumulator = 0.0;
    return;
  }

  const f32 wrapped = wrap_fraction(day_fraction_value);
  const auto tick = static_cast<u32>(
      std::round(static_cast<f64>(wrapped) *
                 static_cast<f64>(clock.day_length_ticks))) %
      clock.day_length_ticks;
  set_day_time_tick(clock, tick);
}

void add_day_ticks(WorldClock& clock, i64 delta_ticks) noexcept {
  set_day_time_tick(
      clock,
      wrapped_tick(static_cast<i64>(clock.day_time_tick) + delta_ticks,
                   clock.day_length_ticks));
}

CelestialState build_celestial_state(f32 day_fraction_value) noexcept {
  CelestialState out;
  out.day_fraction = wrap_fraction(day_fraction_value);

  const f32 angle = out.day_fraction * std::numbers::pi_v<f32> * 2.0F;
  const Vec3f sun_direction = normalize_or_zero({
      0.0F,
      -std::cos(angle),
      std::sin(angle),
  });
  out.sun_direction = sun_direction;
  out.moon_direction = sun_direction * -1.0F;
  out.sun_height = out.sun_direction.y;
  out.moon_height = out.moon_direction.y;

  const f32 daylight = smooth01((out.sun_height + 0.08F) / 0.24F);
  const f32 high_sun = smooth01((out.sun_height + 0.04F) / 0.42F);
  const f32 horizon_warmth =
      (1.0F - high_sun) * smooth01((out.sun_height + 0.12F) / 0.22F);
  out.daylight_strength = mix(0.08F, 1.0F, daylight);
  out.moonlight_strength =
      mix(0.02F, 0.16F, smooth01((out.moon_height + 0.05F) / 0.45F));
  out.star_visibility = 1.0F - smooth01((out.sun_height + 0.18F) / 0.20F);

  const Vec3f warm_sun{1.0F, 0.58F, 0.32F};
  const Vec3f noon_sun{1.0F, 0.96F, 0.88F};
  out.daylight_color = mix(mix(noon_sun, warm_sun, horizon_warmth),
                           noon_sun,
                           high_sun);

  const f32 day_amount = smooth01((out.sun_height + 0.10F) / 0.32F);
  out.ambient_color = mix(
      {0.025F, 0.035F, 0.075F},
      {0.20F, 0.24F, 0.30F},
      day_amount);
  out.fog_color = mix(
      {0.025F, 0.035F, 0.08F},
      {0.55F, 0.68F, 0.86F},
      day_amount);
  out.fog_color = mix(out.fog_color, {0.82F, 0.43F, 0.24F}, horizon_warmth * 0.35F);
  return out;
}

CelestialState build_celestial_state(const WorldClock& clock) noexcept {
  return build_celestial_state(day_fraction(clock));
}

} // namespace arv::world
