#include "server_runtime_internal.hpp"

#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace arv::server::detail {
namespace {

void restart_player_entity_animation(
    world::WorldRuntime& world,
    PlayerID player_id,
    std::string_view animation_name) {
  const std::optional<EntityID> entity_id = world.entity_for_player(player_id);
  if (!entity_id.has_value()) {
    return;
  }
  entity::EntityAnimationState* animation =
      world.find_entity_animation_state(entity_id.value());
  if (animation == nullptr) {
    (void)world.set_entity_animation(
        entity_id.value(),
        std::string{animation_name});
    return;
  }
  animation->name = std::string{animation_name};
  animation->time_seconds = 0.0F;
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

void step_player_without_collision(
    player::PlayerState& player_state,
    const player::PlayerCommand& command,
    const player::PlayerTuning& tuning,
    Seconds tick_seconds) noexcept {
  Vec3f move{};
  if (command.move_z != 0) {
    move = move + horizontal_forward(player_state.yaw_radians) * static_cast<f32>(command.move_z);
  }
  if (command.move_x != 0) {
    move = move + horizontal_right(player_state.yaw_radians) * static_cast<f32>(command.move_x);
  }
  move = normalize_or_zero(move);

  player_state.velocity.x = move.x * tuning.fly_speed;
  player_state.velocity.z = move.z * tuning.fly_speed;

  f32 vertical = 0.0F;
  if (command.jump_held) {
    vertical += 1.0F;
  }
  if (command.sneak_held) {
    vertical -= 1.0F;
  }
  player_state.velocity.y = vertical * tuning.fly_vertical_speed;
  player_state.position = player_state.position + player_state.velocity * tick_seconds;
  player_state.grounded = false;
  player_state.sneaking = false;
  player_state.swimming = false;
  player_state.breath_seconds = tuning.breath_seconds;
}

bool ServerRuntimeImpl::set_player_game_mode(PlayerID player_id, std::string_view mode_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || game_modes_.find(mode_id) == nullptr) {
    return false;
  }

  player_state->game_mode_id = std::string{mode_id};
  refresh_player_permissions(*player_state);
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

    player::PlayerCommand command;
    if (auto found = pending_player_commands_.find(session->player_id);
        found != pending_player_commands_.end()) {
      command = found->second;
      pending_player_commands_.erase(found);
    } else {
      command.client_tick = player_state->last_processed_input_tick + 1U;
      command.selected_hotbar_slot = player_state->selected_hotbar_slot;
    }

    apply_player_command(*player_state, command);
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
      cache.permission_config_revision == permission_config_revision_ &&
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
  cache.permission_config_revision = permission_config_revision_;
  cache.region_registry_revision = region_revision;
  cache.initialized = true;
}

void ServerRuntimeImpl::apply_player_command(
    player::PlayerState& player_state,
    const player::PlayerCommand& command) {
  refresh_player_permissions(player_state);

  player_state.selected_hotbar_slot =
      player::is_valid_hotbar_slot(command.selected_hotbar_slot)
          ? command.selected_hotbar_slot
          : player_state.selected_hotbar_slot;

  if (command.primary_pressed) {
    restart_player_entity_animation(default_world_, player_state.player_id, "hit_right");
  }

  player_state.yaw_radians +=
      static_cast<f32>(command.look_delta_x) * player_tuning_.mouse_sensitivity_radians;
  player_state.pitch_radians = std::clamp(
      player_state.pitch_radians -
          static_cast<f32>(command.look_delta_y) * player_tuning_.mouse_sensitivity_radians,
      -player::DefaultMaxPitchRadians,
      player::DefaultMaxPitchRadians);

  if (!player_state.resolved_permissions.core.can_fly &&
      player_state.movement_mode == player::MovementMode::Flying) {
    player_state.movement_mode = player::MovementMode::Walking;
  }

  if (command.toggle_fly_pressed && player_state.resolved_permissions.core.can_fly) {
    player_state.movement_mode =
        player_state.movement_mode == player::MovementMode::Flying
            ? player::MovementMode::Walking
            : player::MovementMode::Flying;
  }

  world::WorldCollisionQuery collision_query(default_world_, *block_registry_, true);
  physics::KinematicConfig physics_config;
  physics_config.standing_eye_height = player_tuning_.standing_eye_height;
  physics_config.sneaking_eye_height = player_tuning_.sneaking_eye_height;
  physics_config.swimming_eye_height = player_tuning_.swimming_eye_height;
  physics_config.walk_speed = player_tuning_.walk_speed;
  physics_config.sprint_speed = player_tuning_.sprint_speed;
  physics_config.sneak_speed = player_tuning_.sneak_speed;
  physics_config.air_speed = player_tuning_.air_speed;
  physics_config.fly_speed = player_tuning_.fly_speed;
  physics_config.fly_vertical_speed = player_tuning_.fly_vertical_speed;
  physics_config.jump_velocity = player_tuning_.jump_velocity;
  physics_config.gravity_per_second = player_tuning_.gravity_per_second;
  physics_config.terminal_velocity = player_tuning_.terminal_velocity;
  physics_config.body_width = player_tuning_.body_width;
  physics_config.body_height = player_tuning_.body_height;
  physics_config.sneaking_body_height = player_tuning_.sneaking_body_height;
  physics_config.swimming_body_width = player_tuning_.swimming_body_width;
  physics_config.swimming_body_height = player_tuning_.swimming_body_height;
  physics_config.sneak_support_probe_depth = player_tuning_.sneak_support_probe_depth;
  physics_config.collision_epsilon = player_tuning_.collision_epsilon;
  physics_config.surface_swim_speed = player_tuning_.surface_swim_speed;
  physics_config.underwater_swim_speed = player_tuning_.underwater_swim_speed;
  physics_config.fast_surface_swim_speed = player_tuning_.fast_surface_swim_speed;
  physics_config.fast_underwater_swim_speed = player_tuning_.fast_underwater_swim_speed;
  physics_config.upward_swim_acceleration = player_tuning_.upward_swim_acceleration;
  physics_config.downward_swim_acceleration = player_tuning_.downward_swim_acceleration;
  physics_config.liquid_drag = player_tuning_.liquid_drag;
  physics_config.surface_float_depth = player_tuning_.surface_float_depth;
  physics_config.surface_buoyancy = player_tuning_.surface_buoyancy;
  physics_config.underwater_buoyancy = player_tuning_.underwater_buoyancy;
  physics_config.breath_seconds = player_tuning_.breath_seconds;

  physics::KinematicInput input;
  input.move_x = command.move_x;
  input.move_z = command.move_z;
  input.jump_pressed = command.jump_pressed;
  input.jump_held = command.jump_held;
  input.sneak_held = command.sneak_held;
  input.sprint_held = command.sprint_held;
  input.fly_mode =
      player_state.resolved_permissions.core.can_fly &&
      player_state.movement_mode == player::MovementMode::Flying;
  input.pitch_radians = player_state.pitch_radians;

  const Seconds tick_seconds = tick_duration_seconds(base_tps_);
  if (player_state.resolved_permissions.core.has_collision) {
    const physics::KinematicStepResult step = physics::step_kinematic_body(
        collision_query,
        {player_state.position,
         player_state.velocity,
         player_state.grounded,
         player_state.sneaking,
         player_state.swimming,
         player_state.breath_seconds},
        player_state.yaw_radians,
        tick_seconds,
        input,
        physics_config);
    player_state.position = step.body.position;
    player_state.velocity = step.body.velocity;
    player_state.grounded = step.body.grounded;
    player_state.sneaking = step.body.sneaking;
    player_state.swimming = step.body.swimming;
    player_state.breath_seconds = step.body.breath_seconds;
  } else {
    step_player_without_collision(player_state, command, player_tuning_, tick_seconds);
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
