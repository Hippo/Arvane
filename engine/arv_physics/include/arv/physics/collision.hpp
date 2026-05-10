#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"

#include <optional>

namespace arv::physics {

inline constexpr f32 CollisionEpsilon = 0.001F;

class ICollisionQuery {
public:
  virtual ~ICollisionQuery() = default;

  [[nodiscard]] virtual bool is_block_loaded(BlockPos pos) const noexcept = 0;
  [[nodiscard]] virtual voxel::BlockVariantID block_variant(BlockPos pos) const noexcept = 0;
  [[nodiscard]] virtual voxel::FluidCell fluid_cell(BlockPos pos) const noexcept {
    return voxel::empty_fluid_cell();
  }
  [[nodiscard]] virtual const voxel::BlockBody* block_body(BlockPos pos) const noexcept = 0;
  [[nodiscard]] virtual bool missing_blocks_are_solid() const noexcept {
    return true;
  }
};

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
  f32 fly_speed{8.0F};
  f32 fly_vertical_speed{6.0F};
  f32 jump_velocity{8.5F};
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

struct MovementResolution {
  Vec3f attempted_delta{};
  Vec3f applied_delta{};
  bool hit_x{};
  bool hit_y{};
  bool hit_z{};
  bool hit_down{};
  bool grounded{};
  Aabb3f resolved_bounds{};
};

struct KinematicStepResult {
  KinematicBody body{};
  MovementResolution movement{};
  LiquidContactState liquid_state{LiquidContactState::NotInLiquid};
  PlayerCollisionPose pose{PlayerCollisionPose::Standing};
};

struct BlockRaycastHit {
  BlockPos block{};
  voxel::BlockVariantID variant{};
  voxel::BlockFace face{voxel::BlockFace::PositiveY};
  Vec3f hit_point{};
  f32 distance{};
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
[[nodiscard]] MovementResolution resolve_movement(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f delta,
    f32 collision_epsilon = CollisionEpsilon);
[[nodiscard]] bool overlaps_solid_blocks(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 collision_epsilon = CollisionEpsilon);
[[nodiscard]] bool has_support_below(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 probe_depth = CollisionEpsilon * 2.0F);
[[nodiscard]] std::optional<BlockRaycastHit> raycast_blocks(
    const ICollisionQuery& query,
    Vec3f origin,
    Vec3f direction,
    f32 max_distance);

} // namespace arv::physics
