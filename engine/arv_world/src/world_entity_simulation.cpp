#include "world_runtime_internal.hpp"

#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace arv::world::detail {
namespace {

[[nodiscard]] bool is_idle_goal(std::string_view type) noexcept {
  return type == "arvane:idle";
}

[[nodiscard]] bool is_wander_goal(std::string_view type) noexcept {
  return type == "arvane:wander";
}

[[nodiscard]] bool is_float_in_fluid_goal(std::string_view type) noexcept {
  return type == "arvane:float_in_fluid";
}

[[nodiscard]] f32 positive_fraction(f32 value) noexcept {
  const f32 whole = std::floor(value);
  return value - whole;
}

[[nodiscard]] Vec3f deterministic_wander_direction(
    EntityID entity_id,
    TickIndex tick,
    usize goal_index) noexcept {
  const f32 seed =
      static_cast<f32>(entity_id.value * 37U + static_cast<u32>(goal_index) * 101U) +
      static_cast<f32>(tick % 100000U) * 0.173F;
  const f32 angle = positive_fraction(std::sin(seed) * 43758.5453F) * 6.28318530718F;
  return {std::cos(angle), 0.0F, std::sin(angle)};
}

[[nodiscard]] Aabb3f entity_body_bounds(Vec3f position, Vec3f box) noexcept {
  const Vec3f size{
      std::max(0.0F, box.x),
      std::max(0.0F, box.y),
      std::max(0.0F, box.z)};
  const f32 half_x = size.x * 0.5F;
  const f32 half_z = size.z * 0.5F;
  return {
      {position.x - half_x, position.y, position.z - half_z},
      {position.x + half_x, position.y + size.y, position.z + half_z}};
}

[[nodiscard]] Quatf yaw_rotation(f32 radians) noexcept {
  const f32 half = radians * 0.5F;
  return {0.0F, std::sin(half), 0.0F, std::cos(half)};
}

[[nodiscard]] bool has_horizontal_motion(Vec3f value) noexcept {
  return value.x * value.x + value.z * value.z > 0.000001F;
}

[[nodiscard]] f32 yaw_from_horizontal_direction(Vec3f direction) noexcept {
  constexpr f32 ModelForwardYawOffset = 3.14159265358979323846F;
  return std::atan2(direction.x, direction.z) + ModelForwardYawOffset;
}

} // namespace

