#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/player/item.hpp"
#include "arv/protocol/message_writer.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

struct ClientboundEntitySpawn {
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

struct ClientboundEntityDespawn {
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
  std::string animation_name{};
  Seconds animation_time_seconds{};
  Vec3f head_rotation_degrees{};
};

struct ClientboundEntitySnapshot {
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

struct ClientboundDroppedItemSnapshot {
  WorldID world_id{};
  u64 server_tick{};
  std::vector<DroppedItemSnapshotEntry> drops{};
};

inline constexpr u32 MaxEntitiesPerSnapshot = 4096;
inline constexpr u32 MaxDroppedItemsPerSnapshot = 4096;

std::vector<u8> serialize_clientbound_entity_spawn(const ClientboundEntitySpawn& spawn);
Expected<ClientboundEntitySpawn, ProtocolError> deserialize_clientbound_entity_spawn(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_entity_despawn(const ClientboundEntityDespawn& despawn);
Expected<ClientboundEntityDespawn, ProtocolError> deserialize_clientbound_entity_despawn(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_entity_snapshot(const ClientboundEntitySnapshot& snapshot);
Expected<ClientboundEntitySnapshot, ProtocolError> deserialize_clientbound_entity_snapshot(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_dropped_item_snapshot(const ClientboundDroppedItemSnapshot& snapshot);
Expected<ClientboundDroppedItemSnapshot, ProtocolError> deserialize_clientbound_dropped_item_snapshot(
    std::span<const u8> bytes);

} // namespace arv::protocol
