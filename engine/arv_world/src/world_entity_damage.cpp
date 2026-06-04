#include "world_runtime_internal.hpp"

#include <algorithm>
#include <cmath>

namespace arv::world {
namespace {

[[nodiscard]] bool finite_location(Location location) noexcept {
  return std::isfinite(location.x) &&
         std::isfinite(location.y) &&
         std::isfinite(location.z) &&
         std::isfinite(location.yaw_radians) &&
         std::isfinite(location.pitch_radians);
}

[[nodiscard]] Location normalized_respawn_location(
    const player::PlayerState& player,
    Location world_spawn,
    WorldID world_id) noexcept {
  if (!world_spawn.world.has_value()) {
    world_spawn.world = world_id;
  }
  if (!player.respawn_location.has_value()) {
    return world_spawn;
  }

  Location respawn = player.respawn_location.value();
  if (!respawn.world.has_value()) {
    respawn.world = world_id;
  }
  if (respawn.world.value() != world_id || !finite_location(respawn)) {
    return world_spawn;
  }
  return respawn;
}

void respawn_player(
    WorldRuntime& world,
    PlayerID player_id,
    EntityID entity_id,
    entity::EntityHealth& health) noexcept {
  player::PlayerState* player = world.find_player(player_id);
  if (player == nullptr) {
    health.current = health.max;
    return;
  }

  const Location respawn =
      normalized_respawn_location(*player, world.spawn_point(), world.id());
  player->position = location_position(respawn);
  player->velocity = {};
  player->yaw_radians = respawn.yaw_radians;
  player->pitch_radians = respawn.pitch_radians;
  player->grounded = false;
  player->sneaking = false;
  player->swimming = false;
  player->knockback_control_suppression_ticks = 0U;
  health.current = health.max;

  (void)world.set_entity_position(entity_id, player->position);
  (void)world.set_player_visual_pose(
      player_id,
      player->yaw_radians,
      player->pitch_radians,
      0.0F);
}

} // namespace

DamageResult WorldRuntime::apply_damage(const DamageRequest& request) noexcept {
  assert_owner_thread();
  DamageResult result;
  entity::EntityHealth* health = find_entity_health(request.target);
  if (health == nullptr) {
    return result;
  }

  result.previous_health = health->current;
  result.new_health = health->current;

  if (health->invulnerable || health->current <= 0.0F ||
      !std::isfinite(request.amount) || request.amount <= 0.0F) {
    return result;
  }

  health->current = std::clamp(health->current - request.amount, 0.0F, health->max);
  result.applied = health->current < result.previous_health;
  result.killed = result.previous_health > 0.0F && health->current <= 0.0F;
  if (result.killed) {
    if (const std::optional<PlayerID> player_id = player_for_entity(request.target);
        player_id.has_value()) {
      respawn_player(*this, player_id.value(), request.target, *health);
    } else {
      (void)destroy_entity(request.target, EntityDespawnReason::Removed);
    }
  }
  result.new_health = health->current;
  return result;
}

} // namespace arv::world
