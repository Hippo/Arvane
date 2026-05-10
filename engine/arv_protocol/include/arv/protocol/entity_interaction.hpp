#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <vector>

namespace arv::protocol {

struct ServerboundAttackEntity {
  EntityNetID target_entity{};
  u32 sequence{};
};

std::vector<u8> serialize_serverbound_attack_entity(
    const ServerboundAttackEntity& attack);
Expected<ServerboundAttackEntity, ProtocolError> deserialize_serverbound_attack_entity(
    std::span<const u8> bytes);

} // namespace arv::protocol
