#pragma once

#include "arv/core/types.hpp"
#include "arv/io/config.hpp"

namespace arv::dedicated {

struct DedicatedLaunchOptions {
  io::ConfigLoadOptions config{};
  u64 max_ticks{};
};

[[nodiscard]] DedicatedLaunchOptions parse_launch_options(int argc, char** argv);

} // namespace arv::dedicated
