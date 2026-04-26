#pragma once

#include "arv/core/types.hpp"

namespace arv::player {

inline constexpr u8 DefaultHotbarSlotCount = 9;
inline constexpr u8 DefaultPlayerInventorySlotCount = 36;
inline constexpr f32 DefaultStandingEyeHeight = 1.62F;
inline constexpr f32 DefaultSneakingEyeHeight = 1.50F;
inline constexpr f32 DefaultReachDistance = 5.0F;
inline constexpr f32 DefaultMouseSensitivityRadians = 0.0025F;
inline constexpr f32 DefaultMaxPitchRadians = 1.55334306F;
inline constexpr f32 DefaultWalkSpeed = 4.5F;
inline constexpr f32 DefaultSprintSpeed = 6.0F;
inline constexpr f32 DefaultAirSpeed = 3.5F;
inline constexpr f32 DefaultFlySpeed = 8.0F;
inline constexpr f32 DefaultFlyVerticalSpeed = 6.0F;
inline constexpr f32 DefaultJumpVelocity = 8.5F;
inline constexpr f32 DefaultGravityPerSecond = 32.0F;
inline constexpr f32 DefaultTerminalVelocity = 55.0F;
inline constexpr f32 DefaultPlayerBodyWidth = 0.6F;
inline constexpr f32 DefaultPlayerBodyHeight = 1.8F;

struct PlayerTuning {
  f32 standing_eye_height{DefaultStandingEyeHeight};
  f32 sneaking_eye_height{DefaultSneakingEyeHeight};
  f32 reach_distance{DefaultReachDistance};
  f32 mouse_sensitivity_radians{DefaultMouseSensitivityRadians};
  f32 walk_speed{DefaultWalkSpeed};
  f32 sprint_speed{DefaultSprintSpeed};
  f32 air_speed{DefaultAirSpeed};
  f32 fly_speed{DefaultFlySpeed};
  f32 fly_vertical_speed{DefaultFlyVerticalSpeed};
  f32 jump_velocity{DefaultJumpVelocity};
  f32 gravity_per_second{DefaultGravityPerSecond};
  f32 terminal_velocity{DefaultTerminalVelocity};
  f32 body_width{DefaultPlayerBodyWidth};
  f32 body_height{DefaultPlayerBodyHeight};
  u8 hotbar_slot_count{DefaultHotbarSlotCount};
  u8 inventory_slot_count{DefaultPlayerInventorySlotCount};
};

[[nodiscard]] constexpr PlayerTuning default_player_tuning() noexcept {
  return {};
}

[[nodiscard]] constexpr bool is_valid_hotbar_slot(u8 slot) noexcept {
  return slot < DefaultHotbarSlotCount;
}

} // namespace arv::player
