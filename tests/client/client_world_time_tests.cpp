#include "arv/client/client_world_time.hpp"

#include <gtest/gtest.h>

TEST(ClientWorldTimeTests, BuildsWorldClockFromTimePacketAndClampsDayLengthTicks) {
  arv::protocol::ClientboundWorldTime time;
  time.world_id = arv::WorldID{5};
  time.world_tick = arv::TickIndex{42};
  time.day_time_tick = 1234U;
  time.day_length_ticks = 0U;
  time.day_length_seconds = 600.0F;
  time.time_scale = 1.5F;
  time.daylight_cycle = false;

  const arv::world::WorldClock clock =
      arv::client::world_clock_from_time_packet(time);

  EXPECT_EQ(clock.world_tick, arv::TickIndex{42});
  EXPECT_EQ(clock.day_time_tick, 1234U);
  EXPECT_EQ(clock.day_length_ticks, 1U);
  EXPECT_EQ(clock.day_length_seconds, 600.0F);
  EXPECT_EQ(clock.time_scale, 1.5F);
  EXPECT_FALSE(clock.daylight_cycle);
  EXPECT_EQ(clock.day_time_accumulator, 0.0);
}
