#pragma once

#include "arv/core/time.hpp"
#include "arv/physics/collision_resolution.hpp"

namespace arv::physics {

enum class LiquidContactState : u8 {
  NotInLiquid,
  PartiallyInLiquid,
  SwimmingAtSurface,
  SwimmingUnderwater,
};

enum class PlayerCollisionPose : u8 {
  Standing,
  Sneaking,
  Swimming,
};

struct KinematicBody {
  Vec3f position{};
  Vec3f velocity{};
  bool grounded{};
  bool sneaking{};
  bool swimming{};
  f32 breath_seconds{15.0F};
};

struct KinematicConfig {
  f32 standing_eye_height{1.62F};
  f32 sneaking_eye_height{1.35F};
  f32 swimming_eye_height{0.45F};
  f32 walk_speed{4.5F};
  f32 sprint_speed{6.0F};
  f32 sneak_speed{2.25F};
  f32 air_speed{3.5F};
  f32 air_control_acceleration_per_second{10.0F};
  f32 default_surface_slipperiness{voxel::DefaultBlockSlipperiness};
  f32 ground_acceleration_per_second{160.0F};
  f32 ground_deceleration_per_second{180.0F};
  f32 min_surface_control{0.08F};
  f32 max_surface_control{4.0F};
  f32 fly_speed{8.0F};
  f32 fly_vertical_speed{6.0F};
  f32 jump_velocity{8.5F};
  f32 sprint_jump_horizontal_boost{1.0F};
  f32 gravity_per_second{32.0F};
  f32 terminal_velocity{55.0F};
  f32 body_width{0.6F};
  f32 body_height{1.8F};
  f32 sneaking_body_height{1.5F};
  f32 swimming_body_width{0.9F};
  f32 swimming_body_height{0.6F};
  f32 step_height{0.6F};
  f32 sneak_support_probe_depth{0.05F};
  f32 collision_epsilon{CollisionEpsilon};
  f32 surface_swim_speed{2.5F};
  f32 underwater_swim_speed{2.0F};
  f32 fast_surface_swim_speed{5.6F};
  f32 fast_underwater_swim_speed{5.0F};
  f32 upward_swim_acceleration{18.0F};
  f32 downward_swim_acceleration{18.0F};
  f32 liquid_drag{0.8F};
  f32 surface_float_depth{1.0F};
  f32 surface_buoyancy{10.0F};
  f32 underwater_buoyancy{1.0F};
  f32 breath_seconds{15.0F};
};

struct KinematicInput {
  i16 move_x{};
  i16 move_z{};
  bool jump_pressed{};
  bool jump_held{};
  bool sneak_held{};
  bool sprint_held{};
  bool fly_mode{};
  f32 pitch_radians{};
};

struct KinematicStepResult {
  KinematicBody body{};
  MovementResolution movement{};
  LiquidContactState liquid_state{LiquidContactState::NotInLiquid};
  PlayerCollisionPose pose{PlayerCollisionPose::Standing};
};

[[nodiscard]] Aabb3f make_body_bounds(Vec3f position, const KinematicConfig& config) noexcept;
[[nodiscard]] Aabb3f make_body_bounds(
    Vec3f position,
    const KinematicConfig& config,
    PlayerCollisionPose pose) noexcept;
[[nodiscard]] Aabb3f make_body_bounds(
    Vec3f position,
    const KinematicConfig& config,
    bool sneaking) noexcept;
[[nodiscard]] KinematicStepResult step_kinematic_body(
    const ICollisionQuery& query,
    const KinematicBody& body,
    f32 yaw_radians,
    Seconds tick_seconds,
    const KinematicInput& input,
    const KinematicConfig& config);

} // namespace arv::physics
