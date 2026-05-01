#pragma once

#include "dedicated_error.hpp"

#include "arv/server/dedicated_lifetime.hpp"

namespace arv::dedicated {

class DedicatedApplication {
public:
  [[nodiscard]] int run(int argc, char** argv);

private:
  [[nodiscard]] int fail(const DedicatedError& error);

  server::DedicatedServerLifetime lifetime_;
};

[[nodiscard]] int run_dedicated_server(int argc, char** argv);

} // namespace arv::dedicated
