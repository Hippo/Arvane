#include "arv/protocol/entity_replication.hpp"

#include <bit>
#include <utility>

namespace arv::protocol {
namespace {

void write_f32(MessageWriter& writer, f32 value) {
  writer.write<u32>(std::bit_cast<u32>(value));
}

Expected<f32, ProtocolError> read_f32(MessageReader& reader) {
  auto value = reader.read<u32>();
  if (!value) {
    return Expected<f32, ProtocolError>::failure(value.error());
  }
  return Expected<f32, ProtocolError>::success(std::bit_cast<f32>(value.value()));
}

void write_vec3(MessageWriter& writer, Vec3f value) {
  write_f32(writer, value.x);
  write_f32(writer, value.y);
  write_f32(writer, value.z);
}

Expected<Vec3f, ProtocolError> read_vec3(MessageReader& reader) {
  auto x = read_f32(reader);
  auto y = read_f32(reader);
  auto z = read_f32(reader);
  if (!x || !y || !z) {
    return Expected<Vec3f, ProtocolError>::failure(ProtocolError{"failed to decode vector"});
  }
  return Expected<Vec3f, ProtocolError>::success({x.value(), y.value(), z.value()});
}

void write_quat(MessageWriter& writer, Quatf value) {
  write_f32(writer, value.x);
  write_f32(writer, value.y);
  write_f32(writer, value.z);
  write_f32(writer, value.w);
}

Expected<Quatf, ProtocolError> read_quat(MessageReader& reader) {
  auto x = read_f32(reader);
  auto y = read_f32(reader);
  auto z = read_f32(reader);
  auto w = read_f32(reader);
  if (!x || !y || !z || !w) {
    return Expected<Quatf, ProtocolError>::failure(ProtocolError{"failed to decode rotation"});
  }
  return Expected<Quatf, ProtocolError>::success({x.value(), y.value(), z.value(), w.value()});
}

Expected<void, ProtocolError> require_no_trailing_bytes(MessageReader& reader) {
  if (reader.remaining() != 0U) {
    return Expected<void, ProtocolError>::failure(ProtocolError{"trailing bytes in entity message"});
  }
  return Expected<void, ProtocolError>::success();
}

} // namespace

std::vector<u8> serialize_clientbound_entity_spawn(const ClientboundEntitySpawn& spawn) {
  MessageWriter writer;
  writer.write<u32>(spawn.world_id.value);
  writer.write<u32>(spawn.entity_id.value);
  writer.write<u32>(spawn.entity_type.value);
  writer.write_string(spawn.entity_type_name);
  write_vec3(writer, spawn.position);
  write_vec3(writer, spawn.velocity);
  write_quat(writer, spawn.rotation);
  writer.write<u32>(spawn.spawn_flags);
  writer.write_bool(spawn.owning_player.has_value());
  if (spawn.owning_player.has_value()) {
    writer.write<u64>(spawn.owning_player->value);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundEntitySpawn, ProtocolError> deserialize_clientbound_entity_spawn(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto entity_id = reader.read<u32>();
  auto entity_type = reader.read<u32>();
  auto entity_type_name = reader.read_string();
  auto position = read_vec3(reader);
  auto velocity = read_vec3(reader);
  auto rotation = read_quat(reader);
  auto flags = reader.read<u32>();
  auto has_owner = reader.read_bool();
  if (!world_id || !entity_id || !entity_type || !entity_type_name || !position ||
      !velocity || !rotation || !flags || !has_owner) {
    return Expected<ClientboundEntitySpawn, ProtocolError>::failure(
        ProtocolError{"failed to decode entity spawn"});
  }

  std::optional<PlayerID> owner;
  if (has_owner.value()) {
    auto player_id = reader.read<u64>();
    if (!player_id) {
      return Expected<ClientboundEntitySpawn, ProtocolError>::failure(
          ProtocolError{"failed to decode entity owner"});
    }
    owner = PlayerID{player_id.value()};
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundEntitySpawn, ProtocolError>::failure(trailing.error());
  }

  ClientboundEntitySpawn spawn;
  spawn.world_id = WorldID{world_id.value()};
  spawn.entity_id = EntityNetID{entity_id.value()};
  spawn.entity_type = EntityTypeID{entity_type.value()};
  spawn.entity_type_name = std::move(entity_type_name.value());
  spawn.position = position.value();
  spawn.velocity = velocity.value();
  spawn.rotation = rotation.value();
  spawn.spawn_flags = flags.value();
  spawn.owning_player = owner;
  return Expected<ClientboundEntitySpawn, ProtocolError>::success(std::move(spawn));
}

std::vector<u8> serialize_clientbound_entity_despawn(const ClientboundEntityDespawn& despawn) {
  MessageWriter writer;
  writer.write<u32>(despawn.world_id.value);
  writer.write<u32>(despawn.entity_id.value);
  writer.write<u16>(despawn.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundEntityDespawn, ProtocolError> deserialize_clientbound_entity_despawn(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto entity_id = reader.read<u32>();
  auto reason = reader.read<u16>();
  if (!world_id || !entity_id || !reason) {
    return Expected<ClientboundEntityDespawn, ProtocolError>::failure(
        ProtocolError{"failed to decode entity despawn"});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundEntityDespawn, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundEntityDespawn, ProtocolError>::success(
      {WorldID{world_id.value()}, EntityNetID{entity_id.value()}, reason.value()});
}

std::vector<u8> serialize_clientbound_entity_snapshot(const ClientboundEntitySnapshot& snapshot) {
  MessageWriter writer;
  writer.write<u32>(snapshot.world_id.value);
  writer.write<u64>(snapshot.server_tick);
  writer.write<u32>(static_cast<u32>(snapshot.entities.size()));
  for (const EntitySnapshotEntry& entry : snapshot.entities) {
    writer.write<u32>(entry.entity_id.value);
    write_vec3(writer, entry.position);
    write_vec3(writer, entry.velocity);
    write_quat(writer, entry.rotation);
    writer.write<u32>(entry.state_flags);
    writer.write_string(entry.animation_name);
    write_f32(writer, entry.animation_time_seconds);
    write_vec3(writer, entry.head_rotation_degrees);
    write_f32(writer, entry.health_current);
    write_f32(writer, entry.health_max);
  }
  return std::move(writer).take_bytes();
}

std::vector<u8> serialize_clientbound_dropped_item_snapshot(const ClientboundDroppedItemSnapshot& snapshot) {
  MessageWriter writer;
  writer.write<u32>(snapshot.world_id.value);
  writer.write<u64>(snapshot.server_tick);
  writer.write<u32>(static_cast<u32>(snapshot.drops.size()));
  for (const DroppedItemSnapshotEntry& entry : snapshot.drops) {
    writer.write<u32>(entry.drop_id.value);
    writer.write<u32>(entry.stack.item.value);
    writer.write<u16>(entry.stack.count);
    write_vec3(writer, entry.position);
    write_vec3(writer, entry.velocity);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundEntitySnapshot, ProtocolError> deserialize_clientbound_entity_snapshot(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto server_tick = reader.read<u64>();
  auto count = reader.read<u32>();
  if (!world_id || !server_tick || !count) {
    return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(
        ProtocolError{"failed to decode entity snapshot header"});
  }
  if (count.value() > MaxEntitiesPerSnapshot) {
    return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(
        ProtocolError{"too many entities in snapshot"});
  }

  ClientboundEntitySnapshot snapshot;
  snapshot.world_id = WorldID{world_id.value()};
  snapshot.server_tick = server_tick.value();
  snapshot.entities.reserve(count.value());
  for (u32 index = 0; index < count.value(); ++index) {
    auto entity_id = reader.read<u32>();
    auto position = read_vec3(reader);
    auto velocity = read_vec3(reader);
    auto rotation = read_quat(reader);
    auto flags = reader.read<u32>();
    auto animation_name = reader.read_string();
    auto animation_time = read_f32(reader);
    auto head_rotation = read_vec3(reader);
    auto health_current = read_f32(reader);
    auto health_max = read_f32(reader);
    if (!entity_id || !position || !velocity || !rotation || !flags ||
        !animation_name || !animation_time || !head_rotation ||
        !health_current || !health_max) {
      return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(
          ProtocolError{"failed to decode entity snapshot entry"});
    }
    snapshot.entities.push_back({EntityNetID{entity_id.value()},
                                 position.value(),
                                 velocity.value(),
                                 rotation.value(),
                                 flags.value(),
                                 std::move(animation_name.value()),
                                 animation_time.value(),
                                 head_rotation.value(),
                                 health_current.value(),
                                 health_max.value()});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundEntitySnapshot, ProtocolError>::success(std::move(snapshot));
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
    auto item = reader.read<u32>();
    auto count_value = reader.read<u16>();
    auto position = read_vec3(reader);
    auto velocity = read_vec3(reader);
    if (!drop_id || !item || !count_value || !position || !velocity) {
      return Expected<ClientboundDroppedItemSnapshot, ProtocolError>::failure(
          ProtocolError{"failed to decode dropped item snapshot entry"});
    }
    snapshot.drops.push_back({EntityNetID{drop_id.value()},
                              player::ItemStack{player::ItemID{item.value()}, count_value.value()},
                              position.value(),
                              velocity.value()});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundDroppedItemSnapshot, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundDroppedItemSnapshot, ProtocolError>::success(std::move(snapshot));
}

} // namespace arv::protocol
