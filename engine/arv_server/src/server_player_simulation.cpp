#include "server_runtime_internal.hpp"

#include "arv/physics/player_movement.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace arv::server::detail {
namespace {

inline constexpr f32 KnockbackSuppressedInputBlend = 0.25F;

enum class PlayerHand : u8 {
  Right,
  Left,
};

void restart_player_entity_animation(
    world::WorldRuntime& world,
    PlayerID player_id,
    std::string_view animation_name) {
  const std::optional<EntityID> entity_id = world.entity_for_player(player_id);
  if (!entity_id.has_value()) {
    return;
  }
  (void)world.play_entity_animation(
      entity_id.value(),
      std::string{animation_name});
}

[[nodiscard]] PlayerHand selected_player_action_hand(
    const player::PlayerState& /*player_state*/) noexcept {
  // The selected hotbar item is currently the only held-item slot, and it is
  // carried in the right hand. Offhand support should branch here.
  return PlayerHand::Right;
}

[[nodiscard]] std::string_view player_hit_animation_name(PlayerHand hand) noexcept {
  switch (hand) {
    case PlayerHand::Right:
      return "hit_right";
    case PlayerHand::Left:
      return "hit_left";
  }
  return "hit_right";
}

[[nodiscard]] physics::PlayerCollisionPose player_collision_pose(
    const player::PlayerState& player_state) noexcept {
  if (player_state.swimming) {
    return physics::PlayerCollisionPose::Swimming;
  }
  return player_state.sneaking ? physics::PlayerCollisionPose::Sneaking
                               : physics::PlayerCollisionPose::Standing;
}

[[nodiscard]] bool command_is_sprinting_forward(
    const player::PlayerCommand& command) noexcept {
  return command.sprint_held && command.move_z > 0;
}

void update_sprint_knockback_state(
    PlayerCombatState& combat_state,
    const player::PlayerCommand& command) noexcept {
  const bool sprinting_forward = command_is_sprinting_forward(command);
  if (sprinting_forward && !combat_state.was_sprinting_forward_last_tick) {
    combat_state.sprint_knockback_ready = true;
  }
  combat_state.was_sprinting_forward_last_tick = sprinting_forward;
}

void apply_knockback_control_suppression(
    player::PlayerState& player_state,
    const world::WorldCollisionQuery& collision_query,
    const physics::KinematicConfig& physics_config,
    Vec3f incoming_velocity,
    Seconds tick_seconds) noexcept {
  if (player_state.knockback_control_suppression_ticks == 0U) {
    return;
  }

  const Vec3f simulated_horizontal{
      player_state.velocity.x,
      0.0F,
      player_state.velocity.z};
  const Vec3f incoming_horizontal{
      incoming_velocity.x,
      0.0F,
      incoming_velocity.z};
  Vec3f blended_horizontal =
      incoming_horizontal * (1.0F - KnockbackSuppressedInputBlend) +
      simulated_horizontal * KnockbackSuppressedInputBlend;
  Vec3f displacement{
      (blended_horizontal.x - simulated_horizontal.x) * tick_seconds,
      0.0F,
      (blended_horizontal.z - simulated_horizontal.z) * tick_seconds};

  if (player_state.resolved_permissions.core.has_collision) {
    const physics::MovementResolution movement = physics::resolve_movement(
        collision_query,
        physics::make_body_bounds(
            player_state.position,
            physics_config,
            player_collision_pose(player_state)),
        displacement,
        physics_config.collision_epsilon);
    displacement = movement.applied_delta;
    if (movement.hit_x) {
      blended_horizontal.x = 0.0F;
    }
    if (movement.hit_z) {
      blended_horizontal.z = 0.0F;
    }
  }

  player_state.position = player_state.position + displacement;
  player_state.velocity.x = blended_horizontal.x;
  player_state.velocity.z = blended_horizontal.z;
  --player_state.knockback_control_suppression_ticks;
}

[[nodiscard]] u64 hash_append_byte(u64 hash, u8 value) noexcept {
  hash ^= value;
  hash *= 1099511628211ULL;
  return hash;
}

[[nodiscard]] u64 hash_append_u64(u64 hash, u64 value) noexcept {
  for (u32 index = 0; index < 8U; ++index) {
    hash = hash_append_byte(hash, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
  }
  return hash;
}

[[nodiscard]] u64 hash_append_string(u64 hash, std::string_view value) noexcept {
  hash = hash_append_u64(hash, value.size());
  for (const char ch : value) {
    hash = hash_append_byte(hash, static_cast<u8>(ch));
  }
  return hash;
}

[[nodiscard]] u64 hash_append_tri_state(u64 hash, player::TriState value) noexcept {
  return hash_append_byte(hash, static_cast<u8>(value));
}

[[nodiscard]] u64 hash_append_core_permissions(
    u64 hash,
    const player::CorePermissionSet& core) noexcept {
  hash = hash_append_tri_state(hash, core.can_take_damage);
  hash = hash_append_tri_state(hash, core.can_modify_world);
  hash = hash_append_tri_state(hash, core.can_interact);
  hash = hash_append_tri_state(hash, core.can_open_inventory);
  hash = hash_append_tri_state(hash, core.has_creative_inventory);
  hash = hash_append_tri_state(hash, core.can_fly);
  hash = hash_append_tri_state(hash, core.has_collision);
  hash = hash_append_tri_state(hash, core.is_visible_to_others);
  return hash;
}

[[nodiscard]] u64 permission_source_fingerprint(
    const player::PlayerState& player_state) noexcept {
  u64 hash = 14695981039346656037ULL;
  hash = hash_append_string(hash, player_state.game_mode_id);
  hash = hash_append_u64(hash, player_state.permission_overrides.groups.size());
  for (const std::string& group : player_state.permission_overrides.groups) {
    hash = hash_append_string(hash, group);
  }
  hash = hash_append_core_permissions(hash, player_state.permission_overrides.overrides);
  hash = hash_append_u64(hash, player_state.permission_overrides.custom_overrides.size());
  for (const auto& [node, value] : player_state.permission_overrides.custom_overrides) {
    hash = hash_append_string(hash, node);
    hash = hash_append_tri_state(hash, value);
  }
  return hash;
}

} // namespace

Vec3f player_look_direction(const player::PlayerState& player_state) noexcept {
  return look_direction(player_state.yaw_radians, player_state.pitch_radians);
}

ChunkSectionCoord player_chunk_coord(const player::PlayerState& player_state) noexcept {
  return block_to_chunk({{
      static_cast<i32>(std::floor(player_state.position.x)),
      static_cast<i32>(std::floor(player_state.position.y)),
      static_cast<i32>(std::floor(player_state.position.z)),
  }});
}

Vec3f player_eye_position(
    const player::PlayerState& player_state,
    const player::PlayerTuning& tuning) noexcept {
  const f32 eye_height =
      player_state.swimming
          ? tuning.swimming_eye_height
          : (player_state.sneaking ? tuning.sneaking_eye_height
                                   : tuning.standing_eye_height);
  return {
      player_state.position.x,
      player_state.position.y + eye_height,
      player_state.position.z};
}

bool ServerRuntimeImpl::set_player_game_mode(PlayerID player_id, std::string_view mode_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || game_modes_.find(mode_id) == nullptr) {
    return false;
  }

