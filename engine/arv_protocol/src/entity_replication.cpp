#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/item_stack_wire.hpp"
#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {

std::vector<u8> serialize_clientbound_dropped_item_snapshot(const ClientboundDroppedItemSnapshot& snapshot) {
  MessageWriter writer;
  writer.write<u32>(snapshot.world_id.value);
  writer.write<u64>(snapshot.server_tick);
  writer.write<u32>(static_cast<u32>(snapshot.drops.size()));
  for (const DroppedItemSnapshotEntry& entry : snapshot.drops) {
    writer.write<u32>(entry.drop_id.value);
    if (auto stack = write_item_stack(writer, entry.stack); !stack) {
      return {};
    }
    write_vec3(writer, entry.position);
    write_vec3(writer, entry.velocity);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundDroppedItemSnapshot, ProtocolError> deserialize_clientbound_dropped_item_snapshot(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto server_tick = reader.read<u64>();
  auto count = reader.read<u32>();
  if (!world_id || !server_tick || !count) {
    return Expected<ClientboundDroppedItemSnapshot, ProtocolError>::failure(
        ProtocolError{"failed to decode dropped item snapshot header"});
  }
  if (count.value() > MaxDroppedItemsPerSnapshot) {
    return Expected<ClientboundDroppedItemSnapshot, ProtocolError>::failure(
        ProtocolError{"too many dropped items in snapshot"});
  }

  ClientboundDroppedItemSnapshot snapshot;
  snapshot.world_id = WorldID{world_id.value()};
  snapshot.server_tick = server_tick.value();
  snapshot.drops.reserve(count.value());
  for (u32 index = 0; index < count.value(); ++index) {
    auto drop_id = reader.read<u32>();
    auto stack = read_item_stack(reader);
    auto position = read_vec3(reader);
    auto velocity = read_vec3(reader);
    if (!drop_id || !stack || !position || !velocity) {
      return Expected<ClientboundDroppedItemSnapshot, ProtocolError>::failure(
          ProtocolError{"failed to decode dropped item snapshot entry"});
    }
    snapshot.drops.push_back({EntityNetID{drop_id.value()},
                              std::move(stack.value()),
                              position.value(),
                              velocity.value()});
  }
  auto trailing = require_no_trailing_bytes(reader, "entity message");
  if (!trailing) {
    return Expected<ClientboundDroppedItemSnapshot, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundDroppedItemSnapshot, ProtocolError>::success(std::move(snapshot));
}

} // namespace arv::protocol
