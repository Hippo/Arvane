#pragma once

#include "arv/core/math.hpp"
#include "arv/physics/player_movement.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/player_movement.hpp"

#include <span>

namespace arv::web {

struct WebPlayerMovementState {
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

struct WebPlayerMovementInput {
  bool forward{};
  bool back{};
  bool left{};
  bool right{};
  bool jump{};
  bool sneak{};
  bool sprint{};
  bool toggle_fly_pressed{};
  f32 mouse_dx{};
  f32 mouse_dy{};
};

struct WebPlayerMovementStep {
  WebPlayerMovementState state{};
  protocol::PlayerMoveFlags flags{protocol::PlayerMoveFlags::None};
};

[[nodiscard]] WebPlayerMovementStep simulate_web_player_movement(
    WebPlayerMovementState state,
    WebPlayerMovementInput input,
    std::span<const protocol::ClientboundChunkSectionData> chunk_sections,
    Seconds tick_seconds,
    const player::PlayerTuning& tuning = player::default_player_tuning(),
    physics::PlayerMovementAuthority authority = {false, true});

void apply_web_player_move_correction(
    WebPlayerMovementState& state,
    const protocol::ClientboundPlayerMoveCorrection& correction) noexcept;

void apply_web_player_velocity(
    WebPlayerMovementState& state,
    const protocol::ClientboundPlayerVelocity& velocity,
    PlayerID local_player_id) noexcept;

void apply_web_player_permissions(
    WebPlayerMovementState& state,
    physics::PlayerMovementAuthority& authority,
    const protocol::ClientboundPlayerPermissions& permissions) noexcept;

void apply_web_player_movement_tuning(
    player::PlayerTuning& tuning,
    const protocol::ClientboundPlayerMovementTuning& movement_tuning) noexcept;

void apply_web_player_look(
    WebPlayerMovementState& state,
    f32 mouse_dx,
    f32 mouse_dy,
    const player::PlayerTuning& tuning = player::default_player_tuning()) noexcept;

[[nodiscard]] protocol::PlayerMoveFlags web_player_move_flags(
    const WebPlayerMovementState& state,
    const WebPlayerMovementInput& input) noexcept;

[[nodiscard]] f32 web_player_eye_height(
    const WebPlayerMovementState& state,
    const player::PlayerTuning& tuning = player::default_player_tuning()) noexcept;

} // namespace arv::web
