#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/player/item.hpp"
#include "arv/protocol/message_writer.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

struct EntitySpawn {
  WorldID world_id{};
  EntityNetID entity_id{};
  EntityTypeID entity_type{};
  std::string entity_type_name{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  u32 spawn_flags{};
  std::optional<PlayerID> owning_player{};
};

struct EntityDespawn {
  WorldID world_id{};
  EntityNetID entity_id{};
  u16 reason{};
};

struct EntitySnapshotEntry {
  EntityNetID entity_id{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  u32 state_flags{};
};

struct EntitySnapshot {
  WorldID world_id{};
  u64 server_tick{};
  std::vector<EntitySnapshotEntry> entities{};
};

struct DroppedItemSnapshotEntry {
  EntityNetID drop_id{};
  player::ItemStack stack{};
  Vec3f position{};
  Vec3f velocity{};
};

struct DroppedItemSnapshot {
  WorldID world_id{};
  u64 server_tick{};
  std::vector<DroppedItemSnapshotEntry> drops{};
};

inline constexpr u32 MaxEntitiesPerSnapshot = 4096;
inline constexpr u32 MaxDroppedItemsPerSnapshot = 4096;

std::vector<u8> serialize_entity_spawn(const EntitySpawn& spawn);
Expected<EntitySpawn, ProtocolError> deserialize_entity_spawn(std::span<const u8> bytes);

std::vector<u8> serialize_entity_despawn(const EntityDespawn& despawn);
Expected<EntityDespawn, ProtocolError> deserialize_entity_despawn(std::span<const u8> bytes);

std::vector<u8> serialize_entity_snapshot(const EntitySnapshot& snapshot);
Expected<EntitySnapshot, ProtocolError> deserialize_entity_snapshot(std::span<const u8> bytes);

std::vector<u8> serialize_dropped_item_snapshot(const DroppedItemSnapshot& snapshot);
Expected<DroppedItemSnapshot, ProtocolError> deserialize_dropped_item_snapshot(
    std::span<const u8> bytes);

} // namespace arv::protocol
