#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <vector>

namespace arv::protocol {

struct ClientboundWorldTime {
  WorldID world_id{};
  TickIndex world_tick{};
  u32 day_time_tick{};
  u32 day_length_ticks{};
  Seconds day_length_seconds{};
  f32 time_scale{};
  bool daylight_cycle{};
};

std::vector<u8> serialize_clientbound_world_time(const ClientboundWorldTime& time);
Expected<ClientboundWorldTime, ProtocolError> deserialize_clientbound_world_time(
    std::span<const u8> bytes);

} // namespace arv::protocol
