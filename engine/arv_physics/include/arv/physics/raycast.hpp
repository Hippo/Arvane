#pragma once

#include "arv/physics/collision_query.hpp"

#include <optional>

namespace arv::physics {

struct BlockRaycastHit {
  BlockPos block{};
  voxel::BlockVariantID variant{};
  voxel::BlockFace face{voxel::BlockFace::PositiveY};
  Vec3f hit_point{};
  f32 distance{};
};

struct RayAabbHit {
  voxel::BlockFace face{voxel::BlockFace::PositiveY};
  f32 distance{};
  Vec3f hit_point{};
};

[[nodiscard]] std::optional<RayAabbHit> ray_intersects_aabb(
    Vec3f origin,
    Vec3f direction,
    Aabb3f bounds,
    f32 max_distance) noexcept;
[[nodiscard]] std::optional<BlockRaycastHit> raycast_blocks(
    const ICollisionQuery& query,
    Vec3f origin,
    Vec3f direction,
    f32 max_distance);

} // namespace arv::physics
