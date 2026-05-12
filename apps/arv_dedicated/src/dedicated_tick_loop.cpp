#include "dedicated_tick_loop.hpp"

#include "dedicated_signals.hpp"

#include "arv/core/fixed_timestep.hpp"
#include "arv/core/log.hpp"
#include "arv/sched/owner_thread.hpp"

#include <chrono>
#include <thread>

namespace arv::dedicated {

DedicatedTickLoopResult run_dedicated_tick_loop(
    server::ServerRuntime& server,
    u32 base_tps,
    u64 max_ticks) {
  sched::WorldThread world_thread;
  (void)world_thread.start();
  core::info("threading", "world_owner=world thread");
  DedicatedTickLoopResult result = world_thread.call([&server, base_tps, max_ticks] {
    server.voxel_world().bind_owner_thread();
    const auto sleep_duration = std::chrono::duration<Seconds>(
        tick_duration_seconds(base_tps));

    DedicatedTickLoopResult thread_result;
    while (!shutdown_requested() &&
           (max_ticks == 0 || thread_result.ticks_run < max_ticks)) {
      server.tick();
      ++thread_result.ticks_run;
      std::this_thread::sleep_for(sleep_duration);
    }

    thread_result.stopped_by_shutdown_request = shutdown_requested();
    thread_result.stopped_by_tick_limit =
        max_ticks != 0 && thread_result.ticks_run >= max_ticks;
    server.shutdown();
    return thread_result;
  });
  world_thread.request_stop();
  world_thread.join();
  return result;
}

} // namespace arv::dedicated
