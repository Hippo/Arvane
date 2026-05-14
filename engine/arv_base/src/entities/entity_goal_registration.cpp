#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/world/entity_goal_utils.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace arv::base {
namespace {

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
    context.goal.state.movement_direction = world::deterministic_wander_direction(
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
    if (!world::has_horizontal_motion(context.goal.state.movement_direction)) {
      return;
    }
    const Vec3f redirected = world::random_clear_wander_direction(
        context.entity_id,
        context.tick_index,
        context.goal_index,
        context.collision_query,
        context.bounds,
        context.blocked_delta,
        context.collision_epsilon);
    if (!world::has_horizontal_motion(redirected)) {
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
