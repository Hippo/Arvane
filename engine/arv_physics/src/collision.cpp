#include "arv/physics/collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arv::physics {
namespace {

enum class Axis {
  X,
  Y,
  Z,
};

struct RayAabbHit {
  voxel::BlockFace face{voxel::BlockFace::PositiveY};
  f32 distance{};
  Vec3f hit_point{};
};

[[nodiscard]] f32 min_component(f32 a, f32 b) noexcept {
  return a < b ? a : b;
}

[[nodiscard]] f32 max_component(f32 a, f32 b) noexcept {
  return a > b ? a : b;
}

[[nodiscard]] bool ranges_overlap(f32 min_a, f32 max_a, f32 min_b, f32 max_b) noexcept {
  return min_a < max_b && max_a > min_b;
}

[[nodiscard]] bool overlaps_other_axes(const Aabb3f& a, const Aabb3f& b, Axis axis) noexcept {
  switch (axis) {
    case Axis::X:
      return ranges_overlap(a.min.y, a.max.y, b.min.y, b.max.y) &&
             ranges_overlap(a.min.z, a.max.z, b.min.z, b.max.z);
    case Axis::Y:
      return ranges_overlap(a.min.x, a.max.x, b.min.x, b.max.x) &&
             ranges_overlap(a.min.z, a.max.z, b.min.z, b.max.z);
    case Axis::Z:
      return ranges_overlap(a.min.x, a.max.x, b.min.x, b.max.x) &&
             ranges_overlap(a.min.y, a.max.y, b.min.y, b.max.y);
  }
  return false;
}

[[nodiscard]] f32 clamp_axis_delta(
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

[[nodiscard]] Vec3f horizontal_forward(f32 yaw_radians) noexcept {
  return normalize_or_zero({std::sin(yaw_radians), 0.0F, std::cos(yaw_radians)});
}

[[nodiscard]] Vec3f horizontal_right(f32 yaw_radians) noexcept {
  return normalize_or_zero({std::cos(yaw_radians), 0.0F, -std::sin(yaw_radians)});
}

[[nodiscard]] Aabb3f swept_bounds(const Aabb3f& bounds, Vec3f delta) noexcept {
  return {
      {min_component(bounds.min.x, bounds.min.x + delta.x),
       min_component(bounds.min.y, bounds.min.y + delta.y),
       min_component(bounds.min.z, bounds.min.z + delta.z)},
      {max_component(bounds.max.x, bounds.max.x + delta.x),
       max_component(bounds.max.y, bounds.max.y + delta.y),
       max_component(bounds.max.z, bounds.max.z + delta.z)}};
}

[[nodiscard]] std::optional<RayAabbHit> intersect_ray_aabb(
    Vec3f origin,
    Vec3f direction,
    const Aabb3f& box,
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
          box.min.x,
          box.max.x,
          voxel::BlockFace::NegativeX,
          voxel::BlockFace::PositiveX) ||
      !update_axis(
          origin.y,
          direction.y,
          box.min.y,
          box.max.y,
          voxel::BlockFace::NegativeY,
          voxel::BlockFace::PositiveY) ||
      !update_axis(
          origin.z,
          direction.z,
          box.min.z,
          box.max.z,
          voxel::BlockFace::NegativeZ,
          voxel::BlockFace::PositiveZ)) {
    return std::nullopt;
  }

  if (t_min < 0.0F || t_min > max_distance) {
    return std::nullopt;
  }

  return RayAabbHit{entry_face, t_min, origin + direction * t_min};
}

[[nodiscard]] i32 floor_i32(f32 value) noexcept {
  return static_cast<i32>(std::floor(value));
}

} // namespace

Aabb3f make_body_bounds(Vec3f position, const KinematicConfig& config) noexcept {
  const f32 half_width = config.body_width * 0.5F;
  return {
      {position.x - half_width, position.y, position.z - half_width},
      {position.x + half_width, position.y + config.body_height, position.z + half_width}};
}

MovementResolution resolve_movement(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f delta,
    f32 collision_epsilon) {
  std::vector<Aabb3f> boxes;
  boxes.reserve(64);
  const Aabb3f broad = swept_bounds(bounds, delta);
  query.gather_collision_boxes(
      {{broad.min.x - collision_epsilon, broad.min.y - collision_epsilon, broad.min.z - collision_epsilon},
       {broad.max.x + collision_epsilon, broad.max.y + collision_epsilon, broad.max.z + collision_epsilon}},
      boxes);

  MovementResolution result;
  result.attempted_delta = delta;
  result.applied_delta = delta;
  Aabb3f resolved = bounds;

  const f32 dx = clamp_axis_delta(resolved, delta.x, Axis::X, boxes, collision_epsilon);
  result.hit_x = dx != delta.x;
  result.applied_delta.x = dx;
  resolved.min.x += dx;
  resolved.max.x += dx;

  const f32 dy = clamp_axis_delta(resolved, delta.y, Axis::Y, boxes, collision_epsilon);
  result.hit_y = dy != delta.y;
  result.hit_down = result.hit_y && delta.y < 0.0F;
  result.applied_delta.y = dy;
  resolved.min.y += dy;
  resolved.max.y += dy;

  const f32 dz = clamp_axis_delta(resolved, delta.z, Axis::Z, boxes, collision_epsilon);
  result.hit_z = dz != delta.z;
  result.applied_delta.z = dz;
  resolved.min.z += dz;
  resolved.max.z += dz;

  result.resolved_bounds = resolved;
  result.grounded = result.hit_down || has_support_below(query, resolved, collision_epsilon * 2.0F);
  return result;
}

