#pragma once

#include "arv/core/time.hpp"
#include "arv/physics/collision.hpp"
#include "arv/player/player_state.hpp"

namespace arv::physics {

struct PlayerMovementAuthority {
  bool can_fly{};
  bool has_collision{true};
};

struct PlayerKinematicState {
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  bool grounded{};
  bool sneaking{};
  bool swimming{};
  f32 breath_seconds{player::DefaultBreathSeconds};
  player::MovementMode movement_mode{player::MovementMode::Walking};
};

struct PlayerMovementStep {
  PlayerKinematicState state{};
  MovementResolution movement{};
  LiquidContactState liquid_state{LiquidContactState::NotInLiquid};
  PlayerCollisionPose pose{PlayerCollisionPose::Standing};
  bool used_collision{};
};

[[nodiscard]] KinematicConfig kinematic_config_from_tuning(
    const player::PlayerTuning& tuning) noexcept;

[[nodiscard]] PlayerKinematicState kinematic_state_from_player(
    const player::PlayerState& player_state) noexcept;

void apply_kinematic_state_to_player(
    player::PlayerState& player_state,
    const PlayerKinematicState& state) noexcept;

void apply_player_look_and_mode(
    PlayerKinematicState& state,
    const player::PlayerCommand& command,
    const player::PlayerTuning& tuning,
    PlayerMovementAuthority authority) noexcept;

[[nodiscard]] KinematicInput kinematic_input_from_command(
    const player::PlayerCommand& command,
    const PlayerKinematicState& state,
    PlayerMovementAuthority authority) noexcept;

[[nodiscard]] PlayerMovementStep simulate_player_movement_tick(
    const ICollisionQuery* collision_query,
    PlayerKinematicState state,
    const player::PlayerCommand& command,
    const player::PlayerTuning& tuning,
    PlayerMovementAuthority authority,
    Seconds tick_seconds,
    f32 gravity_scale = 1.0F);

} // namespace arv::physics
