#pragma once

#include "arv/protocol/world_time.hpp"
#include "arv/world/world_time.hpp"

namespace arv::client {

[[nodiscard]] world::WorldClock world_clock_from_time_packet(
    const protocol::ClientboundWorldTime& time) noexcept;

} // namespace arv::client
