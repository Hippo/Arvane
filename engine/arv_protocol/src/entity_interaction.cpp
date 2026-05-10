#include "arv/protocol/entity_interaction.hpp"

#include <utility>

namespace arv::protocol {

std::vector<u8> serialize_serverbound_attack_entity(
    const ServerboundAttackEntity& attack) {
  MessageWriter writer;
  writer.write<u32>(attack.target_entity.value);
  writer.write<u32>(attack.sequence);
  return std::move(writer).take_bytes();
}

Expected<ServerboundAttackEntity, ProtocolError> deserialize_serverbound_attack_entity(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto target = reader.read<u32>();
  auto sequence = reader.read<u32>();
  if (!target || !sequence) {
    return Expected<ServerboundAttackEntity, ProtocolError>::failure(
        ProtocolError{"failed to decode entity attack"});
  }
  if (reader.remaining() != 0U) {
    return Expected<ServerboundAttackEntity, ProtocolError>::failure(
        ProtocolError{"trailing bytes in entity attack"});
  }
  return Expected<ServerboundAttackEntity, ProtocolError>::success(
      {EntityNetID{target.value()}, sequence.value()});
}

} // namespace arv::protocol
