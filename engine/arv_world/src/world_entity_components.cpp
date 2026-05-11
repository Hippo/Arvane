#include "world_runtime_internal.hpp"

#include <algorithm>
#include <utility>

namespace arv::world::detail {

void apply_authored_components(
    entt::registry& registry,
    entt::entity handle,
    const entity::EntityComponentSet& components) {
  if (handle == entt::null || !registry.valid(handle)) {
    return;
  }

  if (components.collision.has_value()) {
    registry.emplace_or_replace<entity::EntityCollision>(
        handle,
        components.collision->box);
  }
  if (components.movement.has_value()) {
    registry.emplace_or_replace<entity::EntityMovement>(
        handle,
        components.movement->walk_speed);
  }
  if (components.health.has_value()) {
    const f32 max_health = std::max(0.0F, components.health->max);
    const f32 current_health = std::clamp(
        components.health->current.value_or(max_health),
        0.0F,
        max_health);
    registry.emplace_or_replace<entity::EntityHealth>(
        handle,
        max_health,
        current_health,
        components.health->invulnerable);
  }
  if (components.knockback_resistance.has_value()) {
    registry.emplace_or_replace<entity::KnockbackResistanceComponent>(
        handle,
        std::clamp(components.knockback_resistance->resistance, 0.0F, 1.0F));
  }
  if (components.animation.has_value()) {
    entity::EntityAnimationState animation;
    if (!components.animation->initial.empty()) {
      animation.active.push_back({components.animation->initial, 0.0F});
    }
    registry.emplace_or_replace<entity::EntityAnimationState>(
        handle,
        std::move(animation));
  }
  if (components.brain.has_value()) {
    entity::BrainComponent brain;
    brain.behavior = components.brain->behavior;
    brain.tick_interval = std::max<u32>(1U, components.brain->tick_interval);
    brain.goals.reserve(components.brain->goals.size());
    for (const entity::AuthoredGoalDefinition& source : components.brain->goals) {
      entity::GoalSlot goal;
      goal.type = source.type;
      goal.priority = source.priority;
      goal.radius = source.radius;
      goal.speed_multiplier = source.speed_multiplier;
      goal.duration_ticks = std::max<u32>(1U, source.duration_ticks);
      goal.cooldown_ticks = source.cooldown_ticks;
      goal.range = source.range;
      goal.target = source.target;
      goal.animation = source.animation;
      brain.goals.push_back(std::move(goal));
    }
    std::stable_sort(
        brain.goals.begin(),
        brain.goals.end(),
        [](const entity::GoalSlot& lhs, const entity::GoalSlot& rhs) {
          return lhs.priority < rhs.priority;
        });
    if (!brain.goals.empty()) {
      registry.emplace_or_replace<entity::BrainComponent>(handle, std::move(brain));
      (void)registry.get_or_emplace<entity::EntityAnimationState>(handle);
    }
  }
}

} // namespace arv::world::detail