u32 WorldEntitySimulation::tick_brains(
    WorldRuntime& world,
    WorldEntityStore& entities) {
  u32 updated = 0;
  entt::registry& registry = entities.registry();
  const TickIndex tick_index = world.tick_index();
  const auto view = registry.view<
      entity::RuntimeEntity,
      entity::EntityTransform,
      entity::BrainComponent>();
  for (const entt::entity handle : view) {
    const auto& runtime = view.get<entity::RuntimeEntity>(handle);
    const auto& transform = view.get<entity::EntityTransform>(handle);
    auto& brain = view.get<entity::BrainComponent>(handle);
    if (brain.goals.empty() || tick_index < brain.next_tick) {
      continue;
    }
    brain.next_tick = tick_index + std::max<u32>(1U, brain.tick_interval);

    const auto set_animation = [&](std::string_view animation_name) {
      auto& animation = registry.get_or_emplace<entity::EntityAnimationState>(handle);
      if (animation.name != animation_name) {
        animation.name = std::string{animation_name};
        animation.time_seconds = 0.0F;
      }
    };
    const auto clear_movement_intent = [&]() {
      if (auto* intent = registry.try_get<entity::MovementIntent>(handle);
          intent != nullptr) {
        intent->desired_direction = {};
        intent->speed_multiplier = 1.0F;
        intent->jump = false;
        intent->sprint = false;
      }
    };
    const auto goal_can_start = [&](const entity::GoalSlot& goal) {
      if (goal.state.cooldown_until > tick_index) {
        return false;
      }
      if (is_float_in_fluid_goal(goal.type)) {
        const BlockPos pos{{
            static_cast<i32>(std::floor(transform.position.x)),
            static_cast<i32>(std::floor(transform.position.y)),
            static_cast<i32>(std::floor(transform.position.z)),
        }};
        return !world.get_fluid(pos).empty();
      }
      return is_idle_goal(goal.type) || is_wander_goal(goal.type);
    };
    const auto goal_can_continue = [&](const entity::GoalSlot& goal) {
      if (is_float_in_fluid_goal(goal.type)) {
        const BlockPos pos{{
            static_cast<i32>(std::floor(transform.position.x)),
            static_cast<i32>(std::floor(transform.position.y)),
            static_cast<i32>(std::floor(transform.position.z)),
        }};
        return !world.get_fluid(pos).empty();
      }
      return goal.state.remaining_ticks > 0U;
    };
    const auto start_goal = [&](usize index) {
      entity::GoalSlot& goal = brain.goals[index];
      goal.state.remaining_ticks = std::max<u32>(1U, goal.duration_ticks);
      if (brain.blackboard.home_position == Vec3f{}) {
        brain.blackboard.home_position = transform.position;
      }
      if (is_wander_goal(goal.type)) {
        goal.state.movement_direction =
            deterministic_wander_direction(runtime.entity_id, tick_index, index);
      } else if (is_float_in_fluid_goal(goal.type)) {
        goal.state.movement_direction = {0.0F, 1.0F, 0.0F};
      } else {
        goal.state.movement_direction = {};
      }
      set_animation(goal.animation);
      brain.active_goal = index;
    };
    const auto stop_goal = [&](usize index, entity::GoalStopReason reason) {
      entity::GoalSlot& goal = brain.goals[index];
      clear_movement_intent();
      if (reason == entity::GoalStopReason::Completed && goal.cooldown_ticks > 0U) {
        goal.state.cooldown_until = tick_index + goal.cooldown_ticks;
      }
      goal.state.remaining_ticks = 0U;
      if (brain.active_goal.has_value() && brain.active_goal.value() == index) {
        brain.active_goal = std::nullopt;
      }
    };
    const auto tick_goal = [&](entity::GoalSlot& goal) {
      if (goal.state.remaining_ticks > 0U) {
        --goal.state.remaining_ticks;
      }
      if (is_wander_goal(goal.type) || is_float_in_fluid_goal(goal.type)) {
        auto& intent = registry.get_or_emplace<entity::MovementIntent>(handle);
        intent.desired_direction = goal.state.movement_direction;
        intent.speed_multiplier = std::max(0.0F, goal.speed_multiplier);
        intent.jump = is_float_in_fluid_goal(goal.type);
        intent.sprint = false;
      } else {
        clear_movement_intent();
      }
      set_animation(goal.animation);
    };

    if (brain.active_goal.has_value()) {
      const usize active = brain.active_goal.value();
      if (active >= brain.goals.size() || !goal_can_continue(brain.goals[active])) {
        if (active < brain.goals.size()) {
          stop_goal(active, entity::GoalStopReason::Completed);
        } else {
          brain.active_goal = std::nullopt;
        }
      }
    }

    std::optional<usize> candidate;
    for (usize index = 0; index < brain.goals.size(); ++index) {
      if (goal_can_start(brain.goals[index])) {
        candidate = index;
        break;
      }
    }
    if (!brain.active_goal.has_value() && candidate.has_value()) {
      start_goal(candidate.value());
    } else if (brain.active_goal.has_value() && candidate.has_value()) {
      const usize active = brain.active_goal.value();
      if (brain.goals[candidate.value()].priority < brain.goals[active].priority) {
        stop_goal(active, entity::GoalStopReason::Interrupted);
        start_goal(candidate.value());
      }
    }

    if (brain.active_goal.has_value()) {
      tick_goal(brain.goals[brain.active_goal.value()]);
    } else {
      clear_movement_intent();
      set_animation({});
    }
    ++updated;
  }
  return updated;
}

