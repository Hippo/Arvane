#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

struct ClientboundWorldJoinResult {
  bool accepted{};
  std::string reject_reason{};
  WorldID world_id{};
  PlayerID player_id{};
  EntityNetID local_entity_id{};
  Vec3f spawn_position{};
  u64 initial_tick{};
  u32 tick_rate{};
  i32 effective_view_distance{};
};

struct ClientboundWorldLeave {
  WorldID world_id{};
  u16 reason{};
};

std::vector<u8> serialize_clientbound_world_join_result(const ClientboundWorldJoinResult& result);
Expected<ClientboundWorldJoinResult, ProtocolError> deserialize_clientbound_world_join_result(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_world_leave(const ClientboundWorldLeave& leave);
Expected<ClientboundWorldLeave, ProtocolError> deserialize_clientbound_world_leave(std::span<const u8> bytes);

} // namespace arv::protocol
