#pragma once

#include "arv/server/dedicated_lifetime.hpp"

namespace arv::dedicated {

void transition_lifetime(
    server::DedicatedServerLifetime& lifetime,
    server::DedicatedServerLifetimeState state);

} // namespace arv::dedicated
