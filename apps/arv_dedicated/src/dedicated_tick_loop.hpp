#pragma once

#include "arv/core/types.hpp"
#include "arv/server/server_runtime.hpp"

namespace arv::dedicated {

struct DedicatedTickLoopResult {
  u64 ticks_run{};
  bool stopped_by_tick_limit{};
  bool stopped_by_shutdown_request{};
};

[[nodiscard]] DedicatedTickLoopResult run_dedicated_tick_loop(
    server::ServerRuntime& server,
    u32 base_tps,
    u64 max_ticks);

} // namespace arv::dedicated
