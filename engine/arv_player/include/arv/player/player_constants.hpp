#pragma once

#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"

namespace arv::player {

inline constexpr u8 DefaultHotbarSlotCount = 9;
inline constexpr u8 DefaultPlayerInventorySlotCount = 36;
inline constexpr f32 DefaultStandingEyeHeight = 1.62F;
inline constexpr f32 DefaultSneakingEyeHeight = 1.35F;
inline constexpr f32 DefaultSwimmingEyeHeight = 0.45F;
inline constexpr f32 DefaultReachDistance = 5.0F;
inline constexpr f32 DefaultMouseSensitivityRadians = 0.0025F;
inline constexpr f32 DefaultMaxPitchRadians = 1.55334306F;
inline constexpr f32 DefaultWalkSpeed = 4.5F;
inline constexpr f32 DefaultSprintSpeed = 6.0F;
inline constexpr f32 DefaultSneakSpeed = 2.25F;
inline constexpr f32 DefaultAirSpeed = 3.5F;
inline constexpr f32 DefaultAirControlAccelerationPerSecond = 10.0F;
inline constexpr f32 DefaultSurfaceSlipperiness = voxel::DefaultBlockSlipperiness;
inline constexpr f32 DefaultGroundAccelerationPerSecond = 160.0F;
inline constexpr f32 DefaultGroundDecelerationPerSecond = 180.0F;
inline constexpr f32 DefaultMinSurfaceControl = 0.08F;
inline constexpr f32 DefaultMaxSurfaceControl = 4.0F;
inline constexpr f32 DefaultFlySpeed = 8.0F;
inline constexpr f32 DefaultFlyVerticalSpeed = 6.0F;
inline constexpr f32 DefaultJumpVelocity = 8.5F;
inline constexpr f32 DefaultSprintJumpHorizontalBoost = 1.0F;
inline constexpr f32 DefaultGravityPerSecond = 32.0F;
inline constexpr f32 DefaultTerminalVelocity = 55.0F;
inline constexpr f32 DefaultPlayerBodyWidth = 0.6F;
inline constexpr f32 DefaultPlayerBodyHeight = 1.8F;
inline constexpr f32 DefaultPlayerSneakingBodyHeight = 1.5F;
inline constexpr f32 DefaultPlayerSwimmingBodyWidth = 0.9F;
inline constexpr f32 DefaultPlayerSwimmingBodyHeight = 0.6F;
inline constexpr f32 DefaultPlayerStepHeight = 0.6F;
inline constexpr f32 DefaultEyeHeightLerpPerSecond = 12.0F;
inline constexpr f32 DefaultSneakSupportProbeDepth = 0.05F;
inline constexpr f32 DefaultCollisionEpsilon = 0.001F;
inline constexpr f32 DefaultSurfaceSwimSpeed = 2.5F;
inline constexpr f32 DefaultUnderwaterSwimSpeed = 2.0F;
inline constexpr f32 DefaultFastSurfaceSwimSpeed = 5.6F;
inline constexpr f32 DefaultFastUnderwaterSwimSpeed = 5.0F;
inline constexpr f32 DefaultUpwardSwimAcceleration = 18.0F;
inline constexpr f32 DefaultDownwardSwimAcceleration = 18.0F;
inline constexpr f32 DefaultLiquidDrag = 0.8F;
inline constexpr f32 DefaultSurfaceFloatDepth = 1.0F;
inline constexpr f32 DefaultSurfaceBuoyancy = 10.0F;
inline constexpr f32 DefaultUnderwaterBuoyancy = 1.0F;
inline constexpr f32 DefaultBreathSeconds = 15.0F;
inline constexpr f32 DefaultMaxHeadBodyYawRadians = 1.309F;

struct PlayerTuning {
  f32 standing_eye_height{DefaultStandingEyeHeight};
  f32 sneaking_eye_height{DefaultSneakingEyeHeight};
  f32 swimming_eye_height{DefaultSwimmingEyeHeight};
  f32 eye_height_lerp_per_second{DefaultEyeHeightLerpPerSecond};
  f32 reach_distance{DefaultReachDistance};
  f32 mouse_sensitivity_radians{DefaultMouseSensitivityRadians};
  f32 walk_speed{DefaultWalkSpeed};
  f32 sprint_speed{DefaultSprintSpeed};
  f32 sneak_speed{DefaultSneakSpeed};
  f32 air_speed{DefaultAirSpeed};
  f32 air_control_acceleration_per_second{DefaultAirControlAccelerationPerSecond};
  f32 default_surface_slipperiness{DefaultSurfaceSlipperiness};
  f32 ground_acceleration_per_second{DefaultGroundAccelerationPerSecond};
  f32 ground_deceleration_per_second{DefaultGroundDecelerationPerSecond};
  f32 min_surface_control{DefaultMinSurfaceControl};
  f32 max_surface_control{DefaultMaxSurfaceControl};
  f32 fly_speed{DefaultFlySpeed};
  f32 fly_vertical_speed{DefaultFlyVerticalSpeed};
  f32 jump_velocity{DefaultJumpVelocity};
  f32 sprint_jump_horizontal_boost{DefaultSprintJumpHorizontalBoost};
  f32 gravity_per_second{DefaultGravityPerSecond};
  f32 terminal_velocity{DefaultTerminalVelocity};
  f32 body_width{DefaultPlayerBodyWidth};
  f32 body_height{DefaultPlayerBodyHeight};
  f32 sneaking_body_height{DefaultPlayerSneakingBodyHeight};
  f32 swimming_body_width{DefaultPlayerSwimmingBodyWidth};
  f32 swimming_body_height{DefaultPlayerSwimmingBodyHeight};
  f32 step_height{DefaultPlayerStepHeight};
  f32 sneak_support_probe_depth{DefaultSneakSupportProbeDepth};
  f32 collision_epsilon{DefaultCollisionEpsilon};
  f32 surface_swim_speed{DefaultSurfaceSwimSpeed};
  f32 underwater_swim_speed{DefaultUnderwaterSwimSpeed};
  f32 fast_surface_swim_speed{DefaultFastSurfaceSwimSpeed};
  f32 fast_underwater_swim_speed{DefaultFastUnderwaterSwimSpeed};
  f32 upward_swim_acceleration{DefaultUpwardSwimAcceleration};
  f32 downward_swim_acceleration{DefaultDownwardSwimAcceleration};
  f32 liquid_drag{DefaultLiquidDrag};
  f32 surface_float_depth{DefaultSurfaceFloatDepth};
  f32 surface_buoyancy{DefaultSurfaceBuoyancy};
  f32 underwater_buoyancy{DefaultUnderwaterBuoyancy};
  f32 breath_seconds{DefaultBreathSeconds};
  f32 max_head_body_yaw_radians{DefaultMaxHeadBodyYawRadians};
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
