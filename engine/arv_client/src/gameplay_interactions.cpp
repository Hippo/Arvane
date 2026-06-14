#include "arv/client/gameplay_interactions.hpp"

#include "arv/physics/raycast.hpp"
#include "arv/player/player_constants.hpp"

#include <algorithm>

namespace arv::client {

Aabb3f replicated_entity_interaction_bounds(
    const ReplicatedEntityMirror& mirror,
    const entity::EntityRegistry& entity_registry) noexcept {
  Vec3f box{1.0F, 1.0F, 1.0F};
  if (const entity::EntityTypeRecord* type = entity_registry.type(mirror.entity_type);
      type != nullptr && type->components.collision.has_value()) {
    box = type->components.collision->box;
  } else if (mirror.owning_player.has_value()) {
    box = {
        player::DefaultPlayerBodyWidth,
        player::DefaultPlayerBodyHeight,
        player::DefaultPlayerBodyWidth};
  }

  const f32 half_x = std::max(box.x, 0.0F) * 0.5F;
  const f32 half_z = std::max(box.z, 0.0F) * 0.5F;
  const f32 height = std::max(box.y, 0.0F);
  return {
      {mirror.position.x - half_x,
       mirror.position.y,
       mirror.position.z - half_z},
      {mirror.position.x + half_x,
       mirror.position.y + height,
       mirror.position.z + half_z}};
}

std::optional<EntityNetID> targeted_attack_entity(
    const EntityTargetingQuery& query,
    const player::ItemRecord* held_item,
    f32 default_attack_range) {
  const CameraShell& camera = query.client.camera();
  const Vec3f origin = camera.position();
  const Vec3f direction = camera.forward_vector();
  f32 max_distance = default_attack_range;
  if (held_item != nullptr && held_item->attack_data.has_value()) {
    max_distance = held_item->attack_data->attack_range;
  }

  if (const std::optional<physics::BlockRaycastHit> block_hit = physics::raycast_blocks(
          query.collision_query,
          origin,
          direction,
          max_distance);
      block_hit.has_value() && !query.block_registry.is_air(block_hit->variant)) {
    max_distance = std::min(max_distance, block_hit->distance);
  }

  std::optional<EntityNetID> best_entity;
  f32 best_distance = max_distance;
  const std::optional<EntityNetID> local_entity = query.client.local_entity_id();
  for (const ReplicatedEntityMirror& mirror : query.client.replicated_entity_mirrors()) {
    if ((local_entity.has_value() && mirror.entity_id == local_entity.value()) ||
        mirror.dead) {
      continue;
    }
    const std::optional<physics::RayAabbHit> hit = physics::ray_intersects_aabb(
        origin,
        direction,
        replicated_entity_interaction_bounds(mirror, query.entity_registry),
        best_distance);
    if (hit.has_value() && hit->distance <= best_distance) {
      best_distance = hit->distance;
      best_entity = mirror.entity_id;
    }
  }
  return best_entity;
}

std::optional<EntityNetID> targeted_rideable_entity(
    const EntityTargetingQuery& query,
    f32 max_distance) {
  const CameraShell& camera = query.client.camera();
  const Vec3f origin = camera.position();
  const Vec3f direction = camera.forward_vector();

  if (const std::optional<physics::BlockRaycastHit> block_hit = physics::raycast_blocks(
          query.collision_query,
          origin,
          direction,
          max_distance);
      block_hit.has_value() && !query.block_registry.is_air(block_hit->variant)) {
    max_distance = std::min(max_distance, block_hit->distance);
  }

  std::optional<EntityNetID> best_entity;
  f32 best_distance = max_distance;
  const std::optional<EntityNetID> local_entity = query.client.local_entity_id();
  for (const ReplicatedEntityMirror& mirror : query.client.replicated_entity_mirrors()) {
    if ((local_entity.has_value() && mirror.entity_id == local_entity.value()) ||
        mirror.dead) {
      continue;
    }
    const entity::EntityTypeRecord* type =
        query.entity_registry.type(mirror.entity_type);
    const bool type_is_rideable =
        type != nullptr && type->components.rideable.has_value();
    const bool definition_is_rideable =
        query.vehicle_registry.find(mirror.entity_type_name) != nullptr;
    if (!type_is_rideable && !definition_is_rideable) {
      continue;
    }
    const std::optional<physics::RayAabbHit> hit = physics::ray_intersects_aabb(
        origin,
        direction,
        replicated_entity_interaction_bounds(mirror, query.entity_registry),
        best_distance);
    if (hit.has_value() && hit->distance <= best_distance) {
      best_distance = hit->distance;
      best_entity = mirror.entity_id;
    }
  }
  return best_entity;
}

bool request_attack_entity_if_targeted(
    ClientRuntime& client,
    const physics::ICollisionQuery& collision_query,
    const voxel::BlockRegistry& block_registry,
    const entity::EntityRegistry& entity_registry,
    const entity::VehicleDefinitionRegistry& vehicle_registry,
    const player::ItemRecord* held_item,
    f32 default_attack_range) {
  const std::optional<EntityNetID> target = targeted_attack_entity(
      {client, collision_query, block_registry, entity_registry, vehicle_registry},
      held_item,
      default_attack_range);
  if (!target.has_value()) {
    return false;
  }
  const bool sent = static_cast<bool>(client.request_attack_entity(target.value()));
  if (sent) {
    (void)client.drain_attack_feedback();
  }
  return sent;
}

bool request_ride_entity_if_targeted(
    ClientRuntime& client,
    const physics::ICollisionQuery& collision_query,
    const voxel::BlockRegistry& block_registry,
    const entity::EntityRegistry& entity_registry,
    const entity::VehicleDefinitionRegistry& vehicle_registry,
    f32 max_distance) {
  const std::optional<EntityNetID> target = targeted_rideable_entity(
      {client, collision_query, block_registry, entity_registry, vehicle_registry},
      max_distance);
  if (!target.has_value()) {
    return false;
  }
  return static_cast<bool>(client.request_ride_entity(target.value()));
}

bool local_player_is_mounted(const ClientRuntime& client) noexcept {
  const std::optional<EntityNetID> local_entity = client.local_entity_id();
  if (!local_entity.has_value()) {
    return false;
  }
  const ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(local_entity.value());
  return mirror != nullptr && mirror->parent_rideable.has_value();
}

} // namespace arv::client
