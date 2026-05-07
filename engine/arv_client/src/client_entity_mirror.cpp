#include "client_entity_mirror.hpp"

#include <utility>

namespace arv::client {

void ClientEntityMirrorStore::apply_spawn(
    protocol::EntitySpawn spawn,
    WorldID current_world) {
  if (spawn.world_id != current_world) {
    return;
  }
  ReplicatedEntityMirror& mirror = entities_[spawn.entity_id];
  mirror.entity_id = spawn.entity_id;
  mirror.entity_type = spawn.entity_type;
  mirror.entity_type_name = std::move(spawn.entity_type_name);
  mirror.world_id = spawn.world_id;
  mirror.previous_position = spawn.position;
  mirror.position = spawn.position;
  mirror.velocity = spawn.velocity;
  mirror.previous_rotation = spawn.rotation;
  mirror.rotation = spawn.rotation;
  mirror.owning_player = spawn.owning_player;
}

void ClientEntityMirrorStore::apply_despawn(
    const protocol::EntityDespawn& despawn,
    WorldID current_world) {
  if (despawn.world_id != current_world) {
    return;
  }
  entities_.erase(despawn.entity_id);
}

void ClientEntityMirrorStore::apply_snapshot(
    const protocol::EntitySnapshot& snapshot,
    WorldID current_world) {
  if (snapshot.world_id != current_world) {
    return;
  }
  // TODO(arvane): Add client prediction replay/reconciliation history once
  // predicted entity movement is specified.
  for (const protocol::EntitySnapshotEntry& entry : snapshot.entities) {
    auto found = entities_.find(entry.entity_id);
    if (found == entities_.end()) {
      continue;
    }
    ReplicatedEntityMirror& mirror = found->second;
    if (snapshot.server_tick < mirror.last_server_tick) {
      continue;
    }
    mirror.last_server_tick = snapshot.server_tick;
    mirror.previous_position = mirror.position;
    mirror.previous_rotation = mirror.rotation;
    mirror.position = entry.position;
    mirror.velocity = entry.velocity;
    mirror.rotation = entry.rotation;
    mirror.animation_name = entry.animation_name;
    mirror.animation_time_seconds = entry.animation_time_seconds;
    mirror.head_rotation_degrees = entry.head_rotation_degrees;
  }
}

usize ClientEntityMirrorStore::count() const noexcept {
  return entities_.size();
}

const ReplicatedEntityMirror* ClientEntityMirrorStore::find(
    EntityNetID entity_id) const noexcept {
  const auto found = entities_.find(entity_id);
  return found == entities_.end() ? nullptr : &found->second;
}

std::vector<ReplicatedEntityMirror> ClientEntityMirrorStore::mirrors() const {
  std::vector<ReplicatedEntityMirror> mirrors;
  mirrors.reserve(entities_.size());
  for (const auto& [entity_id, mirror] : entities_) {
    (void)entity_id;
    mirrors.push_back(mirror);
  }
  return mirrors;
}

void ClientEntityMirrorStore::clear() noexcept {
  entities_.clear();
}

} // namespace arv::client
