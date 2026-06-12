#pragma once

#include "arv/protocol/entity_updates.hpp"
#include "arv/player/item.hpp"

#include <span>
#include <vector>

namespace arv::protocol {

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

inline constexpr u32 MaxDroppedItemsPerSnapshot = 4096;

std::vector<u8> serialize_clientbound_dropped_item_snapshot(const ClientboundDroppedItemSnapshot& snapshot);
Expected<ClientboundDroppedItemSnapshot, ProtocolError> deserialize_clientbound_dropped_item_snapshot(
    std::span<const u8> bytes);

} // namespace arv::protocol
