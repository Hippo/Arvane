#include "arv/physics/player_movement.hpp"

#include <algorithm>

namespace arv::physics {
namespace {

void step_player_without_collision(
    PlayerKinematicState& state,
    const player::PlayerCommand& command,
    const player::PlayerTuning& tuning,
    Seconds tick_seconds) noexcept {
  const MovementBasis basis = horizontal_movement_basis(state.yaw_radians);
  Vec3f move{};
  if (command.move_z != 0) {
    move = move + basis.forward * static_cast<f32>(command.move_z);
  }
  if (command.move_x != 0) {
    move = move + basis.right * static_cast<f32>(command.move_x);
  }
  move = normalize_or_zero(move);

  state.velocity.x = move.x * tuning.fly_speed;
  state.velocity.z = move.z * tuning.fly_speed;

  f32 vertical = 0.0F;
  if (command.jump_held) {
    vertical += 1.0F;
  }
  if (command.sneak_held) {
    vertical -= 1.0F;
  }
  state.velocity.y = vertical * tuning.fly_vertical_speed;
  state.position = state.position + state.velocity * tick_seconds;
  state.grounded = false;
  state.sneaking = false;
  state.swimming = false;
  state.breath_seconds = tuning.breath_seconds;
}

} // namespace

KinematicConfig kinematic_config_from_tuning(
    const player::PlayerTuning& tuning) noexcept {
  KinematicConfig config;
  config.standing_eye_height = tuning.standing_eye_height;
  config.sneaking_eye_height = tuning.sneaking_eye_height;
  config.swimming_eye_height = tuning.swimming_eye_height;
  config.walk_speed = tuning.walk_speed;
  config.sprint_speed = tuning.sprint_speed;
  config.sneak_speed = tuning.sneak_speed;
  config.air_speed = tuning.air_speed;
  config.air_control_acceleration_per_second =
      tuning.air_control_acceleration_per_second;
  config.default_surface_slipperiness = tuning.default_surface_slipperiness;
  config.ground_acceleration_per_second = tuning.ground_acceleration_per_second;
  config.ground_deceleration_per_second = tuning.ground_deceleration_per_second;
  config.min_surface_control = tuning.min_surface_control;
  config.max_surface_control = tuning.max_surface_control;
  config.fly_speed = tuning.fly_speed;
  config.fly_vertical_speed = tuning.fly_vertical_speed;
  config.jump_velocity = tuning.jump_velocity;
  config.sprint_jump_horizontal_boost = tuning.sprint_jump_horizontal_boost;
  config.gravity_per_second = tuning.gravity_per_second;
  config.terminal_velocity = tuning.terminal_velocity;
  config.body_width = tuning.body_width;
  config.body_height = tuning.body_height;
  config.sneaking_body_height = tuning.sneaking_body_height;
  config.swimming_body_width = tuning.swimming_body_width;
  config.swimming_body_height = tuning.swimming_body_height;
  config.step_height = tuning.step_height;
  config.sneak_support_probe_depth = tuning.sneak_support_probe_depth;
  config.collision_epsilon = tuning.collision_epsilon;
  config.surface_swim_speed = tuning.surface_swim_speed;
  config.underwater_swim_speed = tuning.underwater_swim_speed;
  config.fast_surface_swim_speed = tuning.fast_surface_swim_speed;
  config.fast_underwater_swim_speed = tuning.fast_underwater_swim_speed;
  config.upward_swim_acceleration = tuning.upward_swim_acceleration;
  config.downward_swim_acceleration = tuning.downward_swim_acceleration;
  config.liquid_drag = tuning.liquid_drag;
  config.surface_float_depth = tuning.surface_float_depth;
  config.surface_buoyancy = tuning.surface_buoyancy;
  config.underwater_buoyancy = tuning.underwater_buoyancy;
  config.breath_seconds = tuning.breath_seconds;
  return config;
}

PlayerKinematicState kinematic_state_from_player(
    const player::PlayerState& player_state) noexcept {
  return {
      player_state.position,
      player_state.velocity,
      player_state.yaw_radians,
      player_state.pitch_radians,
      player_state.grounded,
      player_state.sneaking,
      player_state.swimming,
      player_state.breath_seconds,
      player_state.movement_mode};
}

void apply_kinematic_state_to_player(
    player::PlayerState& player_state,
    const PlayerKinematicState& state) noexcept {
  player_state.position = state.position;
  player_state.velocity = state.velocity;
  player_state.yaw_radians = state.yaw_radians;
  player_state.pitch_radians = state.pitch_radians;
  player_state.grounded = state.grounded;
  player_state.sneaking = state.sneaking;
  player_state.swimming = state.swimming;
  player_state.breath_seconds = state.breath_seconds;
  player_state.movement_mode = state.movement_mode;
}

void apply_player_look_and_mode(
    PlayerKinematicState& state,
    const player::PlayerCommand& command,
    const player::PlayerTuning& tuning,
    PlayerMovementAuthority authority) noexcept {
  state.yaw_radians +=
      static_cast<f32>(command.look_delta_x) * tuning.mouse_sensitivity_radians;
  state.pitch_radians = std::clamp(
      state.pitch_radians -
          static_cast<f32>(command.look_delta_y) * tuning.mouse_sensitivity_radians,
      -player::DefaultMaxPitchRadians,
      player::DefaultMaxPitchRadians);

  if (!authority.can_fly && state.movement_mode == player::MovementMode::Flying) {
    state.movement_mode = player::MovementMode::Walking;
  }

  if (command.toggle_fly_pressed && authority.can_fly) {
    state.movement_mode =
        state.movement_mode == player::MovementMode::Flying
            ? player::MovementMode::Walking
            : player::MovementMode::Flying;
  }
}

KinematicInput kinematic_input_from_command(
    const player::PlayerCommand& command,
    const PlayerKinematicState& state,
    PlayerMovementAuthority authority) noexcept {
  KinematicInput input;
  input.move_x = command.move_x;
  input.move_z = command.move_z;
  input.jump_pressed = command.jump_pressed;
  input.jump_held = command.jump_held;
  input.sneak_held = command.sneak_held;
  input.sprint_held = command.sprint_held;
  input.fly_mode =
      authority.can_fly && state.movement_mode == player::MovementMode::Flying;
  input.pitch_radians = state.pitch_radians;
  return input;
}

PlayerMovementStep simulate_player_movement_tick(
    const ICollisionQuery* collision_query,
    PlayerKinematicState state,
    const player::PlayerCommand& command,
    const player::PlayerTuning& tuning,
    PlayerMovementAuthority authority,
    Seconds tick_seconds) {
  apply_player_look_and_mode(state, command, tuning, authority);

  PlayerMovementStep result;
  result.used_collision = authority.has_collision && collision_query != nullptr;
  if (result.used_collision) {
    const KinematicStepResult step = step_kinematic_body(
        *collision_query,
        {state.position,
         state.velocity,
         state.grounded,
         state.sneaking,
         state.swimming,
         state.breath_seconds},
        state.yaw_radians,
        tick_seconds,
        kinematic_input_from_command(command, state, authority),
        kinematic_config_from_tuning(tuning));
    state.position = step.body.position;
    state.velocity = step.body.velocity;
    state.grounded = step.body.grounded;
    state.sneaking = step.body.sneaking;
    state.swimming = step.body.swimming;
    state.breath_seconds = step.body.breath_seconds;
    result.movement = step.movement;
    result.liquid_state = step.liquid_state;
    result.pose = step.pose;
  } else if (!authority.has_collision) {
    step_player_without_collision(state, command, tuning, tick_seconds);
  }

  result.state = state;
  return result;
}

} // namespace arv::physics
