#include "arv/physics/collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

[[nodiscard]] Vec3f view_forward(f32 yaw_radians, f32 pitch_radians) noexcept {
  const f32 cos_pitch = std::cos(pitch_radians);
  return normalize_or_zero(
      {std::sin(yaw_radians) * cos_pitch,
       std::sin(pitch_radians),
       std::cos(yaw_radians) * cos_pitch});
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

void append_block_body_boxes(
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

void collect_collision_boxes(
    const ICollisionQuery& query,
    const Aabb3f& world_bounds,
    std::vector<Aabb3f>& out_boxes) {
  const i32 min_x = floor_i32(world_bounds.min.x);
  const i32 min_y = floor_i32(world_bounds.min.y);
  const i32 min_z = floor_i32(world_bounds.min.z);
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

[[nodiscard]] bool is_space_clear(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 collision_epsilon) {
  std::vector<Aabb3f> boxes;
  boxes.reserve(32);
  collect_collision_boxes(
      query,
      {{bounds.min.x - collision_epsilon, bounds.min.y - collision_epsilon, bounds.min.z - collision_epsilon},
       {bounds.max.x + collision_epsilon, bounds.max.y + collision_epsilon, bounds.max.z + collision_epsilon}},
      boxes);
  for (const Aabb3f& box : boxes) {
    if (bounds.overlaps(box)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Vec3f clamp_sneak_horizontal_delta(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f delta,
    f32 probe_depth) {
  if ((delta.x == 0.0F && delta.z == 0.0F) || has_support_below(
                                                query,
                                                {{bounds.min.x + delta.x, bounds.min.y, bounds.min.z + delta.z},
                                                 {bounds.max.x + delta.x, bounds.max.y, bounds.max.z + delta.z}},
                                                probe_depth)) {
    return delta;
  }

  Aabb3f resolved = bounds;
  if (delta.x != 0.0F) {
    const Aabb3f x_bounds{
        {resolved.min.x + delta.x, resolved.min.y, resolved.min.z},
        {resolved.max.x + delta.x, resolved.max.y, resolved.max.z}};
    if (has_support_below(query, x_bounds, probe_depth)) {
      resolved = x_bounds;
    } else {
      delta.x = 0.0F;
    }
  }

  if (delta.z != 0.0F) {
    const Aabb3f z_bounds{
        {resolved.min.x, resolved.min.y, resolved.min.z + delta.z},
        {resolved.max.x, resolved.max.y, resolved.max.z + delta.z}};
    if (!has_support_below(query, z_bounds, probe_depth)) {
      delta.z = 0.0F;
    }
  }

  return delta;
}

[[nodiscard]] f32 horizontal_delta_length_squared(Vec3f delta) noexcept {
  return delta.x * delta.x + delta.z * delta.z;
}

[[nodiscard]] Aabb3f translated_bounds(const Aabb3f& bounds, Vec3f delta) noexcept {
  return {
      {bounds.min.x + delta.x, bounds.min.y + delta.y, bounds.min.z + delta.z},
      {bounds.max.x + delta.x, bounds.max.y + delta.y, bounds.max.z + delta.z}};
}

[[nodiscard]] std::optional<MovementResolution> try_step_up(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f attempted_delta,
    const MovementResolution& normal_movement,
    f32 step_height,
    f32 collision_epsilon) {
  const Vec3f horizontal_delta{attempted_delta.x, 0.0F, attempted_delta.z};
  if (step_height <= collision_epsilon ||
      horizontal_delta_length_squared(horizontal_delta) <= collision_epsilon * collision_epsilon ||
      !(normal_movement.hit_x || normal_movement.hit_z)) {
    return std::nullopt;
  }

  const MovementResolution lift = resolve_movement(
      query,
      bounds,
      {0.0F, step_height, 0.0F},
      collision_epsilon);
  if (lift.applied_delta.y <= collision_epsilon) {
    return std::nullopt;
  }

  const Aabb3f lifted_bounds = translated_bounds(bounds, lift.applied_delta);
  const MovementResolution horizontal = resolve_movement(
      query,
      lifted_bounds,
      horizontal_delta,
      collision_epsilon);
  const f32 normal_horizontal_distance =
      horizontal_delta_length_squared(normal_movement.applied_delta);
  const f32 stepped_horizontal_distance =
      horizontal_delta_length_squared(horizontal.applied_delta);
  if (horizontal.hit_x || horizontal.hit_z ||
      stepped_horizontal_distance <= normal_horizontal_distance + collision_epsilon * collision_epsilon) {
    return std::nullopt;
  }

  const Aabb3f after_horizontal = translated_bounds(lifted_bounds, horizontal.applied_delta);
  const MovementResolution settle = resolve_movement(
      query,
      after_horizontal,
      {0.0F, -lift.applied_delta.y, 0.0F},
      collision_epsilon);
  const Aabb3f settled_bounds = translated_bounds(after_horizontal, settle.applied_delta);
  if (!settle.hit_down && !has_support_below(query, settled_bounds, collision_epsilon * 2.0F)) {
    return std::nullopt;
  }

  MovementResolution stepped;
  stepped.attempted_delta = attempted_delta;
  stepped.applied_delta = {
      horizontal.applied_delta.x,
      lift.applied_delta.y + settle.applied_delta.y,
      horizontal.applied_delta.z};
  stepped.hit_x = horizontal.hit_x;
  stepped.hit_y = lift.hit_y || settle.hit_y;
  stepped.hit_z = horizontal.hit_z;
  stepped.hit_down = settle.hit_down;
  stepped.grounded = true;
  stepped.resolved_bounds = settled_bounds;
  return stepped;
}

[[nodiscard]] PlayerCollisionPose pose_from_body(const KinematicBody& body) noexcept {
  if (body.swimming) {
    return PlayerCollisionPose::Swimming;
  }
  return body.sneaking ? PlayerCollisionPose::Sneaking : PlayerCollisionPose::Standing;
}

[[nodiscard]] f32 fluid_surface_y(BlockPos pos, voxel::FluidCell fluid) noexcept {
  if (fluid.empty()) {
    return static_cast<f32>(pos.value.y);
  }
  constexpr f32 MaxFluidLevel = 8.0F;
  const f32 level = fluid.source()
                        ? MaxFluidLevel
                        : std::clamp(static_cast<f32>(fluid.level), 0.0F, MaxFluidLevel);
  return static_cast<f32>(pos.value.y) + level / MaxFluidLevel;
}

struct LiquidContactProbe {
  LiquidContactState state{LiquidContactState::NotInLiquid};
  f32 highest_surface_y{};
};

[[nodiscard]] bool point_is_under_liquid_surface(
    const ICollisionQuery& query,
    Vec3f point,
    f32 collision_epsilon) {
  const BlockPos pos{{floor_i32(point.x), floor_i32(point.y), floor_i32(point.z)}};
  const voxel::FluidCell fluid = query.fluid_cell(pos);
  return !fluid.empty() && point.y < fluid_surface_y(pos, fluid) - collision_epsilon;
}

[[nodiscard]] LiquidContactProbe probe_liquid_contact(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f head_point,
    f32 collision_epsilon) {
  const i32 min_x = floor_i32(bounds.min.x);
  const i32 min_y = floor_i32(bounds.min.y);
  const i32 min_z = floor_i32(bounds.min.z);
  const i32 max_x = static_cast<i32>(std::ceil(bounds.max.x));
  const i32 max_y = static_cast<i32>(std::ceil(bounds.max.y));
  const i32 max_z = static_cast<i32>(std::ceil(bounds.max.z));

  LiquidContactProbe probe;
  bool overlaps_liquid = false;
  for (i32 z = min_z; z < max_z; ++z) {
    for (i32 y = min_y; y < max_y; ++y) {
      for (i32 x = min_x; x < max_x; ++x) {
        const BlockPos pos{{x, y, z}};
        const voxel::FluidCell fluid = query.fluid_cell(pos);
        if (fluid.empty()) {
          continue;
        }

        const f32 surface_y = fluid_surface_y(pos, fluid);
        if (bounds.max.y <= static_cast<f32>(y) + collision_epsilon ||
            bounds.min.y >= surface_y - collision_epsilon) {
          continue;
        }

        overlaps_liquid = true;
        probe.highest_surface_y = std::max(probe.highest_surface_y, surface_y);
      }
    }
  }

  if (!overlaps_liquid) {
    return probe;
  }

  if (point_is_under_liquid_surface(query, head_point, collision_epsilon)) {
    probe.state = LiquidContactState::SwimmingUnderwater;
  } else if (bounds.min.y < probe.highest_surface_y - collision_epsilon) {
    probe.state = LiquidContactState::SwimmingAtSurface;
  } else {
    probe.state = LiquidContactState::PartiallyInLiquid;
  }
  return probe;
}

[[nodiscard]] bool in_liquid(LiquidContactState state) noexcept {
  return state != LiquidContactState::NotInLiquid;
}

[[nodiscard]] bool should_land_sprint(const KinematicInput& input) noexcept {
  return input.sprint_held && input.move_z > 0 && !input.sneak_held;
}

} // namespace

Aabb3f make_body_bounds(Vec3f position, const KinematicConfig& config) noexcept {
  return make_body_bounds(position, config, PlayerCollisionPose::Standing);
}

Aabb3f make_body_bounds(
    Vec3f position,
    const KinematicConfig& config,
    PlayerCollisionPose pose) noexcept {
  const f32 body_width =
      pose == PlayerCollisionPose::Swimming ? config.swimming_body_width : config.body_width;
  const f32 half_width = body_width * 0.5F;
  const f32 body_height =
      pose == PlayerCollisionPose::Swimming
          ? config.swimming_body_height
          : (pose == PlayerCollisionPose::Sneaking ? config.sneaking_body_height
                                                   : config.body_height);
  return {
      {position.x - half_width, position.y, position.z - half_width},
      {position.x + half_width, position.y + body_height, position.z + half_width}};
}

Aabb3f make_body_bounds(Vec3f position, const KinematicConfig& config, bool sneaking) noexcept {
  return make_body_bounds(
      position,
      config,
      sneaking ? PlayerCollisionPose::Sneaking : PlayerCollisionPose::Standing);
}

MovementResolution resolve_movement(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f delta,
    f32 collision_epsilon) {
  std::vector<Aabb3f> boxes;
  boxes.reserve(64);
  const Aabb3f broad = swept_bounds(bounds, delta);
  collect_collision_boxes(
      query,
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
  collect_collision_boxes(query, probe, boxes);

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
  PlayerCollisionPose current_pose = pose_from_body(body);
  const Aabb3f current_bounds = make_body_bounds(body.position, config, current_pose);
  const Vec3f current_head{
      body.position.x,
      body.position.y + config.standing_eye_height,
      body.position.z};
  const LiquidContactProbe liquid =
      probe_liquid_contact(query, current_bounds, current_head, config.collision_epsilon);
  const bool liquid_movement = !input.fly_mode && in_liquid(liquid.state);

  Vec3f move{};
  if (input.move_z != 0) {
    move = move + horizontal_forward(yaw_radians) * static_cast<f32>(input.move_z);
  }
  if (input.move_x != 0) {
    move = move + horizontal_right(yaw_radians) * static_cast<f32>(input.move_x);
  }
  move = normalize_or_zero(move);

  PlayerCollisionPose requested_pose = PlayerCollisionPose::Standing;
  if (!input.fly_mode) {
    const bool swim_fast = liquid_movement && input.sprint_held &&
                           (input.move_x != 0 || input.move_z != 0) && !input.sneak_held;
    if (swim_fast) {
      requested_pose = PlayerCollisionPose::Swimming;
    } else if (!liquid_movement && input.sneak_held) {
      requested_pose = PlayerCollisionPose::Sneaking;
    }
  }

  PlayerCollisionPose active_pose = requested_pose;
  if (requested_pose != current_pose &&
      !is_space_clear(
          query,
          make_body_bounds(body.position, config, requested_pose),
          config.collision_epsilon)) {
    active_pose = current_pose;
  }
  next.swimming = active_pose == PlayerCollisionPose::Swimming;
  next.sneaking = active_pose == PlayerCollisionPose::Sneaking;
  bool clamp_surface_float = false;
  f32 surface_float_y = body.position.y;

  if (input.fly_mode) {
    next.swimming = false;
    next.sneaking = false;
    next.velocity.x = move.x * config.fly_speed;
    next.velocity.z = move.z * config.fly_speed;

    f32 vertical = 0.0F;
    if (input.jump_held) {
      vertical += 1.0F;
    }
    if (input.sneak_held) {
      vertical -= 1.0F;
    }
    next.velocity.y = vertical * config.fly_vertical_speed;
    next.breath_seconds = config.breath_seconds;
  } else if (liquid_movement) {
    const bool underwater = liquid.state == LiquidContactState::SwimmingUnderwater;
    const bool jump_out_of_surface =
        !underwater && (input.jump_pressed || input.jump_held);
    const bool fast_swim = input.sprint_held && (input.move_x != 0 || input.move_z != 0);
    const f32 swim_speed =
        underwater
            ? (fast_swim ? config.fast_underwater_swim_speed : config.underwater_swim_speed)
            : (fast_swim ? config.fast_surface_swim_speed : config.surface_swim_speed);

    Vec3f swim_move = move;
    if (underwater && fast_swim && input.move_z > 0) {
      swim_move = view_forward(yaw_radians, input.pitch_radians) * static_cast<f32>(input.move_z);
      if (input.move_x != 0) {
        swim_move = swim_move + horizontal_right(yaw_radians) * static_cast<f32>(input.move_x);
      }
      swim_move = normalize_or_zero(swim_move);
    }

    next.velocity.x = swim_move.x * swim_speed;
    next.velocity.z = swim_move.z * swim_speed;
    next.velocity.y = body.velocity.y * std::clamp(config.liquid_drag, 0.0F, 1.0F);

    if (underwater && fast_swim && input.move_z > 0) {
      next.velocity.y += swim_move.y * swim_speed;
    }
    if (jump_out_of_surface) {
      next.velocity.y = std::max(next.velocity.y, config.jump_velocity);
    } else if (input.jump_held) {
      next.velocity.y += config.upward_swim_acceleration * tick_seconds;
    }
    if (input.sneak_held) {
      next.velocity.y -= config.downward_swim_acceleration * tick_seconds;
    } else if (underwater) {
      next.velocity.y += config.underwater_buoyancy * tick_seconds;
    } else {
      surface_float_y = liquid.highest_surface_y - config.surface_float_depth;
      clamp_surface_float = !jump_out_of_surface;
      if (body.position.y < surface_float_y - config.collision_epsilon) {
        next.velocity.y =
            std::max(0.0F, next.velocity.y + config.surface_buoyancy * tick_seconds);
      } else if (body.position.y > surface_float_y + config.collision_epsilon) {
        next.velocity.y -= config.surface_buoyancy * tick_seconds;
      }
    }

    next.velocity.y = std::clamp(
        next.velocity.y,
        -config.terminal_velocity,
        config.terminal_velocity);
    next.breath_seconds =
        underwater ? std::max(0.0F, body.breath_seconds - tick_seconds)
                   : config.breath_seconds;
  } else {
    next.breath_seconds = config.breath_seconds;
    const f32 horizontal_speed =
        body.grounded ? (next.sneaking ? config.sneak_speed
                                       : (should_land_sprint(input) ? config.sprint_speed
                                                                    : config.walk_speed))
                      : config.air_speed;
    next.velocity.x = move.x * horizontal_speed;
    next.velocity.z = move.z * horizontal_speed;

    const bool jumped = (input.jump_pressed || input.jump_held) && body.grounded;
    if (jumped) {
      next.velocity.y = config.jump_velocity;
    } else if (!body.grounded) {
      next.velocity.y =
          std::max(body.velocity.y - config.gravity_per_second * tick_seconds, -config.terminal_velocity);
    } else if (next.velocity.y < 0.0F) {
      next.velocity.y = 0.0F;
    }
  }

  Vec3f movement_delta = next.velocity * tick_seconds;
  if (clamp_surface_float && tick_seconds > 0.0F) {
    const f32 delta_to_float_y = surface_float_y - body.position.y;
    const bool crosses_float_y =
        (delta_to_float_y >= 0.0F && movement_delta.y > delta_to_float_y) ||
        (delta_to_float_y <= 0.0F && movement_delta.y < delta_to_float_y);
    if (crosses_float_y) {
      movement_delta.y = delta_to_float_y;
      next.velocity.y = movement_delta.y / tick_seconds;
    }
  }
  const Aabb3f active_bounds = make_body_bounds(body.position, config, active_pose);
  if (!input.fly_mode && !liquid_movement && body.grounded && next.sneaking) {
    movement_delta =
        clamp_sneak_horizontal_delta(query, active_bounds, movement_delta, config.sneak_support_probe_depth);
    if (tick_seconds > 0.0F) {
      next.velocity.x = movement_delta.x / tick_seconds;
      next.velocity.z = movement_delta.z / tick_seconds;
    }
  }

  MovementResolution movement = resolve_movement(
      query,
      active_bounds,
      movement_delta,
      config.collision_epsilon);
  if (!input.fly_mode && !liquid_movement && body.grounded && movement_delta.y <= config.collision_epsilon) {
    if (std::optional<MovementResolution> stepped = try_step_up(
            query,
            active_bounds,
            movement_delta,
            movement,
            config.step_height,
            config.collision_epsilon);
        stepped.has_value()) {
      movement = stepped.value();
    }
  }

  next.position = body.position + movement.applied_delta;
  next.grounded = !input.fly_mode && !liquid_movement && movement.grounded;
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
  result.liquid_state = liquid.state;
  result.pose = active_pose;
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
    append_block_body_boxes(query, block, boxes);
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
