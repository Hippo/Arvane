#include "desktop_frame_loop.hpp"

#include "desktop_runtime_config.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/client/gameplay_interactions.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/world/world_queries.hpp"

namespace arv::desktop {

const player::ItemRecord* DesktopFrameLoop::selected_held_item(
    const DesktopGameSession& session) const noexcept {
  if (session.client == nullptr || !session.client->has_authoritative_inventory()) {
    return nullptr;
  }
  return client::selected_inventory_item(
      session.client->inventory(),
      session.client->selected_hotbar_slot(),
      assets_.item_registry);
}

bool DesktopFrameLoop::preview_held_item_placement_succeeds(
    const DesktopGameSession& session,
    const player::ItemRecord* item) const {
  if (session.client == nullptr || item == nullptr ||
      !session.client->resolved_permissions().core.can_modify_world) {
    return false;
  }

  world::WorldCollisionQuery query(session.client_world, assets_.block_registry, false);
  return client::held_item_placement_preview_succeeds(
      {.client = *session.client,
       .collision_query = query,
       .block_registry = assets_.block_registry,
       .item_registry = assets_.item_registry,
       .config = {.reach_distance = config_.physics.reach_distance,
                  .player_kinematics =
                      make_local_placement_kinematic_config(config_.physics),
                  .water_fluid_type = water_fluid_.type,
                  .empty_bucket_item_name = base::ids::BucketItemID,
                  .water_bucket_item_name = base::ids::WaterBucketItemID}},
      item);
}

std::optional<EntityNetID> DesktopFrameLoop::targeted_rideable_entity(
    const DesktopGameSession& session) const {
  if (session.client == nullptr) {
    return std::nullopt;
  }

  world::WorldCollisionQuery query(
      session.client_world,
      assets_.block_registry,
      false);
  return client::targeted_rideable_entity(
      {*session.client,
       query,
       assets_.block_registry,
       assets_.entity_registry,
       assets_.vehicle_registry},
      config_.physics.reach_distance + 1.5F);
}

client::GameplayInteractionDispatchResult
DesktopFrameLoop::dispatch_gameplay_interactions_from_input(
    DesktopGameSession& session,
    const client::InputFrame& client_input,
    bool gameplay_blocked) const {
  if (session.client == nullptr) {
    return {};
  }
  world::WorldCollisionQuery query(
      session.client_world,
      assets_.block_registry,
      false);
  return client::dispatch_gameplay_interactions(
      {.client = *session.client,
       .collision_query = query,
       .block_registry = assets_.block_registry,
       .entity_registry = assets_.entity_registry,
       .vehicle_registry = assets_.vehicle_registry,
       .held_item = selected_held_item(session),
       .gameplay_blocked = gameplay_blocked,
       .secondary_pressed = client_input.secondary_pressed,
       .primary_pressed = client_input.primary_pressed,
       .mount_pressed = client_input.mount_pressed,
       .reach_distance = config_.physics.reach_distance,
       .default_attack_range = config_.gameplay.combat.default_attack_range,
       .ride_range = config_.physics.reach_distance + 1.5F});
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
  const std::optional<client::ParticleBurstDesc> burst =
      client::build_local_block_face_particle_burst(
          {
              .collision_query = query,
              .block_registry = assets_.block_registry,
              .seed_state = session.particle_seeds,
              .origin = camera.position(),
              .direction = camera.forward_vector(),
              .max_distance = config_.physics.reach_distance,
              .count = count,
              .extent = extent,
              .radius = radius,
          });
  if (burst.has_value()) {
    (void)session.client->spawn_particles(burst.value());
  }
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
  world::WorldCollisionQuery query(session.client_world, assets_.block_registry, false);
  const std::optional<client::ParticleBurstDesc> burst =
      client::update_local_sprint_ground_particle_burst(
          session.sprint_ground_particles,
          {
              .collision_query = query,
              .block_registry = assets_.block_registry,
              .seed_state = session.particle_seeds,
              .sprint_held = input.sprint_held,
              .move_forward = input.move_forward,
              .move_backward = input.move_backward,
              .sneak_held = input.sneak_held,
              .player_grounded = session.client->player_grounded(),
              .player_position = session.client->player_position(),
              .player_velocity = velocity,
              .delta_seconds = delta_seconds,
          });
  if (burst.has_value()) {
    (void)session.client->spawn_particles(burst.value());
  }
}

void DesktopFrameLoop::update_held_item_animation_from_input(
    DesktopGameSession& session,
    const client::InputFrame& client_input,
    Seconds now_seconds,
    bool stop_for_ride_request) const {
  if (session.client == nullptr ||
      session.client->inventory_open() ||
      session.client->chat_open()) {
    (void)client::update_held_item_animation_playback_from_frame(
        session.held_item_animation,
        session.held_item_secondary_use_active,
        assets_.item_animation_library,
        {
            .animations_available = false,
            .unavailable_policy =
                client::HeldItemAnimationUnavailablePolicy::ClearSecondaryUseOnly,
            .now_seconds = now_seconds,
        });
    return;
  }

  const player::ItemRecord* held_item = selected_held_item(session);
  const bool block_break_active = session.client->block_break_progress().active;
  const bool mining_block =
      client_input.primary_held && block_break_active;
  if (client::consume_local_block_mining_particle_due(
          session.block_mining_particles,
          mining_block,
          now_seconds)) {
    spawn_local_block_face_particles(session, 2U, 0.18F, 0.02F);
  }

  if (held_item == nullptr) {
    if (client_input.primary_pressed) {
      spawn_local_block_hit_particles(session);
    }
    (void)client::update_held_item_animation_playback_from_frame(
        session.held_item_animation,
        session.held_item_secondary_use_active,
        assets_.item_animation_library,
        {
            .held_item = nullptr,
            .now_seconds = now_seconds,
        });
    return;
  }

  if (client_input.primary_pressed) {
    spawn_local_block_hit_particles(session);
  }

  (void)client::update_held_item_animation_playback_from_frame(
      session.held_item_animation,
      session.held_item_secondary_use_active,
      assets_.item_animation_library,
      {
          .primary_pressed = client_input.primary_pressed,
          .primary_held = client_input.primary_held,
          .secondary_pressed = client_input.secondary_pressed,
          .secondary_held = client_input.secondary_held,
          .block_break_active = block_break_active,
          .force_stop = stop_for_ride_request,
          .held_item = held_item,
          .condition_state =
              {
                  .placement_succeeds =
                      preview_held_item_placement_succeeds(session, held_item),
              },
          .now_seconds = now_seconds,
      });
}

void DesktopFrameLoop::update_block_outline_target(DesktopGameSession& active_session) {
  active_session.block_outline_target.reset();
  world::WorldCollisionQuery query(
      active_session.client_world,
      assets_.block_registry,
      false);
  const client::CameraShell& camera = active_session.client->camera();
  active_session.block_outline_target = client::find_block_outline_target(
      query,
      assets_.block_registry,
      camera.position(),
      camera.forward_vector(),
      config_.physics.reach_distance);
}

} // namespace arv::desktop
