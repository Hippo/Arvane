#include "arv/client/gameplay_interactions.hpp"

#include "arv/physics/raycast.hpp"
#include "arv/player/inventory.hpp"
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

bool request_dismount_if_mounted(ClientRuntime& client) {
  if (!local_player_is_mounted(client)) {
    return false;
  }
  return static_cast<bool>(client.request_dismount());
}

std::optional<BlockPos> targeted_container_block(
    const ContainerBlockTargetQuery& query) {
  if (query.client.inventory_open() || query.client.chat_open()) {
    return std::nullopt;
  }

  const CameraShell& camera = query.client.camera();
  const std::optional<physics::BlockRaycastHit> hit = physics::raycast_blocks(
      query.collision_query,
      camera.position(),
      camera.forward_vector(),
      query.reach_distance);
  if (!hit.has_value()) {
    return std::nullopt;
  }

  const voxel::BlockVariantRecord* variant =
      query.block_registry.variant(hit->variant);
  if (variant == nullptr || variant->container_type.empty()) {
    return std::nullopt;
  }
  return hit->block;
}

bool request_open_container_block_if_targeted(
    ClientRuntime& client,
    const physics::ICollisionQuery& collision_query,
    const voxel::BlockRegistry& block_registry,
    f32 reach_distance) {
  const std::optional<BlockPos> target = targeted_container_block(
      {client, collision_query, block_registry, reach_distance});
  if (!target.has_value()) {
    return false;
  }
  return static_cast<bool>(
      client.request_open_container_block(client.camera().world_id(), target.value()));
}

