#include "arv/core/fixed_timestep.hpp"

#include <gtest/gtest.h>

TEST(FixedTimestepTests, AccumulatesUntilOneTickIsAvailable) {
  arv::core::FixedTimestepAccumulator accumulator(60);
  const arv::Seconds half_tick = accumulator.tick_duration() * 0.5F;

  accumulator.add_time(half_tick);
  EXPECT_FALSE(accumulator.consume_tick());

  accumulator.add_time(half_tick);
  EXPECT_TRUE(accumulator.consume_tick());
  EXPECT_FALSE(accumulator.consume_tick());
}

TEST(FixedTimestepTests, ConsumesMultipleTicksAndLeavesRemainder) {
  arv::core::FixedTimestepAccumulator accumulator(20);

  accumulator.add_time(accumulator.tick_duration() * 2.25F);

  EXPECT_TRUE(accumulator.consume_tick());
  EXPECT_TRUE(accumulator.consume_tick());
  EXPECT_FALSE(accumulator.consume_tick());
  EXPECT_NEAR(accumulator.interpolation_alpha(), 0.25F, 0.0001F);
}
