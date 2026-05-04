#include "arv/world/world_time.hpp"

#include <gtest/gtest.h>

TEST(WorldTimeTests, TickAdvancesWorldTickEvenWhenDaylightCycleIsFrozen) {
  arv::world::WorldClock clock = arv::world::make_world_clock(10, 10.0F, 0.25F);
  clock.daylight_cycle = false;
  const arv::u32 start_day_tick = clock.day_time_tick;

  arv::world::tick_world_clock(clock);

  EXPECT_EQ(clock.world_tick, 1U);
  EXPECT_EQ(clock.day_time_tick, start_day_tick);
}

TEST(WorldTimeTests, TimeScaleUsesAccumulatorForFractionalDayTicks) {
  arv::world::WorldClock clock = arv::world::make_world_clock(10, 10.0F, 0.0F);
  clock.time_scale = 0.5F;

  arv::world::tick_world_clock(clock);
  EXPECT_EQ(clock.day_time_tick, 0U);

  arv::world::tick_world_clock(clock);
  EXPECT_EQ(clock.day_time_tick, 1U);
  EXPECT_EQ(clock.world_tick, 2U);
}

TEST(WorldTimeTests, DayFractionMapsToExpectedCelestialHeights) {
  const arv::world::CelestialState midnight = arv::world::build_celestial_state(0.0F);
  const arv::world::CelestialState sunrise = arv::world::build_celestial_state(0.25F);
  const arv::world::CelestialState noon = arv::world::build_celestial_state(0.5F);
  const arv::world::CelestialState sunset = arv::world::build_celestial_state(0.75F);

  EXPECT_LT(midnight.sun_height, -0.99F);
  EXPECT_NEAR(sunrise.sun_height, 0.0F, 0.001F);
  EXPECT_GT(noon.sun_height, 0.99F);
  EXPECT_NEAR(sunset.sun_height, 0.0F, 0.001F);
  EXPECT_GT(midnight.star_visibility, noon.star_visibility);
}

TEST(WorldTimeTests, ClockTimeConvertsFromDayFraction) {
  const arv::world::ClockTime24 time = arv::world::to_clock_time_24(0.5F);

  EXPECT_EQ(time.hour, 12U);
  EXPECT_EQ(time.minute, 0U);
}
