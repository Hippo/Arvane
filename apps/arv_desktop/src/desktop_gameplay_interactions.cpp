#include "desktop_frame_loop.hpp"

#include "desktop_runtime_config.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cmath>

namespace arv::desktop {
namespace {

inline constexpr Seconds BlockMiningParticleIntervalSeconds = 0.09;
inline constexpr Seconds SprintParticleIntervalSeconds = 0.12;

[[nodiscard]] u32 next_particle_seed(DesktopGameSession& session, BlockPos pos) noexcept {
  const u32 sequence = session.particle_seed_counter++;
  const u64 x = static_cast<u64>(static_cast<i64>(pos.value.x) * 73856093LL);
  const u64 y = static_cast<u64>(static_cast<i64>(pos.value.y) * 19349663LL);
  const u64 z = static_cast<u64>(static_cast<i64>(pos.value.z) * 83492791LL);
  const u32 seed = static_cast<u32>(x ^ y ^ z ^ sequence);
  return seed == 0U ? 1U : seed;
}

} // namespace

const player::ItemRecord* DesktopFrameLoop::selected_held_item(
    const DesktopGameSession& session) const noexcept {
  if (session.client == nullptr || !session.client->has_authoritative_inventory()) {
    return nullptr;
  }
  const usize selected_slot = session.client->selected_hotbar_slot();
  const player::Inventory& inventory = session.client->inventory();
  if (selected_slot >= inventory.slot_count()) {
    return nullptr;
  }
  const std::optional<player::ItemStack>& stack = inventory.slot(selected_slot);
  if (!stack.has_value() || stack->count == 0U) {
    return nullptr;
  }
  return assets_.item_registry.item(stack->item);
}

bool DesktopFrameLoop::selected_water_bucket_can_leave_empty_bucket(
    const DesktopGameSession& session) const noexcept {
  if (session.client == nullptr || !session.client->has_authoritative_inventory()) {
    return false;
  }
  const std::optional<player::ItemID> bucket_id =
      assets_.item_registry.find_item(base::ids::BucketItemID);
  if (!bucket_id.has_value()) {
    return false;
  }
  const player::ItemRecord* bucket = assets_.item_registry.item(bucket_id.value());
  if (bucket == nullptr) {
    return false;
  }

  const usize selected_slot = session.client->selected_hotbar_slot();
  const player::Inventory& inventory = session.client->inventory();
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

std::optional<BlockPos> DesktopFrameLoop::local_secondary_place_pos(
    const DesktopGameSession& session,
    const physics::ICollisionQuery& query) const {
  if (session.client == nullptr) {
    return std::nullopt;
  }
  auto hit = physics::raycast_blocks(
      query,
      session.client->camera().position(),
      session.client->camera().forward_vector(),
      config_.physics.reach_distance);
  if (!hit.has_value()) {
    return std::nullopt;
  }

  BlockPos place_pos = hit->block;
  if (!assets_.block_registry.is_replaceable(hit->variant)) {
    place_pos.value = place_pos.value + voxel::block_face_offset(hit->face);
  }
  if (!query.is_block_loaded(place_pos)) {
    return std::nullopt;
  }
  return place_pos;
}

bool DesktopFrameLoop::preview_block_placement_succeeds(
    const DesktopGameSession& session,
    const player::ItemRecord& item,
    const physics::ICollisionQuery& query) const {
  if (session.client == nullptr || !item.placement_variant.has_value()) {
    return false;
  }
  const std::optional<BlockPos> place_pos = local_secondary_place_pos(session, query);
  if (!place_pos.has_value()) {
    return false;
  }

  const voxel::BlockVariantID existing = session.client_world.get_block(place_pos.value());
  if (!(assets_.block_registry.is_air(existing) ||
        assets_.block_registry.is_replaceable(existing))) {
    return false;
  }

  const physics::KinematicConfig physics_config =
      make_local_placement_kinematic_config(config_.physics);
  const Aabb3f player_bounds = physics::make_body_bounds(
      session.client->player_position(),
      physics_config,
      physics::PlayerCollisionPose::Standing);
  for (const Aabb3f& local_box : assets_.block_registry.collision_boxes(
           item.placement_variant.value())) {
    Aabb3f world_box{
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

bool DesktopFrameLoop::preview_water_bucket_placement_succeeds(
    const DesktopGameSession& session,
    const physics::ICollisionQuery& query) const {
  if (!selected_water_bucket_can_leave_empty_bucket(session)) {
    return false;
  }
  const std::optional<BlockPos> place_pos = local_secondary_place_pos(session, query);
  if (!place_pos.has_value()) {
    return false;
  }

  const voxel::BlockVariantID existing_block =
      session.client_world.get_block(place_pos.value());
  if (!(assets_.block_registry.is_air(existing_block) ||
        assets_.block_registry.is_replaceable(existing_block))) {
    return false;
  }

  const voxel::FluidCell existing_fluid = session.client_world.get_fluid(place_pos.value());
  return existing_fluid.empty() ||
         (existing_fluid.type == water_fluid_.type && !existing_fluid.source());
}

bool DesktopFrameLoop::preview_held_item_placement_succeeds(
    const DesktopGameSession& session,
    const player::ItemRecord* item) const {
  if (session.client == nullptr || item == nullptr ||
      !session.client->resolved_permissions().core.can_modify_world) {
    return false;
  }

  world::WorldCollisionQuery query(session.client_world, assets_.block_registry, false);
  if (item->placement_variant.has_value()) {
    return preview_block_placement_succeeds(session, *item, query);
  }
  if (item->name == base::ids::WaterBucketItemID) {
    return preview_water_bucket_placement_succeeds(session, query);
  }
  return false;
}

Aabb3f DesktopFrameLoop::replicated_entity_attack_bounds(
    const client::ReplicatedEntityMirror& mirror) const noexcept {
  Vec3f box{1.0F, 1.0F, 1.0F};
  if (const entity::EntityTypeRecord* type = assets_.entity_registry.type(mirror.entity_type);
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

std::optional<EntityNetID> DesktopFrameLoop::targeted_attack_entity(
    const DesktopGameSession& session,
    const player::ItemRecord* held_item) const {
  if (session.client == nullptr) {
    return std::nullopt;
  }

  const client::CameraShell& camera = session.client->camera();
  const Vec3f origin = camera.position();
  const Vec3f direction = camera.forward_vector();
  f32 max_distance = config_.gameplay.combat.default_attack_range;
  if (held_item != nullptr && held_item->attack_data.has_value()) {
    max_distance = held_item->attack_data->attack_range;
  }

  world::WorldCollisionQuery query(
      session.client_world,
      assets_.block_registry,
      false);
  if (const std::optional<physics::BlockRaycastHit> block_hit = physics::raycast_blocks(
          query,
          origin,
          direction,
          max_distance);
      block_hit.has_value() && !assets_.block_registry.is_air(block_hit->variant)) {
    max_distance = std::min(max_distance, block_hit->distance);
  }

  std::optional<EntityNetID> best_entity;
  f32 best_distance = max_distance;
  const std::optional<EntityNetID> local_entity = session.client->local_entity_id();
  for (const client::ReplicatedEntityMirror& mirror :
       session.client->replicated_entity_mirrors()) {
    if ((local_entity.has_value() && mirror.entity_id == local_entity.value()) ||
        mirror.dead) {
      continue;
    }
    const std::optional<physics::RayAabbHit> hit = physics::ray_intersects_aabb(
        origin,
        direction,
        replicated_entity_attack_bounds(mirror),
        best_distance);
    if (hit.has_value() && hit->distance <= best_distance) {
      best_distance = hit->distance;
      best_entity = mirror.entity_id;
    }
  }
  return best_entity;
}

std::optional<EntityNetID> DesktopFrameLoop::targeted_rideable_entity(
    const DesktopGameSession& session) const {
  if (session.client == nullptr) {
    return std::nullopt;
  }

  const client::CameraShell& camera = session.client->camera();
  const Vec3f origin = camera.position();
  const Vec3f direction = camera.forward_vector();
  f32 max_distance = config_.physics.reach_distance + 1.5F;

  world::WorldCollisionQuery query(
      session.client_world,
      assets_.block_registry,
      false);
  if (const std::optional<physics::BlockRaycastHit> block_hit = physics::raycast_blocks(
          query,
          origin,
          direction,
          max_distance);
      block_hit.has_value() && !assets_.block_registry.is_air(block_hit->variant)) {
    max_distance = std::min(max_distance, block_hit->distance);
  }

  std::optional<EntityNetID> best_entity;
  f32 best_distance = max_distance;
  const std::optional<EntityNetID> local_entity = session.client->local_entity_id();
  for (const client::ReplicatedEntityMirror& mirror :
       session.client->replicated_entity_mirrors()) {
    if ((local_entity.has_value() && mirror.entity_id == local_entity.value()) ||
        mirror.dead) {
      continue;
    }
    const entity::EntityTypeRecord* type =
        assets_.entity_registry.type(mirror.entity_type);
    const bool type_is_rideable =
        type != nullptr && type->components.rideable.has_value();
    const bool definition_is_rideable =
        assets_.vehicle_registry.find(mirror.entity_type_name) != nullptr;
    if (!type_is_rideable && !definition_is_rideable) {
      continue;
    }
    const std::optional<physics::RayAabbHit> hit = physics::ray_intersects_aabb(
        origin,
        direction,
        replicated_entity_attack_bounds(mirror),
        best_distance);
    if (hit.has_value() && hit->distance <= best_distance) {
      best_distance = hit->distance;
      best_entity = mirror.entity_id;
    }
  }
  return best_entity;
}

bool DesktopFrameLoop::send_local_ride_request_if_targeted(
    DesktopGameSession& session) const {
  const std::optional<EntityNetID> target = targeted_rideable_entity(session);
  if (!target.has_value()) {
    return false;
  }
  return static_cast<bool>(session.client->request_ride_entity(target.value()));
}

void DesktopFrameLoop::send_local_entity_attack_if_targeted(
    DesktopGameSession& session,
    const player::ItemRecord* held_item) const {
  const std::optional<EntityNetID> target = targeted_attack_entity(session, held_item);
  if (!target.has_value()) {
    return;
  }
  (void)session.client->request_attack_entity(target.value());
  (void)session.client->drain_attack_feedback();
}

bool DesktopFrameLoop::held_item_animation_condition_passes(
    const DesktopGameSession& session,
    const player::ItemRecord* item,
    player::HeldItemAnimationPlayCondition condition) const {
  switch (condition) {
    case player::HeldItemAnimationPlayCondition::Always:
      return true;
    case player::HeldItemAnimationPlayCondition::PlacementSucceeds:
      return preview_held_item_placement_succeeds(session, item);
  }
  return false;
}

HeldItemActionPlayResult DesktopFrameLoop::play_held_item_action(
    DesktopGameSession& session,
    const player::ItemRecord* item,
    std::string_view action,
    Seconds now_seconds) const noexcept {
  const client::HeldItemAnimationResolution animation =
      client::resolve_held_item_animation_binding(
          item,
          action,
          assets_.item_animation_library);
  if (animation.clip == nullptr) {
    return animation.disabled
               ? HeldItemActionPlayResult::Suppressed
               : HeldItemActionPlayResult::NoAnimation;
  }
  if (!held_item_animation_condition_passes(session, item, animation.play_condition)) {
    return HeldItemActionPlayResult::Suppressed;
  }
  session.held_item_animation.play(*animation.clip, now_seconds);
  return HeldItemActionPlayResult::Played;
}

void DesktopFrameLoop::spawn_local_block_face_particles(
    DesktopGameSession& session,
    u16 count,
    f32 extent,
    f32 radius) const {
  if (session.client == nullptr) {
    return;
  }

  world::WorldCollisionQuery query(session.client_world, assets_.block_registry, false);
  const client::CameraShell& camera = session.client->camera();
  const std::optional<physics::BlockRaycastHit> hit = physics::raycast_blocks(
      query,
      camera.position(),
      camera.forward_vector(),
      config_.physics.reach_distance);
  if (!hit.has_value() || assets_.block_registry.is_air(hit->variant)) {
    return;
  }
  const voxel::BlockVariantRecord* variant = assets_.block_registry.variant(hit->variant);

  const Vec3i offset = voxel::block_face_offset(hit->face);
  const Vec3f normal{
      static_cast<f32>(offset.x),
      static_cast<f32>(offset.y),
      static_cast<f32>(offset.z)};
  client::ParticleBurstDesc desc;
  desc.type_name = "arvane:block_dust";
  if (variant != nullptr) {
    desc.texture_asset = variant->particle_texture_asset;
  }
  desc.position = hit->hit_point + normal * 0.035F;
  desc.normal = normal;
  desc.extents = {extent, extent, 0.0F};
  desc.radius = radius;
  desc.count = count;
  desc.seed = next_particle_seed(session, hit->block);
  desc.shape = protocol::ParticleSpawnShape::BlockFace;
  (void)session.client->spawn_particles(desc);
}

void DesktopFrameLoop::spawn_local_block_hit_particles(DesktopGameSession& session) const {
  spawn_local_block_face_particles(session, 5U, 0.22F, 0.025F);
}

void DesktopFrameLoop::update_sprint_ground_particles(
    DesktopGameSession& session,
    Seconds delta_seconds) const {
  if (session.client == nullptr) {
    return;
  }

  const client::InputFrame& input = session.last_gameplay_input;
  const Vec3f velocity = session.client->player_velocity();
  const f32 horizontal_speed_sq = velocity.x * velocity.x + velocity.z * velocity.z;
  const bool sprinting_on_ground =
      input.sprint_held &&
      input.move_forward &&
      !input.move_backward &&
      !input.sneak_held &&
      session.client->player_grounded() &&
      horizontal_speed_sq > 1.0F;
  if (!sprinting_on_ground) {
    session.sprint_particle_accumulator = 0.0;
    return;
  }

  session.sprint_particle_accumulator += std::max<Seconds>(delta_seconds, 0.0);
  if (session.sprint_particle_accumulator < SprintParticleIntervalSeconds) {
    return;
  }
  session.sprint_particle_accumulator =
      std::fmod(session.sprint_particle_accumulator, SprintParticleIntervalSeconds);

  world::WorldCollisionQuery query(session.client_world, assets_.block_registry, false);
  const Vec3f origin = session.client->player_position() + Vec3f{0.0F, 0.08F, 0.0F};
  const std::optional<physics::BlockRaycastHit> hit = physics::raycast_blocks(
      query,
      origin,
      {0.0F, -1.0F, 0.0F},
      0.35F);
  if (!hit.has_value() || assets_.block_registry.is_air(hit->variant)) {
    return;
  }
  const voxel::BlockVariantRecord* variant = assets_.block_registry.variant(hit->variant);

  client::ParticleBurstDesc desc;
  desc.type_name = "arvane:block_dust";
  if (variant != nullptr) {
    desc.texture_asset = variant->particle_texture_asset;
  }
  desc.position = hit->hit_point + Vec3f{0.0F, 0.035F, 0.0F};
  desc.normal = {0.0F, 1.0F, 0.0F};
  desc.extents = {0.28F, 0.28F, 0.0F};
  desc.radius = 0.02F;
  desc.count = 3U;
  desc.seed = next_particle_seed(session, hit->block);
  desc.shape = protocol::ParticleSpawnShape::BlockFace;
  (void)session.client->spawn_particles(desc);
}

void DesktopFrameLoop::update_held_item_animation_from_input(
    DesktopGameSession& session,
    const client::InputFrame& client_input,
    Seconds now_seconds) const {
  if (session.client == nullptr ||
      session.client->inventory_open() ||
      session.client->chat_open()) {
    session.held_item_secondary_use_active = false;
    return;
  }

  const player::ItemRecord* held_item = selected_held_item(session);
  const bool mining_block =
      client_input.primary_held &&
      session.client->block_break_progress().active;
  if (mining_block) {
    if (now_seconds >= session.next_block_mining_particle_seconds) {
      spawn_local_block_face_particles(session, 2U, 0.18F, 0.02F);
      session.next_block_mining_particle_seconds =
          now_seconds + BlockMiningParticleIntervalSeconds;
    }
  } else {
    session.next_block_mining_particle_seconds = 0.0;
  }

  if (held_item == nullptr) {
    if (client_input.primary_pressed) {
      spawn_local_block_hit_particles(session);
      send_local_entity_attack_if_targeted(session, nullptr);
    }
    if (client_input.mount_pressed &&
        send_local_ride_request_if_targeted(session)) {
      session.held_item_secondary_use_active = false;
      session.held_item_animation.stop(now_seconds);
      return;
    }
    session.held_item_secondary_use_active = false;
    session.held_item_animation.stop(now_seconds);
    return;
  }

  if (client_input.primary_pressed) {
    spawn_local_block_hit_particles(session);
    (void)play_held_item_action(
        session,
        held_item,
        client::HeldItemActionPrimaryUse,
        now_seconds);
    send_local_entity_attack_if_targeted(session, held_item);
  }

  if (client_input.mount_pressed &&
      send_local_ride_request_if_targeted(session)) {
    session.held_item_secondary_use_active = false;
    session.held_item_animation.stop(now_seconds);
    return;
  }

  if (client_input.primary_held &&
      session.client->block_break_progress().active &&
      session.held_item_animation.finished(now_seconds)) {
    (void)play_held_item_action(
        session,
        held_item,
        client::HeldItemActionPrimaryUse,
        now_seconds);
  }

  if (client_input.secondary_pressed) {
    HeldItemActionPlayResult result = play_held_item_action(
        session,
        held_item,
        client::HeldItemActionSecondaryUseStart,
        now_seconds);
    if (result == HeldItemActionPlayResult::NoAnimation) {
      result = play_held_item_action(
          session,
          held_item,
          client::HeldItemActionSecondaryUseHold,
          now_seconds);
    }
    session.held_item_secondary_use_active =
        result != HeldItemActionPlayResult::Suppressed;
  }

  if (client_input.secondary_held && session.held_item_secondary_use_active &&
      session.held_item_animation.finished(now_seconds)) {
    const client::HeldItemAnimationResolution hold_animation =
        client::resolve_held_item_animation_binding(
            held_item,
            client::HeldItemActionSecondaryUseHold,
            assets_.item_animation_library);
    if (hold_animation.clip != nullptr &&
        held_item_animation_condition_passes(session, held_item, hold_animation.play_condition) &&
        session.held_item_animation.active_clip() != hold_animation.clip) {
      session.held_item_animation.play(*hold_animation.clip, now_seconds);
    }
  }

  if (!client_input.secondary_held && session.held_item_secondary_use_active) {
    session.held_item_secondary_use_active = false;
    if (play_held_item_action(
            session,
            held_item,
            client::HeldItemActionSecondaryUseEnd,
            now_seconds) != HeldItemActionPlayResult::Played) {
      session.held_item_animation.stop(now_seconds);
    }
  }
}

void DesktopFrameLoop::update_block_outline_target(DesktopGameSession& active_session) {
  active_session.block_outline_target.reset();
  world::WorldCollisionQuery query(
      active_session.client_world,
      assets_.block_registry,
      false);
  const client::CameraShell& camera = active_session.client->camera();
  const std::optional<physics::BlockRaycastHit> hit = physics::raycast_blocks(
      query,
      camera.position(),
      camera.forward_vector(),
      config_.physics.reach_distance);
  if (!hit.has_value() || assets_.block_registry.is_air(hit->variant)) {
    return;
  }
  active_session.block_outline_target = BlockOutlineTarget{hit->block, hit->variant};
}

} // namespace arv::desktop
