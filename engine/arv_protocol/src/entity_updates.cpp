#include "arv/protocol/entity_updates.hpp"

#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {

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

Expected<ClientboundEntitySpawn, ProtocolError> deserialize_clientbound_entity_spawn(
    std::span<const u8> bytes) {
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
  auto trailing = require_no_trailing_bytes(reader, "entity message");
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

std::vector<u8> serialize_clientbound_entity_despawn(
    const ClientboundEntityDespawn& despawn) {
  MessageWriter writer;
  writer.write<u32>(despawn.world_id.value);
  writer.write<u32>(despawn.entity_id.value);
  writer.write<u16>(despawn.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundEntityDespawn, ProtocolError> deserialize_clientbound_entity_despawn(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto entity_id = reader.read<u32>();
  auto reason = reader.read<u16>();
  if (!world_id || !entity_id || !reason) {
    return Expected<ClientboundEntityDespawn, ProtocolError>::failure(
        ProtocolError{"failed to decode entity despawn"});
  }
  auto trailing = require_no_trailing_bytes(reader, "entity message");
  if (!trailing) {
    return Expected<ClientboundEntityDespawn, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundEntityDespawn, ProtocolError>::success(
      {WorldID{world_id.value()}, EntityNetID{entity_id.value()}, reason.value()});
}

std::vector<u8> serialize_clientbound_entity_snapshot(
    const ClientboundEntitySnapshot& snapshot) {
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
    writer.write<u32>(static_cast<u32>(entry.animations.size()));
    for (const EntityAnimationPlayback& animation : entry.animations) {
      writer.write_string(animation.name);
      write_f32(writer, animation.time_seconds);
    }
    write_vec3(writer, entry.head_rotation_degrees);
    write_f32(writer, entry.health_current);
    write_f32(writer, entry.health_max);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundEntitySnapshot, ProtocolError> deserialize_clientbound_entity_snapshot(
    std::span<const u8> bytes) {
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
    auto animation_count = reader.read<u32>();
    if (!entity_id || !position || !velocity || !rotation || !flags ||
        !animation_count) {
      return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(
          ProtocolError{"failed to decode entity snapshot entry"});
    }
    if (animation_count.value() > MaxEntityAnimationsPerSnapshotEntry) {
      return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(
          ProtocolError{"too many animations in entity snapshot entry"});
    }
    std::vector<EntityAnimationPlayback> animations;
    animations.reserve(animation_count.value());
    for (u32 animation_index = 0; animation_index < animation_count.value(); ++animation_index) {
      auto animation_name = reader.read_string();
      auto animation_time = read_f32(reader);
      if (!animation_name || !animation_time) {
        return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(
            ProtocolError{"failed to decode entity snapshot animation"});
      }
      animations.push_back({
          std::move(animation_name.value()),
          animation_time.value()});
    }
    auto head_rotation = read_vec3(reader);
    auto health_current = read_f32(reader);
    auto health_max = read_f32(reader);
    if (!head_rotation || !health_current || !health_max) {
      return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(
          ProtocolError{"failed to decode entity snapshot entry"});
    }
    snapshot.entities.push_back({EntityNetID{entity_id.value()},
                                 position.value(),
                                 velocity.value(),
                                 rotation.value(),
                                 flags.value(),
                                 std::move(animations),
                                 head_rotation.value(),
                                 health_current.value(),
                                 health_max.value()});
  }
  auto trailing = require_no_trailing_bytes(reader, "entity message");
  if (!trailing) {
    return Expected<ClientboundEntitySnapshot, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundEntitySnapshot, ProtocolError>::success(std::move(snapshot));
}

} // namespace arv::protocol
