#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/entity/components.hpp"
#include "arv/physics/collision.hpp"
#include "arv/physics/collision_query.hpp"
#include "arv/player/item.hpp"
#include "arv/voxel/block.hpp"

#include <optional>

namespace arv::client {

struct EntityTargetingQuery {
  const ClientRuntime& client;
  const physics::ICollisionQuery& collision_query;
  const voxel::BlockRegistry& block_registry;
  const entity::EntityRegistry& entity_registry;
  const entity::VehicleDefinitionRegistry& vehicle_registry;
};

[[nodiscard]] Aabb3f replicated_entity_interaction_bounds(
    const ReplicatedEntityMirror& mirror,
    const entity::EntityRegistry& entity_registry) noexcept;

[[nodiscard]] std::optional<EntityNetID> targeted_attack_entity(
    const EntityTargetingQuery& query,
    const player::ItemRecord* held_item,
    f32 default_attack_range);

[[nodiscard]] std::optional<EntityNetID> targeted_rideable_entity(
    const EntityTargetingQuery& query,
    f32 max_distance);

[[nodiscard]] bool request_attack_entity_if_targeted(
    ClientRuntime& client,
    const physics::ICollisionQuery& collision_query,
    const voxel::BlockRegistry& block_registry,
    const entity::EntityRegistry& entity_registry,
    const entity::VehicleDefinitionRegistry& vehicle_registry,
    const player::ItemRecord* held_item,
    f32 default_attack_range);

[[nodiscard]] bool request_ride_entity_if_targeted(
    ClientRuntime& client,
    const physics::ICollisionQuery& collision_query,
    const voxel::BlockRegistry& block_registry,
    const entity::EntityRegistry& entity_registry,
    const entity::VehicleDefinitionRegistry& vehicle_registry,
    f32 max_distance);

[[nodiscard]] bool local_player_is_mounted(const ClientRuntime& client) noexcept;

} // namespace arv::client