u32 WorldEntitySimulation::tick_entity_physics(
    WorldRuntime& world,
    WorldEntityStore& entities,
    const voxel::BlockRegistry* block_registry,
    Seconds tick_seconds) {
  if (tick_seconds <= 0.0F) {
    return 0;
  }

  entt::registry& registry = entities.registry();
  u32 updated = 0;
  if (block_registry == nullptr) {
    const auto view = registry.view<
        entity::EntityTransform,
        entity::EntityVelocity,
        entity::EntityMovement,
        entity::MovementIntent>();
    for (const entt::entity handle : view) {
      if (registry.all_of<entity::PlayerEntity>(handle)) {
        continue;
      }
      auto& transform = view.get<entity::EntityTransform>(handle);
      auto& velocity = view.get<entity::EntityVelocity>(handle);
      const auto& movement = view.get<entity::EntityMovement>(handle);
      const auto& intent = view.get<entity::MovementIntent>(handle);
      const Vec3f direction = normalize_or_zero(intent.desired_direction);
      const f32 speed = std::max(0.0F, movement.walk_speed) *
                        std::max(0.0F, intent.speed_multiplier);
      velocity.linear = direction * speed;
      if (length(velocity.linear) <= 0.0F) {
        continue;
      }
      transform.previous_position = transform.position;
      transform.previous_rotation = transform.rotation;
      transform.position = transform.position + velocity.linear * tick_seconds;
      if (has_horizontal_motion(velocity.linear)) {
        transform.rotation = yaw_rotation(yaw_from_horizontal_direction(velocity.linear));
      }
      entities.update_chunk(handle);
      ++updated;
    }
    return updated;
  }

  const WorldCollisionQuery collision_query(world, *block_registry, true);
  const physics::KinematicConfig physics_config;
  const auto view = registry.view<
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::EntityCollision>();
  for (const entt::entity handle : view) {
    if (registry.all_of<entity::PlayerEntity>(handle)) {
      continue;
    }
    auto& transform = view.get<entity::EntityTransform>(handle);
    auto& velocity = view.get<entity::EntityVelocity>(handle);
    const auto& collision = view.get<entity::EntityCollision>(handle);
    const entity::MovementIntent* intent = registry.try_get<entity::MovementIntent>(handle);
    const entity::EntityMovement* movement = registry.try_get<entity::EntityMovement>(handle);

    if (intent != nullptr && movement != nullptr) {
      const Vec3f direction = normalize_or_zero(intent->desired_direction);
      const f32 speed = std::max(0.0F, movement->walk_speed) *
                        std::max(0.0F, intent->speed_multiplier);
      velocity.linear.x = direction.x * speed;
      velocity.linear.z = direction.z * speed;
      if (direction.y > 0.0F) {
        velocity.linear.y = std::max(velocity.linear.y, direction.y * speed);
      }
    }

    const Aabb3f bounds = entity_body_bounds(transform.position, collision.box);
    const bool grounded = physics::has_support_below(
        collision_query,
        bounds,
        physics_config.collision_epsilon * 2.0F);
    if (!grounded) {
      velocity.linear.y = std::max(
          velocity.linear.y - physics_config.gravity_per_second * tick_seconds,
          -physics_config.terminal_velocity);
    } else if (velocity.linear.y < 0.0F) {
      velocity.linear.y = 0.0F;
    }

    const Vec3f movement_delta = velocity.linear * tick_seconds;
    const physics::MovementResolution resolved = physics::resolve_movement(
        collision_query,
        bounds,
        movement_delta,
        physics_config.collision_epsilon);

    transform.previous_position = transform.position;
    transform.previous_rotation = transform.rotation;
    transform.position = transform.position + resolved.applied_delta;
    if (resolved.hit_x) {
      velocity.linear.x = 0.0F;
    }
    if (resolved.hit_y) {
      velocity.linear.y = 0.0F;
    }
    if (resolved.hit_z) {
      velocity.linear.z = 0.0F;
    }

    const Vec3f facing_direction =
        has_horizontal_motion(resolved.applied_delta) ? resolved.applied_delta : velocity.linear;
    if (has_horizontal_motion(facing_direction)) {
      transform.rotation = yaw_rotation(yaw_from_horizontal_direction(facing_direction));
    }

    entities.update_chunk(handle);
    ++updated;
  }
  return updated;
}

