#include "dedicated_lifetime_log.hpp"

#include "arv/core/log.hpp"

namespace arv::dedicated {

void transition_lifetime(
    server::DedicatedServerLifetime& lifetime,
    server::DedicatedServerLifetimeState state) {
  lifetime.transition(state);
  core::info("dedicated", server::to_string(state));
}

} // namespace arv::dedicated
