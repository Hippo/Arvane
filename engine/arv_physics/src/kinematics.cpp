#include "arv/physics/kinematics.hpp"

#include "collision_internal.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace arv::physics {
namespace {

[[nodiscard]] Vec3f approach_horizontal_velocity(
    Vec3f current,
    Vec3f target,
    f32 max_delta) noexcept {
  const Vec3f delta{target.x - current.x, 0.0F, target.z - current.z};
  const f32 delta_sq = horizontal_length_squared(delta);
  if (delta_sq <= 0.0F || max_delta <= 0.0F) {
    return {current.x, 0.0F, current.z};
  }
  const f32 max_delta_sq = max_delta * max_delta;
  if (delta_sq <= max_delta_sq) {
    return {target.x, 0.0F, target.z};
  }
  const f32 scale = max_delta / std::sqrt(delta_sq);
  return {
      current.x + delta.x * scale,
      0.0F,
      current.z + delta.z * scale};
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

[[nodiscard]] std::optional<MovementResolution> try_step_up(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f attempted_delta,
    const MovementResolution& normal_movement,
    f32 step_height,
    f32 collision_epsilon,
    detail::CollisionScratch& scratch) {
  const Vec3f horizontal_delta{attempted_delta.x, 0.0F, attempted_delta.z};
  if (step_height <= collision_epsilon ||
      horizontal_length_squared(horizontal_delta) <= collision_epsilon * collision_epsilon ||
      !(normal_movement.hit_x || normal_movement.hit_z)) {
    return std::nullopt;
  }

  const MovementResolution lift = detail::resolve_movement_with_scratch(
      query,
      bounds,
      {0.0F, step_height, 0.0F},
      collision_epsilon,
      scratch);
  if (lift.applied_delta.y <= collision_epsilon) {
    return std::nullopt;
  }

  const Aabb3f lifted_bounds = translated(bounds, lift.applied_delta);
  const MovementResolution horizontal = detail::resolve_movement_with_scratch(
      query,
      lifted_bounds,
      horizontal_delta,
      collision_epsilon,
      scratch);
  const f32 normal_horizontal_distance =
      horizontal_length_squared(normal_movement.applied_delta);
  const f32 stepped_horizontal_distance =
      horizontal_length_squared(horizontal.applied_delta);
  if (horizontal.hit_x || horizontal.hit_z ||
      stepped_horizontal_distance <= normal_horizontal_distance + collision_epsilon * collision_epsilon) {
    return std::nullopt;
  }

  const Aabb3f after_horizontal = translated(lifted_bounds, horizontal.applied_delta);
  const MovementResolution settle = detail::resolve_movement_with_scratch(
      query,
      after_horizontal,
      {0.0F, -lift.applied_delta.y, 0.0F},
      collision_epsilon,
      scratch);
  const Aabb3f settled_bounds = translated(after_horizontal, settle.applied_delta);
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

struct LiquidContactProbe {
  LiquidContactState state{LiquidContactState::NotInLiquid};
  f32 highest_surface_y{};
};

[[nodiscard]] bool point_is_under_liquid_surface(
    const ICollisionQuery& query,
    Vec3f point,
    f32 collision_epsilon) {
  const BlockPos pos{{floor_to_i32(point.x), floor_to_i32(point.y), floor_to_i32(point.z)}};
  const voxel::FluidCell fluid = query.fluid_cell(pos);
  return !fluid.empty() && point.y < voxel::fluid_surface_y(pos, fluid) - collision_epsilon;
}

[[nodiscard]] LiquidContactProbe probe_liquid_contact(
    const ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f head_point,
    f32 collision_epsilon) {
  const i32 min_x = floor_to_i32(bounds.min.x);
  const i32 min_y = floor_to_i32(bounds.min.y);
  const i32 min_z = floor_to_i32(bounds.min.z);
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

        const f32 surface_y = voxel::fluid_surface_y(pos, fluid);
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

KinematicStepResult step_kinematic_body(
    const ICollisionQuery& query,
    const KinematicBody& body,
    f32 yaw_radians,
    Seconds tick_seconds,
    const KinematicInput& input,
    const KinematicConfig& config) {
  KinematicBody next = body;
  detail::CollisionScratch scratch;
  scratch.boxes.reserve(64);
  PlayerCollisionPose current_pose = pose_from_body(body);
  const Aabb3f current_bounds = make_body_bounds(body.position, config, current_pose);
  const Vec3f current_head{
      body.position.x,
      body.position.y + config.standing_eye_height,
      body.position.z};
  const LiquidContactProbe liquid =
      probe_liquid_contact(query, current_bounds, current_head, config.collision_epsilon);
  const bool liquid_movement = !input.fly_mode && in_liquid(liquid.state);
  const MovementBasis basis = horizontal_movement_basis(yaw_radians);

  Vec3f move{};
  if (input.move_z != 0) {
    move = move + basis.forward * static_cast<f32>(input.move_z);
  }
  if (input.move_x != 0) {
    move = move + basis.right * static_cast<f32>(input.move_x);
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
      !detail::is_space_clear_with_scratch(
          query,
          make_body_bounds(body.position, config, requested_pose),
          config.collision_epsilon,
          scratch)) {
    active_pose = current_pose;
  }
  next.swimming = active_pose == PlayerCollisionPose::Swimming;
  next.sneaking = active_pose == PlayerCollisionPose::Sneaking;
  bool clamp_surface_float = false;
  f32 surface_float_y = body.position.y;
  const Aabb3f active_bounds = make_body_bounds(body.position, config, active_pose);

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
        swim_move = swim_move + basis.right * static_cast<f32>(input.move_x);
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
    if (body.grounded) {
      const f32 horizontal_speed =
          next.sneaking ? config.sneak_speed
                        : (should_land_sprint(input) ? config.sprint_speed
                                                     : config.walk_speed);
      const SupportSurfaceProbe surface = probe_support_surface(
          query,
          active_bounds,
          config.collision_epsilon * 2.0F);
      const f32 default_slipperiness = std::clamp(
          config.default_surface_slipperiness,
          0.02F,
          1.25F);
      const f32 slipperiness = std::clamp(
          surface.supported ? surface.physics.slipperiness : config.default_surface_slipperiness,
          0.02F,
          1.25F);
      const f32 surface_ratio = default_slipperiness / slipperiness;
      const f32 min_surface_control = std::min(config.min_surface_control, config.max_surface_control);
      const f32 max_surface_control = std::max(config.min_surface_control, config.max_surface_control);
      const f32 surface_control = std::clamp(
          surface_ratio * surface_ratio * surface_ratio,
          min_surface_control,
          max_surface_control);
      const bool has_move_input = input.move_x != 0 || input.move_z != 0;
      const f32 base_rate =
          has_move_input ? config.ground_acceleration_per_second
                         : config.ground_deceleration_per_second;
      const Vec3f desired_horizontal{
          move.x * horizontal_speed,
          0.0F,
          move.z * horizontal_speed};
      const Vec3f controlled_horizontal = approach_horizontal_velocity(
          body.velocity,
          desired_horizontal,
          std::max(0.0F, base_rate) * surface_control * tick_seconds);
      next.velocity.x = controlled_horizontal.x;
      next.velocity.z = controlled_horizontal.z;
    } else {
      const Vec3f desired_horizontal{
          move.x * config.air_speed,
          0.0F,
          move.z * config.air_speed};
      const Vec3f controlled_horizontal = approach_horizontal_velocity(
          body.velocity,
          desired_horizontal,
          std::max(0.0F, config.air_control_acceleration_per_second) *
              tick_seconds);
      next.velocity.x = controlled_horizontal.x;
      next.velocity.z = controlled_horizontal.z;
    }

    const bool jumped = (input.jump_pressed || input.jump_held) && body.grounded;
    if (jumped) {
      if (should_land_sprint(input) && config.sprint_jump_horizontal_boost > 0.0F) {
        next.velocity.x += move.x * config.sprint_jump_horizontal_boost;
        next.velocity.z += move.z * config.sprint_jump_horizontal_boost;
      }
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
  if (!input.fly_mode && !liquid_movement && body.grounded && next.sneaking) {
    movement_delta =
        clamp_sneak_horizontal_delta(query, active_bounds, movement_delta, config.sneak_support_probe_depth);
    if (tick_seconds > 0.0F) {
      next.velocity.x = movement_delta.x / tick_seconds;
      next.velocity.z = movement_delta.z / tick_seconds;
    }
  }

  MovementResolution movement = detail::resolve_movement_with_scratch(
      query,
      active_bounds,
      movement_delta,
      config.collision_epsilon,
      scratch);
  if (!input.fly_mode && !liquid_movement && body.grounded && movement_delta.y <= config.collision_epsilon) {
    if (std::optional<MovementResolution> stepped = try_step_up(
            query,
            active_bounds,
            movement_delta,
            movement,
            config.step_height,
            config.collision_epsilon,
            scratch);
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

} // namespace arv::physics
