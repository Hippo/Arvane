#include "arv/physics/raycast.hpp"

#include "collision_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arv::physics {
namespace {

[[nodiscard]] std::optional<RayAabbHit> ray_intersects_aabb_normalized(
    Vec3f origin,
    Vec3f direction,
    Aabb3f bounds,
    f32 max_distance) noexcept {
  f32 t_min = 0.0F;
  f32 t_max = max_distance;
  voxel::BlockFace entry_face = voxel::BlockFace::PositiveY;

  auto update_axis = [&](f32 origin_axis,
                         f32 direction_axis,
                         f32 min_axis,
                         f32 max_axis,
                         voxel::BlockFace min_face,
                         voxel::BlockFace max_face) -> bool {
    constexpr f32 ParallelThreshold = 1.0e-6F;
    if (std::abs(direction_axis) <= ParallelThreshold) {
      return origin_axis >= min_axis && origin_axis <= max_axis;
    }

    const f32 inverse = 1.0F / direction_axis;
    f32 t1 = (min_axis - origin_axis) * inverse;
    f32 t2 = (max_axis - origin_axis) * inverse;
    voxel::BlockFace face = min_face;
    if (t1 > t2) {
      std::swap(t1, t2);
      face = max_face;
    }

    if (t1 > t_min) {
      t_min = t1;
      entry_face = face;
    }
    t_max = std::min(t_max, t2);
    return t_min <= t_max;
  };

  if (!update_axis(
          origin.x,
          direction.x,
          bounds.min.x,
          bounds.max.x,
          voxel::BlockFace::NegativeX,
          voxel::BlockFace::PositiveX) ||
      !update_axis(
          origin.y,
          direction.y,
          bounds.min.y,
          bounds.max.y,
          voxel::BlockFace::NegativeY,
          voxel::BlockFace::PositiveY) ||
      !update_axis(
          origin.z,
          direction.z,
          bounds.min.z,
          bounds.max.z,
          voxel::BlockFace::NegativeZ,
          voxel::BlockFace::PositiveZ)) {
    return std::nullopt;
  }

  if (t_min < 0.0F || t_min > max_distance) {
    return std::nullopt;
  }

  return RayAabbHit{entry_face, t_min, origin + direction * t_min};
}

} // namespace

std::optional<RayAabbHit> ray_intersects_aabb(
    Vec3f origin,
    Vec3f direction,
    Aabb3f bounds,
    f32 max_distance) noexcept {
  direction = normalize_or_zero(direction);
  if (direction == Vec3f{}) {
    return std::nullopt;
  }
  return ray_intersects_aabb_normalized(origin, direction, bounds, max_distance);
}

std::optional<BlockRaycastHit> raycast_blocks(
    const ICollisionQuery& query,
    Vec3f origin,
    Vec3f direction,
    f32 max_distance) {
  direction = normalize_or_zero(direction);
  if (direction == Vec3f{}) {
    return std::nullopt;
  }

  i32 x = floor_to_i32(origin.x);
  i32 y = floor_to_i32(origin.y);
  i32 z = floor_to_i32(origin.z);

  const i32 step_x = direction.x > 0.0F ? 1 : (direction.x < 0.0F ? -1 : 0);
  const i32 step_y = direction.y > 0.0F ? 1 : (direction.y < 0.0F ? -1 : 0);
  const i32 step_z = direction.z > 0.0F ? 1 : (direction.z < 0.0F ? -1 : 0);

  const f32 inf = std::numeric_limits<f32>::infinity();
  auto initial_t_max = [&](f32 origin_axis, f32 direction_axis, i32 cell, i32 step) -> f32 {
    if (step == 0) {
      return inf;
    }
    const f32 boundary = step > 0 ? static_cast<f32>(cell + 1) : static_cast<f32>(cell);
    return (boundary - origin_axis) / direction_axis;
  };

  f32 t_max_x = initial_t_max(origin.x, direction.x, x, step_x);
  f32 t_max_y = initial_t_max(origin.y, direction.y, y, step_y);
  f32 t_max_z = initial_t_max(origin.z, direction.z, z, step_z);
  const f32 t_delta_x = step_x == 0 ? inf : std::abs(1.0F / direction.x);
  const f32 t_delta_y = step_y == 0 ? inf : std::abs(1.0F / direction.y);
  const f32 t_delta_z = step_z == 0 ? inf : std::abs(1.0F / direction.z);

  detail::CollisionScratch scratch;
  scratch.raycast_boxes.reserve(8);

  constexpr usize MaxIterations = 2048;
  for (usize iteration = 0; iteration < MaxIterations; ++iteration) {
    const BlockPos block{{x, y, z}};
    scratch.raycast_boxes.clear();
    detail::append_block_body_boxes(query, block, scratch.raycast_boxes);
    if (!scratch.raycast_boxes.empty()) {
      const voxel::BlockVariantID variant = query.block_variant(block);
      std::optional<RayAabbHit> closest_hit;
      for (const Aabb3f& box : scratch.raycast_boxes) {
        auto hit = ray_intersects_aabb_normalized(origin, direction, box, max_distance);
        if (!hit.has_value()) {
          continue;
        }
        if (!closest_hit.has_value() || hit->distance < closest_hit->distance) {
          closest_hit = hit;
        }
      }
      if (closest_hit.has_value()) {
        return BlockRaycastHit{
            block,
            variant,
            closest_hit->face,
            closest_hit->hit_point,
            closest_hit->distance};
      }
    }

    const f32 next_t = std::min(t_max_x, std::min(t_max_y, t_max_z));
    if (next_t > max_distance) {
      break;
    }

    if (t_max_x <= t_max_y && t_max_x <= t_max_z) {
      x += step_x;
      t_max_x += t_delta_x;
    } else if (t_max_y <= t_max_z) {
      y += step_y;
      t_max_y += t_delta_y;
    } else {
      z += step_z;
      t_max_z += t_delta_z;
    }
  }

  return std::nullopt;
}

} // namespace arv::physics