  player_state->game_mode_id = std::string{mode_id};
  refresh_player_permissions(*player_state);
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session != nullptr && session->player_id == player_id) {
      send_player_permissions(*session, *player_state);
      break;
    }
  }
  return true;
}

bool ServerRuntimeImpl::set_player_fly_speed(PlayerID player_id, f32 fly_speed) {
  if (!std::isfinite(fly_speed) ||
      fly_speed <= 0.0F ||
      fly_speed > movement_.max_fly_speed_blocks_per_second) {
    return false;
  }

  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr) {
    return false;
  }

  player_state->fly_speed = fly_speed;
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session != nullptr && session->player_id == player_id) {
      send_player_movement_tuning(*session, *player_state);
      break;
    }
  }
  return true;
}

void ServerRuntimeImpl::tick_players() {
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr) {
      continue;
    }

    player::PlayerState* player_state = default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    std::optional<player::PlayerCommand> pending_command;
    if (auto found = pending_player_commands_.find(session->player_id);
        found != pending_player_commands_.end()) {
      pending_command = found->second;
      pending_player_commands_.erase(found);
    }
    if (!pending_command.has_value()) {
      continue;
    }
    const player::PlayerCommand& command = pending_command.value();

    update_sprint_knockback_state(combat_states_[session_id], command);
    apply_player_action_command(*player_state, command);
    player_state->last_processed_input_tick = command.client_tick;
  }
}

