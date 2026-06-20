#include "arv/client/client_world_time.hpp"

#include <algorithm>

namespace arv::client {

world::WorldClock world_clock_from_time_packet(
    const protocol::ClientboundWorldTime& time) noexcept {
  world::WorldClock clock;
  clock.world_tick = time.world_tick;
  clock.day_time_tick = time.day_time_tick;
  clock.day_length_ticks = std::max<u32>(time.day_length_ticks, 1U);
  clock.day_length_seconds = time.day_length_seconds;
  clock.time_scale = time.time_scale;
  clock.daylight_cycle = time.daylight_cycle;
  return clock;
}

} // namespace arv::client
