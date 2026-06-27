#include "world_runtime_internal.hpp"

#include <algorithm>
#include <utility>

namespace arv::world::detail {

bool authored_vehicle_components_resolved(
    const entity::EntityComponentSet& components,
    const entity::VehicleDefinitionRegistry* vehicle_definitions) noexcept {
  if (!components.rideable.has_value() && !components.vehicle_physics.has_value()) {
    return true;
  }
  if (vehicle_definitions == nullptr) {
    return false;
  }
  if (components.rideable.has_value() &&
      vehicle_definitions->find(components.rideable->definition) == nullptr) {
    return false;
  }
  if (components.vehicle_physics.has_value() &&
      vehicle_definitions->find(components.vehicle_physics->definition) == nullptr) {
    return false;
  }
  return true;
}

void apply_authored_components(
    entt::registry& registry,
    entt::entity handle,
    const entity::EntityComponentSet& components,
    const entity::VehicleDefinitionRegistry* vehicle_definitions) {
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
      goal.damage = source.damage;
      goal.memory_ticks = source.memory_ticks;
      goal.require_line_of_sight = source.require_line_of_sight;
      goal.target = source.target;
      goal.animation = source.animation;
      goal.attack_animation = source.attack_animation;
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
  if (components.rideable.has_value()) {
    const entity::VehicleDefinition* definition =
        vehicle_definitions == nullptr
            ? nullptr
            : vehicle_definitions->find(components.rideable->definition);
    if (definition != nullptr) {
      entity::RideableComponent rideable;
      rideable.definition = definition->name;
      rideable.seats.reserve(definition->seats.size());
      for (const entity::VehicleSeatDefinition& source : definition->seats) {
        rideable.seats.push_back({source.id,
                                  source.name.empty() ? source.id : source.name,
                                  source.role,
                                  source.local_position,
                                  source.local_rotation,
                                  {},
                                  source.controls_vehicle,
                                  source.camera_mode});
      }
      registry.emplace_or_replace<entity::RideableComponent>(
          handle,
          std::move(rideable));
    }
  }
  if (components.vehicle_physics.has_value()) {
    const entity::VehicleDefinition* definition =
        vehicle_definitions == nullptr
            ? nullptr
            : vehicle_definitions->find(components.vehicle_physics->definition);
    if (definition != nullptr) {
      entity::VehiclePhysicsComponent physics;
      physics.kind = definition->physics;
      physics.definition = definition->name;
      if (definition->fuel.has_value()) {
        physics.fuel_item = definition->fuel->item;
        physics.fuel_seconds = definition->fuel->initial_seconds;
        physics.fuel_capacity_seconds = definition->fuel->capacity_seconds;
        physics.fuel_burn_per_second = definition->fuel->burn_per_second;
      }
      registry.emplace_or_replace<entity::VehiclePhysicsComponent>(
          handle,
          std::move(physics));
    }
  }
}

} // namespace arv::world::detail