u32 WorldEntitySimulation::tick_entity_animations(
    WorldEntityStore& entities,
    Seconds tick_seconds) {
  if (tick_seconds <= 0.0F) {
    return 0;
  }

  u32 updated = 0;
  entt::registry& registry = entities.registry();
  const auto view = registry.view<entity::EntityAnimationState>();
  for (const entt::entity handle : view) {
    auto& animation = view.get<entity::EntityAnimationState>(handle);
    if (animation.name.empty()) {
      animation.time_seconds = 0.0F;
      continue;
    }
    animation.time_seconds += tick_seconds;
    ++updated;
  }
  return updated;
}

u32 WorldEntitySimulation::tick_entities(
    WorldRuntime& world,
    WorldEntityStore& entities,
    WorldRegionRuntime& regions,
    const voxel::BlockRegistry* block_registry) {
  entities.flush_despawns(regions);
  u32 updated = 0;
  const Seconds tick_seconds = tick_duration_seconds(DefaultBaseTps);
  entt::registry& registry = entities.registry();
  const auto player_view = registry.view<
      entity::RuntimeEntity,
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::PlayerEntity,
      player::PlayerState>();
  for (const entt::entity handle : player_view) {
    auto& transform = player_view.get<entity::EntityTransform>(handle);
    auto& velocity = player_view.get<entity::EntityVelocity>(handle);
    const auto& player_state = player_view.get<player::PlayerState>(handle);
    if (transform.position != player_state.position) {
      transform.previous_position = transform.position;
      transform.position = player_state.position;
    }
    velocity.linear = player_state.velocity;
    entities.update_chunk(handle);
    ++updated;
  }

  updated += tick_brains(world, entities);
  updated += tick_entity_physics(world, entities, block_registry, tick_seconds);
  updated += tick_entity_animations(entities, tick_seconds);

  const auto transform_view = registry.view<entity::EntityTransform, entity::ChunkRelevance>();
  for (const entt::entity handle : transform_view) {
    entities.update_chunk(handle);
  }
  return updated;
}

u32 WorldEntitySimulation::tick_dropped_items(
    WorldEntityStore& entities,
    Seconds tick_seconds) {
  u32 updated = 0;
  if (tick_seconds <= 0.0F) {
    return updated;
  }

  entt::registry& registry = entities.registry();
  const auto view = registry.view<entity::DroppedItem>();
  for (const entt::entity handle : view) {
    auto& dropped = view.get<entity::DroppedItem>(handle);
    if (dropped.pickup_delay_seconds <= 0.0F) {
      continue;
    }
    dropped.pickup_delay_seconds =
        std::max<Seconds>(0.0F, dropped.pickup_delay_seconds - tick_seconds);
    ++updated;
  }
  return updated;
}

} // namespace arv::world::detail

namespace arv::world {

u32 WorldRuntime::tick_entities(const voxel::BlockRegistry* block_registry) {
  return impl_->entity_simulation.tick_entities(
      *this,
      impl_->entities,
      impl_->regions,
      block_registry);
}

u32 WorldRuntime::tick_dropped_items(Seconds tick_seconds) {
  return impl_->entity_simulation.tick_dropped_items(impl_->entities, tick_seconds);
}

} // namespace arv::world