void ServerRuntimeImpl::update_world_simulation_activity() {
  std::vector<ChunkSectionCoord> centers;
  centers.reserve(sessions_.size());
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || session->world_id != default_world_.id()) {
      continue;
    }

    const player::PlayerState* player_state =
        default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    centers.emplace_back(player_chunk_coord(*player_state));
  }

  default_world_.update_simulation_radii(centers, simulation_radius_);
}

void ServerRuntimeImpl::refresh_player_permissions(player::PlayerState& player_state) {
  PlayerPermissionCache& cache = permission_caches_[player_state.player_id];
  const u64 source_fingerprint = permission_source_fingerprint(player_state);
  const u64 region_revision = default_world_.regions().revision();
  bool region_layer_changed = false;

  if (default_world_.regions().empty()) {
    const bool had_region_permissions =
        !cache.active_regions.empty() ||
        cache.region_layer.core != player::CorePermissionSet{} ||
        !cache.region_layer.custom.empty();
    cache.active_regions.clear();
    cache.region_layer = {};
    cache.region_registry_revision = region_revision;
    cache.has_last_region_query = true;
    cache.last_region_world = player_state.world_id;
    cache.last_region_position = player_state.position;
    region_layer_changed = had_region_permissions;
  } else {
    const bool query_changed =
        !cache.has_last_region_query ||
        cache.last_region_world != player_state.world_id ||
        cache.last_region_position != player_state.position ||
        cache.region_registry_revision != region_revision;
    if (query_changed) {
      const world::RegionQuery query{player_state.world_id, player_state.position};
      (void)default_world_.update_player_region_state(player_state.player_id, query);
      std::vector<world::RegionID> active_regions;
      if (const world::PlayerRegionState* region_state =
              default_world_.player_region_state(player_state.player_id);
          region_state != nullptr) {
        active_regions = region_state->active_regions;
      }

      if (!cache.initialized ||
          cache.region_registry_revision != region_revision ||
          cache.active_regions != active_regions) {
        cache.region_layer =
            default_world_.regions().permission_layer_for_regions(active_regions);
        cache.active_regions = std::move(active_regions);
        region_layer_changed = true;
      }
      cache.region_registry_revision = region_revision;
      cache.has_last_region_query = true;
      cache.last_region_world = player_state.world_id;
      cache.last_region_position = player_state.position;
    }
  }

  if (cache.initialized &&
      cache.source_fingerprint == source_fingerprint &&
      cache.permission_registry_revision == permission_registry_revision_ &&
      cache.region_registry_revision == region_revision &&
      !region_layer_changed) {
    return;
  }

  player_state.resolved_permissions = player::resolve_permissions(
      game_modes_,
      permission_groups_,
      {
          player_state.game_mode_id,
          &player_state.permission_overrides,
          {},
          &cache.region_layer,
          {},
      });
  cache.source_fingerprint = source_fingerprint;
  cache.permission_registry_revision = permission_registry_revision_;
  cache.region_registry_revision = region_revision;
  cache.initialized = true;
}

void ServerRuntimeImpl::apply_player_action_command(
    player::PlayerState& player_state,
    const player::PlayerCommand& command) {
  refresh_player_permissions(player_state);

  player_state.selected_hotbar_slot =
      player::is_valid_hotbar_slot(command.selected_hotbar_slot)
          ? command.selected_hotbar_slot
          : player_state.selected_hotbar_slot;

  const physics::KinematicConfig physics_config =
      physics::kinematic_config_from_tuning(player_tuning_);
  const Seconds tick_seconds = tick_duration_seconds(base_tps_);

  if (command.primary_pressed) {
    restart_player_entity_animation(
        default_world_,
        player_state.player_id,
        player_hit_animation_name(selected_player_action_hand(player_state)));
  }

  (void)default_world_.set_player_visual_pose(
      player_state.player_id,
      player_state.yaw_radians,
      player_state.pitch_radians,
      player_tuning_.max_head_body_yaw_radians);

  refresh_player_permissions(player_state);

  world::WorldCollisionQuery interaction_query(default_world_, *block_registry_, false);
  const Vec3f eye_position = player_eye_position(player_state, player_tuning_);
  const Vec3f look_direction = player_look_direction(player_state);
  apply_player_interaction_command(
      player_state,
      command,
      interaction_query,
      eye_position,
      look_direction,
      physics_config,
      tick_seconds);
}

} // namespace arv::server::detail
