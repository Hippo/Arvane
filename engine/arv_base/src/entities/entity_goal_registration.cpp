#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace arv::base {
namespace {

[[nodiscard]] f32 positive_fraction(f32 value) noexcept {
  const f32 whole = std::floor(value);
  return value - whole;
}

[[nodiscard]] bool has_horizontal_motion(Vec3f value) noexcept {
  return value.x * value.x + value.z * value.z > 0.000001F;
}

[[nodiscard]] Vec3f deterministic_wander_direction(
    EntityID entity_id,
    TickIndex tick,
    usize goal_index) noexcept {
  const f32 seed =
      static_cast<f32>(entity_id.value * 37U + static_cast<u32>(goal_index) * 101U) +
      static_cast<f32>(tick % 100000U) * 0.173F;
  const f32 angle = positive_fraction(std::sin(seed) * 43758.5453F) * TauRadians;
  return {std::cos(angle), 0.0F, std::sin(angle)};
}

[[nodiscard]] f32 horizontal_progress_squared(
    const physics::ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f direction,
    f32 probe_distance,
    f32 collision_epsilon) {
  const Vec3f horizontal_direction = normalize_or_zero({direction.x, 0.0F, direction.z});
  if (!has_horizontal_motion(horizontal_direction)) {
    return false;
  }

  const physics::MovementResolution resolved = physics::resolve_movement(
      query,
      bounds,
      horizontal_direction * std::max(probe_distance, collision_epsilon * 4.0F),
      collision_epsilon);
  return horizontal_length_squared(resolved.applied_delta);
}

[[nodiscard]] Vec3f cardinal_horizontal_direction(usize index) noexcept {
  switch (index % 4U) {
    case 0U:
      return {1.0F, 0.0F, 0.0F};
    case 1U:
      return {-1.0F, 0.0F, 0.0F};
    case 2U:
      return {0.0F, 0.0F, 1.0F};
    default:
      return {0.0F, 0.0F, -1.0F};
  }
}

[[nodiscard]] Vec3f random_clear_wander_direction(
    EntityID entity_id,
    TickIndex tick,
    usize goal_index,
    const physics::ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f blocked_delta,
    f32 collision_epsilon) {
  const f32 probe_distance = std::sqrt(horizontal_length_squared(blocked_delta));
  Vec3f fallback{};
  Vec3f best{};
  f32 best_score{};

  const auto consider = [&](Vec3f candidate) {
    candidate.y = 0.0F;
    candidate = normalize_or_zero(candidate);
    if (!has_horizontal_motion(candidate)) {
      return;
    }
    if (!has_horizontal_motion(fallback)) {
      fallback = candidate;
    }

    const f32 score = horizontal_progress_squared(
        query,
        bounds,
        candidate,
        probe_distance,
        collision_epsilon);
    if (score > best_score) {
      best_score = score;
      best = candidate;
    }
  };

  constexpr usize MaxAttempts = 12;
  for (usize attempt = 0; attempt < MaxAttempts; ++attempt) {
    consider(deterministic_wander_direction(
        entity_id,
        tick + static_cast<TickIndex>(attempt + 1U),
        goal_index + attempt + 1U));
  }

  const usize cardinal_start =
      static_cast<usize>(entity_id.value + static_cast<u64>(tick) + goal_index);
  for (usize attempt = 0; attempt < 4U; ++attempt) {
    consider(cardinal_horizontal_direction(cardinal_start + attempt));
  }

  if (best_score > collision_epsilon * collision_epsilon) {
    return best;
  }
  return fallback;
}

[[nodiscard]] BlockPos entity_fluid_probe_position(
    const world::EntityGoalContext& context) noexcept {
  return {{
      static_cast<i32>(std::floor(context.transform.position.x)),
      static_cast<i32>(std::floor(context.transform.position.y)),
      static_cast<i32>(std::floor(context.transform.position.z)),
  }};
}

[[nodiscard]] bool entity_in_fluid(world::EntityGoalContext& context) {
  return !context.world.get_fluid(entity_fluid_probe_position(context)).empty();
}

[[nodiscard]] world::EntityGoalHandler make_idle_goal_handler() {
  world::EntityGoalHandler handler;
  handler.can_continue = [](world::EntityGoalContext& context) {
    return context.goal.state.remaining_ticks > 0U;
  };
  handler.tick = [](world::EntityGoalContext& context) {
    context.clear_movement_intent();
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_wander_goal_handler() {
  world::EntityGoalHandler handler;
  handler.can_continue = [](world::EntityGoalContext& context) {
    return context.goal.state.remaining_ticks > 0U;
  };
  handler.start = [](world::EntityGoalContext& context) {
    context.goal.state.movement_direction = deterministic_wander_direction(
        context.entity_id,
        context.tick_index,
        context.goal_index);
  };
  handler.tick = [](world::EntityGoalContext& context) {
    entity::MovementIntent& intent = context.movement_intent();
    intent.desired_direction = context.goal.state.movement_direction;
    intent.speed_multiplier = std::max(0.0F, context.goal.speed_multiplier);
    intent.jump = false;
    intent.sprint = false;
  };
  handler.blocked = [](world::EntityGoalBlockedContext& context) {
    if (!has_horizontal_motion(context.goal.state.movement_direction)) {
      return;
    }
    const Vec3f redirected = random_clear_wander_direction(
        context.entity_id,
        context.tick_index,
        context.goal_index,
        context.collision_query,
        context.bounds,
        context.blocked_delta,
        context.collision_epsilon);
    if (!has_horizontal_motion(redirected)) {
      return;
    }

    context.goal.state.movement_direction = redirected;
    if (context.movement_intent != nullptr) {
      context.movement_intent->desired_direction = redirected;
    }
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_float_in_fluid_goal_handler() {
  world::EntityGoalHandler handler;
  handler.can_start = entity_in_fluid;
  handler.can_continue = entity_in_fluid;
  handler.start = [](world::EntityGoalContext& context) {
    context.goal.state.movement_direction = {0.0F, 1.0F, 0.0F};
  };
  handler.tick = [](world::EntityGoalContext& context) {
    entity::MovementIntent& intent = context.movement_intent();
    intent.desired_direction = context.goal.state.movement_direction;
    intent.speed_multiplier = std::max(0.0F, context.goal.speed_multiplier);
    intent.jump = true;
    intent.sprint = false;
  };
  return handler;
}

} // namespace

void register_base_entity_goals(world::EntityGoalRegistry& registry) {
  auto result = registry.register_handler(
      std::string{ids::IdleGoalID},
      make_idle_goal_handler());
  assert(result && "idle goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::WanderGoalID},
      make_wander_goal_handler());
  assert(result && "wander goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::FloatInFluidGoalID},
      make_float_in_fluid_goal_handler());
  assert(result && "float-in-fluid goal registration failed");
  (void)result;
}

} // namespace arv::base
