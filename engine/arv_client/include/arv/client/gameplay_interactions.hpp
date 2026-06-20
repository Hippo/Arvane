#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/entity/components.hpp"
#include "arv/physics/collision.hpp"
#include "arv/physics/collision_query.hpp"
#include "arv/physics/kinematics.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"

#include <optional>
#include <string_view>

namespace arv::client {

struct EntityTargetingQuery {
  const ClientRuntime& client;
  const physics::ICollisionQuery& collision_query;
  const voxel::BlockRegistry& block_registry;
  const entity::EntityRegistry& entity_registry;
  const entity::VehicleDefinitionRegistry& vehicle_registry;
};

struct HeldItemPlacementPreviewConfig {
  f32 reach_distance{player::DefaultReachDistance};
  physics::KinematicConfig player_kinematics{};
  voxel::FluidTypeID water_fluid_type{voxel::DefaultWaterFluidTypeID};
  std::string_view empty_bucket_item_name{};
  std::string_view water_bucket_item_name{};
};

struct HeldItemPlacementPreviewQuery {
  const ClientRuntime& client;
  const physics::ICollisionQuery& collision_query;
  const voxel::BlockRegistry& block_registry;
  const player::ItemRegistry& item_registry;
  HeldItemPlacementPreviewConfig config{};
};

struct ContainerBlockTargetQuery {
  const ClientRuntime& client;
  const physics::ICollisionQuery& collision_query;
  const voxel::BlockRegistry& block_registry;
  f32 reach_distance{player::DefaultReachDistance};
};

struct GameplayInteractionDispatchInput {
  ClientRuntime& client;
  const physics::ICollisionQuery& collision_query;
  const voxel::BlockRegistry& block_registry;
  const entity::EntityRegistry& entity_registry;
  const entity::VehicleDefinitionRegistry& vehicle_registry;
  const player::ItemRecord* held_item{};
  bool gameplay_blocked{};
  bool secondary_pressed{};
  bool primary_pressed{};
  bool mount_pressed{};
  f32 reach_distance{player::DefaultReachDistance};
  f32 default_attack_range{player::DefaultReachDistance};
  f32 ride_range{player::DefaultReachDistance + 1.5F};
};

struct GameplayInteractionDispatchResult {
  bool opened_container{};
  bool attacked_entity{};
  bool requested_ride{};
  bool requested_dismount{};
  bool consume_secondary_use{};
  bool stop_held_item_animation{};
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

[[nodiscard]] bool request_dismount_if_mounted(ClientRuntime& client);

[[nodiscard]] std::optional<BlockPos> targeted_container_block(
    const ContainerBlockTargetQuery& query);

[[nodiscard]] bool request_open_container_block_if_targeted(
    ClientRuntime& client,
    const physics::ICollisionQuery& collision_query,
    const voxel::BlockRegistry& block_registry,
    f32 reach_distance);

[[nodiscard]] GameplayInteractionDispatchResult dispatch_gameplay_interactions(
    const GameplayInteractionDispatchInput& input);

[[nodiscard]] bool local_player_is_mounted(const ClientRuntime& client) noexcept;

[[nodiscard]] std::optional<player::ItemStack> visible_held_item_stack(
    const ClientRuntime& client);

[[nodiscard]] std::optional<BlockPos> secondary_place_position(
    const HeldItemPlacementPreviewQuery& query);

[[nodiscard]] bool selected_water_bucket_can_leave_empty_bucket(
    const ClientRuntime& client,
    const player::ItemRegistry& item_registry,
    std::string_view empty_bucket_item_name) noexcept;

[[nodiscard]] bool block_placement_preview_succeeds(
    const HeldItemPlacementPreviewQuery& query,
    const player::ItemRecord& held_item);

[[nodiscard]] bool water_bucket_placement_preview_succeeds(
    const HeldItemPlacementPreviewQuery& query);

[[nodiscard]] bool held_item_placement_preview_succeeds(
    const HeldItemPlacementPreviewQuery& query,
    const player::ItemRecord* held_item);

} // namespace arv::client
