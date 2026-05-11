#pragma once

#include "arv/physics/collision_resolution.hpp"

#include <algorithm>
#include <vector>

namespace arv::physics::detail {

struct CollisionScratch {
  std::vector<Aabb3f> boxes{};
  std::vector<Aabb3f> raycast_boxes{};
};

enum class Axis {
  X,
  Y,
  Z,
};

[[nodiscard]] inline bool overlaps_other_axes(
    const Aabb3f& a,
    const Aabb3f& b,
    Axis axis) noexcept {
  switch (axis) {
    case Axis::X:
      return intervals_overlap_open(a.min.y, a.max.y, b.min.y, b.max.y) &&
             intervals_overlap_open(a.min.z, a.max.z, b.min.z, b.max.z);
    case Axis::Y:
      return intervals_overlap_open(a.min.x, a.max.x, b.min.x, b.max.x) &&
             intervals_overlap_open(a.min.z, a.max.z, b.min.z, b.max.z);
    case Axis::Z:
      return intervals_overlap_open(a.min.x, a.max.x, b.min.x, b.max.x) &&
             intervals_overlap_open(a.min.y, a.max.y, b.min.y, b.max.y);
  }
  return false;
}

[[nodiscard]] inline f32 clamp_axis_delta(
    const Aabb3f& bounds,
    f32 delta,
    Axis axis,
    const std::vector<Aabb3f>& boxes,
    f32 epsilon) noexcept {
  f32 clamped = delta;
  for (const Aabb3f& box : boxes) {
    if (!overlaps_other_axes(bounds, box, axis)) {
      continue;
    }

    switch (axis) {
      case Axis::X:
        if (delta > 0.0F && bounds.max.x <= box.min.x) {
          clamped = std::min(clamped, box.min.x - bounds.max.x - epsilon);
        } else if (delta < 0.0F && bounds.min.x >= box.max.x) {
          clamped = std::max(clamped, box.max.x - bounds.min.x + epsilon);
        }
        break;
      case Axis::Y:
        if (delta > 0.0F && bounds.max.y <= box.min.y) {
          clamped = std::min(clamped, box.min.y - bounds.max.y - epsilon);
        } else if (delta < 0.0F && bounds.min.y >= box.max.y) {
          clamped = std::max(clamped, box.max.y - bounds.min.y + epsilon);
        }
        break;
      case Axis::Z:
        if (delta > 0.0F && bounds.max.z <= box.min.z) {
          clamped = std::min(clamped, box.min.z - bounds.max.z - epsilon);
        } else if (delta < 0.0F && bounds.min.z >= box.max.z) {
          clamped = std::max(clamped, box.max.z - bounds.min.z + epsilon);
        }
        break;
    }
  }
  return clamped;
}

inline void append_block_body_boxes(
    const ICollisionQuery& query,
    BlockPos pos,
    std::vector<Aabb3f>& out_boxes) {
  const Vec3f block_offset{
      static_cast<f32>(pos.value.x),
      static_cast<f32>(pos.value.y),
      static_cast<f32>(pos.value.z)};

  if (!query.is_block_loaded(pos)) {
    if (query.missing_blocks_are_solid()) {
      out_boxes.emplace_back(Aabb3f{block_offset, block_offset + Vec3f{1.0F, 1.0F, 1.0F}});
    }
    return;
  }

  const voxel::BlockBody* body = query.block_body(pos);
  if (body == nullptr) {
    return;
  }

  for (const Aabb3f& box : body->boxes) {
    out_boxes.emplace_back(Aabb3f{box.min + block_offset, box.max + block_offset});
  }
}

inline void collect_collision_boxes(
    const ICollisionQuery& query,
    const Aabb3f& world_bounds,
    std::vector<Aabb3f>& out_boxes) {
  const i32 min_x = floor_to_i32(world_bounds.min.x);
  const i32 min_y = floor_to_i32(world_bounds.min.y);
  const i32 min_z = floor_to_i32(world_bounds.min.z);
  const i32 max_x = static_cast<i32>(std::ceil(world_bounds.max.x));
  const i32 max_y = static_cast<i32>(std::ceil(world_bounds.max.y));
  const i32 max_z = static_cast<i32>(std::ceil(world_bounds.max.z));
  const i32 span_x = max_x > min_x ? max_x - min_x : 0;
  const i32 span_y = max_y > min_y ? max_y - min_y : 0;
  const i32 span_z = max_z > min_z ? max_z - min_z : 0;
  out_boxes.reserve(
      out_boxes.size() + static_cast<usize>(span_x) * static_cast<usize>(span_y) *
                             static_cast<usize>(span_z));

  for (i32 z = min_z; z < max_z; ++z) {
    for (i32 y = min_y; y < max_y; ++y) {
      for (i32 x = min_x; x < max_x; ++x) {
        append_block_body_boxes(query, BlockPos{{x, y, z}}, out_boxes);
      }
    }
  }
}

[[nodiscard]] MovementResolution resolve_movement_with_scratch(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f delta,
    f32 collision_epsilon,
    CollisionScratch& scratch);

[[nodiscard]] bool is_space_clear_with_scratch(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 collision_epsilon,
    CollisionScratch& scratch);

} // namespace arv::physics::detail
