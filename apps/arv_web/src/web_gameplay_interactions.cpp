#include "web_gameplay_interactions.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/client/chunk_collision_query.hpp"
#include "arv/client/client_input_mapping.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/gameplay_interactions.hpp"
#include "arv/client/gameplay_ui_model.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/client/particles.hpp"
#include "arv/io/config.hpp"
#include "arv/player/inventory.hpp"
#include "arv/player/player_constants.hpp"
#include "web_application.hpp"

#include <optional>
#include <string>

namespace arv::web {
namespace {

[[nodiscard]] const player::ItemRecord* selected_web_held_item() noexcept {
  if (g_app.session == nullptr ||
      !g_app.session->has_authoritative_inventory() ||
      !g_app.entity_assets.ready) {
    return nullptr;
  }
  return client::selected_inventory_item(
      g_app.session->inventory(),
      g_app.session->selected_hotbar_slot(),
      g_app.entity_assets.loaded.item_registry);
}

[[nodiscard]] bool preview_web_held_item_placement_succeeds(
    const player::ItemRecord* item) {
  if (g_app.session == nullptr ||
      item == nullptr ||
      !g_app.block_assets.ready ||
      !g_app.session->resolved_permissions().core.can_modify_world) {
    return false;
  }

  const client::ChunkSectionCollisionQuery query(
      g_app.chunk_stream.sections(),
      g_app.block_assets.loaded.block_registry);
  return client::held_item_placement_preview_succeeds(
      {.client = *g_app.session,
       .collision_query = query,
       .block_registry = g_app.block_assets.loaded.block_registry,
       .item_registry = g_app.entity_assets.loaded.item_registry,
       .config = {.empty_bucket_item_name = base::ids::BucketItemID,
                  .water_bucket_item_name = base::ids::WaterBucketItemID}},
      item);
}

[[nodiscard]] bool web_entity_interactions_available() noexcept {
  return g_app.session != nullptr &&
         g_app.block_assets.ready &&
         g_app.entity_assets.ready;
}

[[nodiscard]] bool web_local_block_particles_available() noexcept {
  return g_app.session != nullptr && g_app.block_assets.ready;
}

[[nodiscard]] f32 web_default_attack_range() noexcept {
  return io::GameplayConfig{}.combat.default_attack_range;
}

[[nodiscard]] f32 web_ride_target_range() noexcept {
  return player::DefaultReachDistance + 1.5F;
}

[[nodiscard]] std::optional<EntityNetID> targeted_web_rideable_entity() {
  if (!web_entity_interactions_available()) {
    return std::nullopt;
  }
  const client::ChunkSectionCollisionQuery query(
      g_app.chunk_stream.sections(),
      g_app.block_assets.loaded.block_registry);
  return client::targeted_rideable_entity(
      {*g_app.session,
       query,
       g_app.block_assets.loaded.block_registry,
       g_app.entity_assets.loaded.entity_registry,
       g_app.entity_assets.loaded.vehicle_registry},
      web_ride_target_range());
}

void spawn_web_local_block_face_particles(u16 count, f32 extent, f32 radius) {
  if (!web_local_block_particles_available()) {
    return;
  }

  const client::ChunkSectionCollisionQuery query(
      g_app.chunk_stream.sections(),
      g_app.block_assets.loaded.block_registry);
  const client::CameraShell& camera = g_app.session->camera();
  const std::optional<client::ParticleBurstDesc> burst =
      client::build_local_block_face_particle_burst(
          {
              .collision_query = query,
              .block_registry = g_app.block_assets.loaded.block_registry,
              .seed_state = g_app.input.particle_seeds,
              .origin = camera.position(),
              .direction = camera.forward_vector(),
              .max_distance = player::DefaultReachDistance,
              .count = count,
              .extent = extent,
              .radius = radius,
          });
  if (burst.has_value()) {
    (void)g_app.session->spawn_particles(burst.value());
  }
}

} // namespace

void send_web_entity_interactions_from_input(
    client::InputFrame& input,
    Seconds now_seconds) {
  if (!web_entity_interactions_available()) {
    return;
  }

  const client::ChunkSectionCollisionQuery query(
      g_app.chunk_stream.sections(),
      g_app.block_assets.loaded.block_registry);
  const client::GameplayInteractionDispatchResult interactions =
      client::dispatch_gameplay_interactions(
          {.client = *g_app.session,
           .collision_query = query,
           .block_registry = g_app.block_assets.loaded.block_registry,
           .entity_registry = g_app.entity_assets.loaded.entity_registry,
           .vehicle_registry = g_app.entity_assets.loaded.vehicle_registry,
           .held_item = selected_web_held_item(),
           .gameplay_blocked = g_app.ui.screen_manager.blocks_gameplay_input(),
           .secondary_pressed = input.secondary_pressed,
           .primary_pressed = input.primary_pressed,
           .mount_pressed = input.mount_pressed,
           .reach_distance = player::DefaultReachDistance,
           .default_attack_range = web_default_attack_range(),
           .ride_range = web_ride_target_range()});
  if (interactions.consume_secondary_use) {
    input.secondary_pressed = false;
    input.secondary_held = false;
    g_app.input.secondary_animation_pressed = false;
  }
  if (interactions.stop_held_item_animation) {
    client::stop_held_item_animation_playback(
        g_app.input.held_item_animation,
        g_app.input.held_item_secondary_use_active,
        now_seconds);
  }
}

void update_web_held_item_animation_from_input(
    const client::InputFrame& input,
    Seconds now_seconds) {
  if (g_app.session == nullptr) {
    (void)client::update_held_item_animation_playback_from_frame(
        g_app.input.held_item_animation,
        g_app.input.held_item_secondary_use_active,
        g_app.entity_assets.item_animation_library,
        {
            .animations_available = false,
            .now_seconds = now_seconds,
        });
    return;
  }

  const bool playback_available =
      g_app.entity_assets.ready &&
      g_app.entity_assets.item_animation_library.clip_count() != 0U &&
      g_app.session->has_authoritative_inventory();
  const player::ItemRecord* held_item =
      playback_available ? selected_web_held_item() : nullptr;
  const client::HeldItemAnimationConditionState predicted_action{
      .placement_succeeds =
          held_item != nullptr && preview_web_held_item_placement_succeeds(held_item)};
  (void)client::update_held_item_animation_playback_from_frame(
      g_app.input.held_item_animation,
      g_app.input.held_item_secondary_use_active,
      g_app.entity_assets.item_animation_library,
      {
          .primary_pressed = input.primary_pressed,
          .primary_held = input.primary_held,
          .secondary_pressed = input.secondary_pressed,
          .secondary_held = input.secondary_held,
          .block_break_active = g_app.session->block_break_progress().active,
          .animations_available = playback_available,
          .held_item = held_item,
          .condition_state = predicted_action,
          .now_seconds = now_seconds,
      });
}

void update_web_local_particles_from_input(
    const client::InputFrame& input,
    Seconds now_seconds,
    Seconds delta_seconds) {
  if (!web_local_block_particles_available()) {
    return;
  }

  const bool mining_block =
      input.primary_held && g_app.session->block_break_progress().active;
  if (client::consume_local_block_mining_particle_due(
          g_app.input.block_mining_particles,
          mining_block,
          now_seconds)) {
    spawn_web_local_block_face_particles(2U, 0.18F, 0.02F);
  }

  if (input.primary_pressed) {
    spawn_web_local_block_face_particles(5U, 0.22F, 0.025F);
  }

  const client::ChunkSectionCollisionQuery query(
      g_app.chunk_stream.sections(),
      g_app.block_assets.loaded.block_registry);
  const std::optional<client::ParticleBurstDesc> sprint_burst =
      client::update_local_sprint_ground_particle_burst(
          g_app.input.sprint_ground_particles,
          {
              .collision_query = query,
              .block_registry = g_app.block_assets.loaded.block_registry,
              .seed_state = g_app.input.particle_seeds,
              .sprint_held = input.sprint_held,
              .move_forward = input.move_forward,
              .move_backward = input.move_backward,
              .sneak_held = input.sneak_held,
              .player_grounded = g_app.session->player_grounded(),
              .player_position = g_app.session->player_position(),
              .player_velocity = g_app.session->player_velocity(),
              .delta_seconds = delta_seconds,
          });
  if (sprint_burst.has_value()) {
    (void)g_app.session->spawn_particles(sprint_burst.value());
  }
}

ui::RidePromptModel web_ride_prompt_model() {
  const bool interactions_available = web_entity_interactions_available();
  const std::string key_label =
      interactions_available
          ? client::first_action_binding_label(
                g_app.input_bindings,
                platform::LogicalAction::Mount)
          : std::string{};
  const auto has_targeted_rideable = []() -> bool {
    return targeted_web_rideable_entity().has_value();
  };
  return client::build_ride_prompt_model(
      {
          .interactions_available = interactions_available,
          .gameplay_blocked = g_app.ui.screen_manager.blocks_gameplay_input(),
          .inventory_open =
              g_app.session != nullptr && g_app.session->inventory_open(),
          .chat_open = g_app.session != nullptr && g_app.session->chat_open(),
          .local_player_mounted =
              g_app.session != nullptr &&
              client::local_player_is_mounted(*g_app.session),
          .key_label = key_label,
          .has_targeted_rideable = has_targeted_rideable,
      });
}

} // namespace arv::web
