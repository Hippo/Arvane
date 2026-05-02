#include "client_dropped_item_mirror.hpp"

namespace arv::client {

void ClientDroppedItemMirrorStore::apply_snapshot(
    const protocol::DroppedItemSnapshot& snapshot,
    WorldID current_world) {
  if (snapshot.world_id != current_world) {
    return;
  }

  HashSet<EntityNetID> seen;
  seen.reserve(snapshot.drops.size());
  for (const protocol::DroppedItemSnapshotEntry& entry : snapshot.drops) {
    if (entry.stack.count == 0U) {
      continue;
    }
    seen.insert(entry.drop_id);
    DroppedItemMirror& mirror = dropped_items_[entry.drop_id];
    if (snapshot.server_tick < mirror.last_server_tick) {
      continue;
    }
    const bool first_update = mirror.drop_id.value == 0U;
    mirror.drop_id = entry.drop_id;
    mirror.world_id = snapshot.world_id;
    mirror.previous_position = first_update ? entry.position : mirror.position;
    mirror.position = entry.position;
    mirror.velocity = entry.velocity;
    mirror.stack = entry.stack;
    mirror.last_server_tick = snapshot.server_tick;
  }

  for (auto iter = dropped_items_.begin(); iter != dropped_items_.end();) {
    if (iter->second.world_id == snapshot.world_id &&
        snapshot.server_tick >= iter->second.last_server_tick &&
        seen.find(iter->first) == seen.end()) {
      iter = dropped_items_.erase(iter);
    } else {
      ++iter;
    }
  }
}

usize ClientDroppedItemMirrorStore::count() const noexcept {
  return dropped_items_.size();
}

const DroppedItemMirror* ClientDroppedItemMirrorStore::find(
    EntityNetID drop_id) const noexcept {
  const auto found = dropped_items_.find(drop_id);
  return found == dropped_items_.end() ? nullptr : &found->second;
}

std::vector<DroppedItemMirror> ClientDroppedItemMirrorStore::mirrors() const {
  std::vector<DroppedItemMirror> mirrors;
  mirrors.reserve(dropped_items_.size());
  for (const auto& [drop_id, mirror] : dropped_items_) {
    (void)drop_id;
    mirrors.push_back(mirror);
  }
  return mirrors;
}

void ClientDroppedItemMirrorStore::clear() noexcept {
  dropped_items_.clear();
}

} // namespace arv::client