GameplayInteractionDispatchResult dispatch_gameplay_interactions(
    const GameplayInteractionDispatchInput& input) {
  GameplayInteractionDispatchResult result{};
  if (input.gameplay_blocked || input.client.inventory_open() ||
      input.client.chat_open()) {
    return result;
  }

  if (input.secondary_pressed) {
    result.opened_container = request_open_container_block_if_targeted(
        input.client,
        input.collision_query,
        input.block_registry,
        input.reach_distance);
    if (result.opened_container) {
      result.consume_secondary_use = true;
      result.stop_held_item_animation = true;
    }
  }

  if (input.primary_pressed) {
    result.attacked_entity = request_attack_entity_if_targeted(
        input.client,
        input.collision_query,
        input.block_registry,
        input.entity_registry,
        input.vehicle_registry,
        input.held_item,
        input.default_attack_range);
  }

  if (input.mount_pressed) {
    if (local_player_is_mounted(input.client)) {
      result.requested_dismount = request_dismount_if_mounted(input.client);
    } else {
      result.requested_ride = request_ride_entity_if_targeted(
          input.client,
          input.collision_query,
          input.block_registry,
          input.entity_registry,
          input.vehicle_registry,
          input.ride_range);
      if (result.requested_ride) {
        result.stop_held_item_animation = true;
      }
    }
  }

  return result;
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

std::optional<player::ItemStack> visible_held_item_stack(
    const ClientRuntime& client) {
  if (!client.has_authoritative_inventory() ||
      client.inventory_open() ||
      client.chat_open() ||
      client.container_open()) {
    return std::nullopt;
  }
  const usize selected_slot = client.selected_hotbar_slot();
  const player::Inventory& inventory = client.inventory();
  if (selected_slot >= inventory.slot_count()) {
    return std::nullopt;
  }
  const std::optional<player::ItemStack>& slot = inventory.slot(selected_slot);
  if (!slot.has_value() || slot->count == 0U) {
    return std::nullopt;
  }
  return slot.value();
}

std::optional<BlockPos> secondary_place_position(
    const HeldItemPlacementPreviewQuery& query) {
  const CameraShell& camera = query.client.camera();
  const std::optional<physics::BlockRaycastHit> hit = physics::raycast_blocks(
      query.collision_query,
      camera.position(),
      camera.forward_vector(),
      query.config.reach_distance);
  if (!hit.has_value()) {
    return std::nullopt;
  }

  BlockPos place_pos = hit->block;
  if (!query.block_registry.is_replaceable(hit->variant)) {
    place_pos.value = place_pos.value + voxel::block_face_offset(hit->face);
  }
  if (!query.collision_query.is_block_loaded(place_pos)) {
    return std::nullopt;
  }
  return place_pos;
}

bool selected_water_bucket_can_leave_empty_bucket(
    const ClientRuntime& client,
    const player::ItemRegistry& item_registry,
    std::string_view empty_bucket_item_name) noexcept {
  if (empty_bucket_item_name.empty() || !client.has_authoritative_inventory()) {
    return false;
  }

  const std::optional<player::ItemID> bucket_id =
      item_registry.find_item(empty_bucket_item_name);
  if (!bucket_id.has_value()) {
    return false;
  }
  const player::ItemRecord* bucket = item_registry.item(bucket_id.value());
  if (bucket == nullptr) {
    return false;
  }

  const usize selected_slot = client.selected_hotbar_slot();
  const player::Inventory& inventory = client.inventory();
  if (selected_slot >= inventory.slot_count()) {
    return false;
  }
  const std::optional<player::ItemStack>& held = inventory.slot(selected_slot);
  if (!held.has_value() || held->count == 0U) {
    return false;
  }
  if (held->count == 1U) {
    return true;
  }

  for (usize slot_index = 0; slot_index < inventory.slot_count(); ++slot_index) {
    if (slot_index == selected_slot) {
      continue;
    }
    const std::optional<player::ItemStack>& slot = inventory.slot(slot_index);
    if (!slot.has_value()) {
      return true;
    }
    if (slot->item == bucket_id.value() && slot->count < bucket->max_stack_size) {
      return true;
    }
  }
  return false;
}

bool block_placement_preview_succeeds(
    const HeldItemPlacementPreviewQuery& query,
    const player::ItemRecord& held_item) {
  if (!held_item.placement_variant.has_value()) {
    return false;
  }

  const std::optional<BlockPos> place_pos = secondary_place_position(query);
  if (!place_pos.has_value()) {
    return false;
  }

  const voxel::BlockVariantID existing =
      query.collision_query.block_variant(place_pos.value());
  if (!(query.block_registry.is_air(existing) ||
        query.block_registry.is_replaceable(existing))) {
    return false;
  }

  const Aabb3f player_bounds = physics::make_body_bounds(
      query.client.player_position(),
      query.config.player_kinematics,
      physics::PlayerCollisionPose::Standing);
  for (const Aabb3f& local_box :
       query.block_registry.collision_boxes(held_item.placement_variant.value())) {
    const Aabb3f world_box{
        {local_box.min.x + static_cast<f32>(place_pos->value.x),
         local_box.min.y + static_cast<f32>(place_pos->value.y),
         local_box.min.z + static_cast<f32>(place_pos->value.z)},
        {local_box.max.x + static_cast<f32>(place_pos->value.x),
         local_box.max.y + static_cast<f32>(place_pos->value.y),
         local_box.max.z + static_cast<f32>(place_pos->value.z)}};
    if (player_bounds.overlaps(world_box)) {
      return false;
    }
  }
  return true;
}

bool water_bucket_placement_preview_succeeds(
    const HeldItemPlacementPreviewQuery& query) {
  if (!selected_water_bucket_can_leave_empty_bucket(
          query.client,
          query.item_registry,
          query.config.empty_bucket_item_name)) {
    return false;
  }

  const std::optional<BlockPos> place_pos = secondary_place_position(query);
  if (!place_pos.has_value()) {
    return false;
  }

  const voxel::BlockVariantID existing_block =
      query.collision_query.block_variant(place_pos.value());
  if (!(query.block_registry.is_air(existing_block) ||
        query.block_registry.is_replaceable(existing_block))) {
    return false;
  }

  const voxel::FluidCell existing_fluid =
      query.collision_query.fluid_cell(place_pos.value());
  return existing_fluid.empty() ||
         (existing_fluid.type == query.config.water_fluid_type &&
          !existing_fluid.source());
}

bool held_item_placement_preview_succeeds(
    const HeldItemPlacementPreviewQuery& query,
    const player::ItemRecord* held_item) {
  if (held_item == nullptr ||
      !query.client.resolved_permissions().core.can_modify_world) {
    return false;
  }

  if (held_item->placement_variant.has_value()) {
    return block_placement_preview_succeeds(query, *held_item);
  }
  if (!query.config.water_bucket_item_name.empty() &&
      held_item->name == query.config.water_bucket_item_name) {
    return water_bucket_placement_preview_succeeds(query);
  }
  return false;
}

} // namespace arv::client
