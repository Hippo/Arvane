#include "arv/physics/collision_resolution.hpp"

#include "collision_internal.hpp"

#include <cmath>
#include <vector>

namespace arv::physics {
namespace detail {

bool is_space_clear_with_scratch(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 collision_epsilon,
    CollisionScratch& scratch) {
  scratch.boxes.clear();
  scratch.boxes.reserve(32);
  detail::collect_collision_boxes(
      query,
      {{bounds.min.x - collision_epsilon, bounds.min.y - collision_epsilon, bounds.min.z - collision_epsilon},
       {bounds.max.x + collision_epsilon, bounds.max.y + collision_epsilon, bounds.max.z + collision_epsilon}},
      scratch.boxes);
  for (const Aabb3f& box : scratch.boxes) {
    if (bounds.overlaps(box)) {
      return false;
    }
  }
  return true;
}

MovementResolution resolve_movement_with_scratch(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f delta,
    f32 collision_epsilon,
    CollisionScratch& scratch) {
  scratch.boxes.clear();
  scratch.boxes.reserve(64);
  const Aabb3f broad = swept(bounds, delta);
  detail::collect_collision_boxes(
      query,
      {{broad.min.x - collision_epsilon, broad.min.y - collision_epsilon, broad.min.z - collision_epsilon},
       {broad.max.x + collision_epsilon, broad.max.y + collision_epsilon, broad.max.z + collision_epsilon}},
      scratch.boxes);

  MovementResolution result;
  result.attempted_delta = delta;
  result.applied_delta = delta;
  Aabb3f resolved = bounds;

  const f32 dx = detail::clamp_axis_delta(
      resolved,
      delta.x,
      detail::Axis::X,
      scratch.boxes,
      collision_epsilon);
  result.hit_x = dx != delta.x;
  result.applied_delta.x = dx;
  resolved.min.x += dx;
  resolved.max.x += dx;

  const f32 dy = detail::clamp_axis_delta(
      resolved,
      delta.y,
      detail::Axis::Y,
      scratch.boxes,
      collision_epsilon);
  result.hit_y = dy != delta.y;
  result.hit_down = result.hit_y && delta.y < 0.0F;
  result.applied_delta.y = dy;
  resolved.min.y += dy;
  resolved.max.y += dy;

  const f32 dz = detail::clamp_axis_delta(
      resolved,
      delta.z,
      detail::Axis::Z,
      scratch.boxes,
      collision_epsilon);
  result.hit_z = dz != delta.z;
  result.applied_delta.z = dz;
  resolved.min.z += dz;
  resolved.max.z += dz;

  result.resolved_bounds = resolved;
  result.grounded = result.hit_down || has_support_below(query, resolved, collision_epsilon * 2.0F);
  return result;
}

} // namespace detail

MovementResolution resolve_movement(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f delta,
    f32 collision_epsilon) {
  detail::CollisionScratch scratch;
  return detail::resolve_movement_with_scratch(query, bounds, delta, collision_epsilon, scratch);
}

bool overlaps_solid_blocks(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 collision_epsilon) {
  detail::CollisionScratch scratch;
  return !detail::is_space_clear_with_scratch(query, bounds, collision_epsilon, scratch);
}

bool has_support_below(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 probe_depth) {
  return probe_support_surface(query, bounds, probe_depth).supported;
}

SupportSurfaceProbe probe_support_surface(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 probe_depth) {
  const Aabb3f probe_bounds{
      {bounds.min.x, bounds.min.y - probe_depth, bounds.min.z},
      {bounds.max.x, bounds.min.y, bounds.max.z}};
  const i32 min_x = floor_to_i32(probe_bounds.min.x);
  const i32 min_y = floor_to_i32(probe_bounds.min.y);
  const i32 min_z = floor_to_i32(probe_bounds.min.z);
  const i32 max_x = static_cast<i32>(std::ceil(probe_bounds.max.x));
  const i32 max_y = static_cast<i32>(std::ceil(probe_bounds.max.y));
  const i32 max_z = static_cast<i32>(std::ceil(probe_bounds.max.z));

  SupportSurfaceProbe probe;
  const auto consider_box =
      [&](BlockPos block, Aabb3f box, voxel::BlockPhysicsProperties physics) {
        const bool overlaps_horizontally =
            intervals_overlap_open(bounds.min.x, bounds.max.x, box.min.x, box.max.x) &&
            intervals_overlap_open(bounds.min.z, bounds.max.z, box.min.z, box.max.z);
        const bool touches_from_below =
            box.max.y <= bounds.min.y + probe_depth && box.max.y >= bounds.min.y - probe_depth;
        if (!overlaps_horizontally || !touches_from_below) {
          return;
        }

        const bool higher = !probe.supported || box.max.y > probe.support_y + 1.0e-6F;
        const bool tie_more_slippery =
            probe.supported && std::abs(box.max.y - probe.support_y) <= 1.0e-6F &&
            physics.slipperiness > probe.physics.slipperiness;
        if (higher || tie_more_slippery) {
          probe.supported = true;
          probe.block = block;
          probe.support_y = box.max.y;
          probe.physics = physics;
        }
      };

  for (i32 z = min_z; z < max_z; ++z) {
    for (i32 y = min_y; y < max_y; ++y) {
      for (i32 x = min_x; x < max_x; ++x) {
        const BlockPos block{{x, y, z}};
        const Vec3f block_offset{
            static_cast<f32>(x),
            static_cast<f32>(y),
            static_cast<f32>(z)};

        if (!query.is_block_loaded(block)) {
          if (query.missing_blocks_are_solid()) {
            consider_box(
                block,
                {block_offset, block_offset + Vec3f{1.0F, 1.0F, 1.0F}},
                {});
          }
          continue;
        }

        const voxel::BlockBody* body = query.block_body(block);
        if (body == nullptr) {
          continue;
        }
        const voxel::BlockPhysicsProperties physics = query.block_physics(block);
        for (const Aabb3f& local_box : body->boxes) {
          consider_box(
              block,
              {local_box.min + block_offset, local_box.max + block_offset},
              physics);
        }
      }
    }
  }

  return probe;
}

} // namespace arv::physics