bool has_support_below(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 probe_depth) {
  std::vector<Aabb3f> boxes;
  boxes.reserve(16);
  Aabb3f probe{
      {bounds.min.x, bounds.min.y - probe_depth, bounds.min.z},
      {bounds.max.x, bounds.min.y, bounds.max.z}};
  query.gather_collision_boxes(probe, boxes);

  for (const Aabb3f& box : boxes) {
    const bool overlaps_horizontally =
        ranges_overlap(bounds.min.x, bounds.max.x, box.min.x, box.max.x) &&
        ranges_overlap(bounds.min.z, bounds.max.z, box.min.z, box.max.z);
    const bool touches_from_below =
        box.max.y <= bounds.min.y + probe_depth && box.max.y >= bounds.min.y - probe_depth;
    if (overlaps_horizontally && touches_from_below) {
      return true;
    }
  }

  return false;
}

KinematicStepResult step_kinematic_body(
    const ICollisionQuery& query,
    const KinematicBody& body,
    f32 yaw_radians,
    Seconds tick_seconds,
    const KinematicInput& input,
    const KinematicConfig& config) {
  KinematicBody next = body;

  Vec3f move{};
  if (input.move_z != 0) {
    move = move + horizontal_forward(yaw_radians) * static_cast<f32>(input.move_z);
  }
  if (input.move_x != 0) {
    move = move + horizontal_right(yaw_radians) * static_cast<f32>(input.move_x);
  }
  move = normalize_or_zero(move);

  if (input.fly_mode) {
    const f32 speed = input.sprint_held ? config.sprint_speed : config.fly_speed;
    next.velocity.x = move.x * speed;
    next.velocity.z = move.z * speed;

    f32 vertical = 0.0F;
    if (input.jump_held) {
      vertical += 1.0F;
    }
    if (input.move_down_held) {
      vertical -= 1.0F;
    }
    next.velocity.y = vertical * config.fly_vertical_speed;
  } else {
    const f32 horizontal_speed =
        body.grounded ? (input.sprint_held ? config.sprint_speed : config.walk_speed)
                      : config.air_speed;
    next.velocity.x = move.x * horizontal_speed;
    next.velocity.z = move.z * horizontal_speed;

    const bool jumped = input.jump_pressed && body.grounded;
    if (jumped) {
      next.velocity.y = config.jump_velocity;
    } else if (!body.grounded) {
      next.velocity.y =
          std::max(body.velocity.y - config.gravity_per_second * tick_seconds, -config.terminal_velocity);
    } else if (next.velocity.y < 0.0F) {
      next.velocity.y = 0.0F;
    }
  }

  const MovementResolution movement = resolve_movement(
      query,
      make_body_bounds(body.position, config),
      next.velocity * tick_seconds,
      config.collision_epsilon);

  next.position = body.position + movement.applied_delta;
  next.grounded = !input.fly_mode && movement.grounded;
  if (movement.hit_x) {
    next.velocity.x = 0.0F;
  }
  if (movement.hit_y) {
    next.velocity.y = 0.0F;
  }
  if (movement.hit_z) {
    next.velocity.z = 0.0F;
  }

  KinematicStepResult result;
  result.body = next;
  result.movement = movement;
  return result;
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

  i32 x = floor_i32(origin.x);
  i32 y = floor_i32(origin.y);
  i32 z = floor_i32(origin.z);

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

  std::vector<Aabb3f> boxes;
  boxes.reserve(8);

  constexpr usize MaxIterations = 2048;
  for (usize iteration = 0; iteration < MaxIterations; ++iteration) {
    const BlockPos block{{x, y, z}};
    boxes.clear();
    query.gather_block_collision_boxes(block, boxes);
    if (!boxes.empty()) {
      const voxel::BlockVariantID variant = query.block_variant(block);
      std::optional<RayAabbHit> closest_hit;
      for (const Aabb3f& box : boxes) {
        auto hit = intersect_ray_aabb(origin, direction, box, max_distance);
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
