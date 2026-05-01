#include "dedicated_tick_loop.hpp"

#include "dedicated_signals.hpp"

#include "arv/core/fixed_timestep.hpp"

#include <chrono>
#include <thread>

namespace arv::dedicated {

DedicatedTickLoopResult run_dedicated_tick_loop(
    server::ServerRuntime& server,
    u32 base_tps,
    u64 max_ticks) {
  const auto sleep_duration = std::chrono::duration<Seconds>(
      tick_duration_seconds(base_tps));

  DedicatedTickLoopResult result;
  while (!shutdown_requested() &&
         (max_ticks == 0 || result.ticks_run < max_ticks)) {
    server.tick();
    ++result.ticks_run;
    std::this_thread::sleep_for(sleep_duration);
  }

  result.stopped_by_shutdown_request = shutdown_requested();
  result.stopped_by_tick_limit =
      max_ticks != 0 && result.ticks_run >= max_ticks;
  return result;
}

} // namespace arv::dedicated
