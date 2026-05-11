#pragma once

#include "arv/physics/collision_query.hpp"

namespace arv::physics {

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

struct SupportSurfaceProbe {
  bool supported{};
  BlockPos block{};
  f32 support_y{};
  voxel::BlockPhysicsProperties physics{};
};

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
[[nodiscard]] SupportSurfaceProbe probe_support_surface(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 probe_depth = CollisionEpsilon * 2.0F);

} // namespace arv::physics
