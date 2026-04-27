#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"

#include <optional>
#include <vector>

namespace arv::physics {

inline constexpr f32 CollisionEpsilon = 0.001F;

class ICollisionQuery {
public:
  virtual ~ICollisionQuery() = default;

  [[nodiscard]] virtual bool is_block_loaded(BlockPos pos) const noexcept = 0;
  [[nodiscard]] virtual voxel::BlockVariantID block_variant(BlockPos pos) const noexcept = 0;
  virtual void gather_collision_boxes(const Aabb3f& world_bounds, std::vector<Aabb3f>& out_boxes) const = 0;
  virtual void gather_block_collision_boxes(BlockPos pos, std::vector<Aabb3f>& out_boxes) const = 0;
};

struct KinematicBody {
  Vec3f position{};
  Vec3f velocity{};
  bool grounded{};
  bool sneaking{};
};

struct KinematicConfig {
  f32 walk_speed{4.5F};
  f32 sprint_speed{6.0F};
  f32 sneak_speed{2.25F};
  f32 air_speed{3.5F};
  f32 fly_speed{8.0F};
  f32 fly_vertical_speed{6.0F};
  f32 jump_velocity{8.5F};
  f32 gravity_per_second{32.0F};
  f32 terminal_velocity{55.0F};
  f32 body_width{0.6F};
  f32 body_height{1.8F};
  f32 sneaking_body_height{1.5F};
  f32 sneak_support_probe_depth{0.05F};
  f32 collision_epsilon{CollisionEpsilon};
};

struct KinematicInput {
  i16 move_x{};
  i16 move_z{};
  bool jump_pressed{};
  bool jump_held{};
  bool sneak_held{};
  bool sprint_held{};
  bool fly_mode{};
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
