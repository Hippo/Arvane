#include "world_runtime_internal.hpp"

#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/entity_goal_utils.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace arv::world::detail {
namespace {

inline constexpr f32 AirborneIntentBlend = 0.10F;
inline constexpr f32 VehicleStopSpeedBlocksPerSecond = 0.04F;
inline constexpr f32 VehicleStopAngularRadiansPerSecond = 0.01F;
inline constexpr f32 VehicleAnimationSpeedEpsilonBlocksPerSecond = 0.05F;

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

[[nodiscard]] Aabb3f dropped_item_bounds(Vec3f position) noexcept {
  constexpr f32 HalfWidth = 0.125F;
  constexpr f32 Height = 0.25F;
  return {
      {position.x - HalfWidth, position.y, position.z - HalfWidth},
      {position.x + HalfWidth, position.y + Height, position.z + HalfWidth}};
}

struct DroppedItemLiquidProbe {
  bool overlaps_liquid{};
  f32 highest_surface_y{};
};

[[nodiscard]] DroppedItemLiquidProbe probe_dropped_item_liquid(
    const physics::ICollisionQuery& query,
    const Aabb3f& bounds,
    f32 collision_epsilon) {
  const i32 min_x = static_cast<i32>(std::floor(bounds.min.x));
  const i32 min_y = static_cast<i32>(std::floor(bounds.min.y));
  const i32 min_z = static_cast<i32>(std::floor(bounds.min.z));
  const i32 max_x = static_cast<i32>(std::ceil(bounds.max.x));
  const i32 max_y = static_cast<i32>(std::ceil(bounds.max.y));
  const i32 max_z = static_cast<i32>(std::ceil(bounds.max.z));

  DroppedItemLiquidProbe probe;
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
        probe.overlaps_liquid = true;
        probe.highest_surface_y = std::max(probe.highest_surface_y, surface_y);
      }
    }
  }
  return probe;
}

[[nodiscard]] f32 dropped_item_buoyancy(
    const entity::DroppedItem& dropped,
    const player::ItemRegistry* item_registry) noexcept {
  if (item_registry == nullptr) {
    return player::DefaultItemBuoyancy;
  }
  const player::ItemRecord* item = item_registry->item(dropped.stack.item);
  return item == nullptr ? player::DefaultItemBuoyancy : item->buoyancy;
}

[[nodiscard]] bool player_animation_is_sneaking(std::string_view animation_name) noexcept {
  return animation_name == "sneak" ||
         animation_name == "sneak_idle" ||
         animation_name == "sneak_walk";
}

[[nodiscard]] bool player_animation_is_locomotion(std::string_view animation_name) noexcept {
  return animation_name == "idle" ||
         animation_name == "walk" ||
         animation_name == "swim_idle" ||
         animation_name == "swim" ||
         player_animation_is_sneaking(animation_name);
}

[[nodiscard]] bool player_animation_is_attack(std::string_view animation_name) noexcept {
  return animation_name == "hit_left" ||
         animation_name == "hit_right";
}

[[nodiscard]] bool rideable_has_occupant(
    const entity::RideableComponent* rideable) noexcept {
  if (rideable == nullptr) {
    return false;
  }
  if (rideable->controller.value != 0U) {
    return true;
  }
  return std::any_of(
      rideable->seats.begin(),
      rideable->seats.end(),
      [](const entity::Seat& seat) {
        return seat.occupant.value != 0U;
      });
}

[[nodiscard]] f32 vehicle_ground_drag(
    const entity::VehiclePhysicsComponent& physics,
    const entity::VehicleDefinition* definition) noexcept {
  switch (physics.kind) {
    case entity::VehiclePhysicsKind::LivingMount:
      return definition == nullptr
                 ? 0.82F
                 : std::min(std::max(definition->living_mount.friction, 0.0F), 0.82F);
    case entity::VehiclePhysicsKind::SurfaceWaterVehicle:
      if (definition == nullptr) {
        return physics.in_water ? 0.88F : 0.35F;
      }
      return physics.in_water ? definition->surface_water.water_drag
                              : definition->surface_water.land_drag;
    case entity::VehiclePhysicsKind::GroundVehicle:
      return definition == nullptr ? 0.94F : definition->ground.drag;
  }
  return 0.9F;
}

[[nodiscard]] f32 vehicle_gravity_scale(
    const entity::VehiclePhysicsComponent& physics,
    const entity::VehicleDefinition* definition) noexcept {
  if (definition == nullptr) {
    return 1.0F;
  }
  switch (physics.kind) {
    case entity::VehiclePhysicsKind::LivingMount:
      return definition->living_mount.gravity_scale;
    case entity::VehiclePhysicsKind::SurfaceWaterVehicle:
      return physics.in_water ? 0.0F : 1.0F;
    case entity::VehiclePhysicsKind::GroundVehicle:
      return definition->ground.gravity_scale;
  }
  return 1.0F;
}

[[nodiscard]] std::string_view ground_vehicle_motion_animation_name(
    Quatf rotation,
    Vec3f velocity) noexcept {
  const Vec3f visual_forward =
      rotate_vector(normalize_quat(rotation), {0.0F, 0.0F, -1.0F});
  const f32 signed_speed = dot(velocity, visual_forward);
  if (signed_speed > VehicleAnimationSpeedEpsilonBlocksPerSecond) {
    return "driving";
  }
  if (signed_speed < -VehicleAnimationSpeedEpsilonBlocksPerSecond) {
    return "reverse";
  }
  return {};
}

void set_single_entity_animation(
    entt::registry& registry,
    entt::entity handle,
    std::string_view animation_name) {
  entity::EntityAnimationState* animation =
      registry.try_get<entity::EntityAnimationState>(handle);
  if (animation_name.empty()) {
    if (animation != nullptr) {
      animation->active.clear();
    }
    return;
  }

  if (animation == nullptr) {
    animation = &registry.emplace<entity::EntityAnimationState>(handle);
  }
  if (animation->active.size() == 1U && animation->active.front().name == animation_name) {
    return;
  }
  animation->active.clear();
  animation->active.push_back({std::string{animation_name}, 0.0F});
}

void update_unmounted_ground_vehicle_motion_animation(
    entt::registry& registry,
    entt::entity handle,
    const entity::VehiclePhysicsComponent& physics,
    Quatf rotation,
    Vec3f velocity) {
  if (physics.kind != entity::VehiclePhysicsKind::GroundVehicle) {
    return;
  }
  set_single_entity_animation(
      registry,
      handle,
      ground_vehicle_motion_animation_name(rotation, velocity));
}

[[nodiscard]] entity::EntityAnimationPlayback* find_player_locomotion_animation(
    entity::EntityAnimationState& animation) noexcept {
  for (entity::EntityAnimationPlayback& active : animation.active) {
    if (player_animation_is_locomotion(active.name)) {
      return &active;
    }
  }
  return nullptr;
}

void remove_finished_player_attack_animations(
    entity::EntityAnimationState& animation) {
  constexpr Seconds AttackAnimationSeconds = 0.25F;
  std::erase_if(
      animation.active,
      [](const entity::EntityAnimationPlayback& active) {
        return player_animation_is_attack(active.name) &&
               active.time_seconds >= AttackAnimationSeconds;
      });
}

void set_player_locomotion_animation(
    entity::EntityAnimationState& animation,
    std::string_view name) {
  entity::EntityAnimationPlayback* current = find_player_locomotion_animation(animation);
  if (current != nullptr && current->name == name) {
    return;
  }
  std::erase_if(
      animation.active,
      [](const entity::EntityAnimationPlayback& active) {
        return player_animation_is_locomotion(active.name);
      });
  if (!name.empty()) {
    animation.active.insert(animation.active.begin(), {std::string{name}, 0.0F});
  }
}

void update_player_animation_state(
    entity::EntityAnimationState& animation,
    const player::PlayerState& player_state) {
  remove_finished_player_attack_animations(animation);

  const bool moving = has_horizontal_motion(player_state.velocity);
  if (player_state.swimming) {
    set_player_locomotion_animation(animation, moving ? "swim" : "swim_idle");
    return;
  }

  if (player_state.sneaking) {
    entity::EntityAnimationPlayback* locomotion =
        find_player_locomotion_animation(animation);
    if (locomotion == nullptr || !player_animation_is_sneaking(locomotion->name)) {
      set_player_locomotion_animation(animation, "sneak");
      return;
    }
    if (locomotion->name == "sneak" && locomotion->time_seconds < 0.25F) {
      return;
    }
    set_player_locomotion_animation(animation, moving ? "sneak_walk" : "sneak_idle");
    return;
  }

  set_player_locomotion_animation(animation, moving ? "walk" : "idle");
}

[[nodiscard]] bool can_jump_up_one_block(
    const physics::ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f delta,
    f32 collision_epsilon) {
  Vec3f horizontal_delta{delta.x, 0.0F, delta.z};
  if (!has_horizontal_motion(horizontal_delta)) {
    return false;
  }

  constexpr f32 OneBlock = 1.0F;
  const physics::MovementResolution raised = physics::resolve_movement(
      query,
      bounds,
      {0.0F, OneBlock, 0.0F},
      collision_epsilon);
  if (raised.hit_y || raised.applied_delta.y < OneBlock - collision_epsilon * 2.0F) {
    return false;
  }

  const physics::MovementResolution crossed = physics::resolve_movement(
      query,
      raised.resolved_bounds,
      horizontal_delta,
      collision_epsilon);
  if (crossed.hit_x || crossed.hit_z) {
    return false;
  }

  return physics::has_support_below(
      query,
      crossed.resolved_bounds,
      collision_epsilon * 2.0F);
}

} // namespace

u32 WorldEntitySimulation::tick_brains(
    WorldRuntime& world,
    WorldEntityStore& entities,
    const EntityGoalRegistry* goal_registry) {
  if (goal_registry == nullptr) {
    return 0;
  }
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
      if (animation_name.empty()) {
        animation.active.clear();
        return;
      }
      if (animation.active.size() == 1U &&
          animation.active.front().name == animation_name) {
        return;
      }
      animation.active.clear();
      animation.active.push_back({std::string{animation_name}, 0.0F});
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
    const auto movement_intent = [&]() -> entity::MovementIntent& {
      return registry.get_or_emplace<entity::MovementIntent>(handle);
    };
    const auto make_context = [&](entity::GoalSlot& goal, usize index) {
      return EntityGoalContext{
          world,
          runtime.entity_id,
          tick_index,
          transform,
          brain,
          goal,
          index,
          FunctionRef<void(std::string_view)>{set_animation},
          FunctionRef<void()>{clear_movement_intent},
          FunctionRef<entity::MovementIntent&()>{movement_intent},
      };
    };
    const auto goal_can_start = [&](usize index) {
      entity::GoalSlot& goal = brain.goals[index];
      if (goal.state.cooldown_until > tick_index) {
        return false;
      }
      const EntityGoalHandler* handler = goal_registry->find(goal.type);
      if (handler == nullptr) {
        return false;
      }
      EntityGoalContext context = make_context(goal, index);
      return handler->can_start ? handler->can_start(context) : true;
    };
    const auto goal_can_continue = [&](usize index) {
      entity::GoalSlot& goal = brain.goals[index];
      const EntityGoalHandler* handler = goal_registry->find(goal.type);
      if (handler == nullptr) {
        return false;
      }
      EntityGoalContext context = make_context(goal, index);
      return handler->can_continue ? handler->can_continue(context)
                                   : goal.state.remaining_ticks > 0U;
    };
    const auto start_goal = [&](usize index) {
      entity::GoalSlot& goal = brain.goals[index];
      goal.state.remaining_ticks = std::max<u32>(1U, goal.duration_ticks);
      if (brain.blackboard.home_position == Vec3f{}) {
        brain.blackboard.home_position = transform.position;
      }
      goal.state.movement_direction = {};
      if (const EntityGoalHandler* handler = goal_registry->find(goal.type);
          handler != nullptr && handler->start) {
        EntityGoalContext context = make_context(goal, index);
        handler->start(context);
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
    const auto tick_goal = [&](usize index) {
      entity::GoalSlot& goal = brain.goals[index];
      if (goal.state.remaining_ticks > 0U) {
        --goal.state.remaining_ticks;
      }
      const EntityGoalHandler* handler = goal_registry->find(goal.type);
      if (handler != nullptr && handler->tick) {
        EntityGoalContext context = make_context(goal, index);
        handler->tick(context);
      } else {
        clear_movement_intent();
      }
      set_animation(goal.animation);
    };

    if (brain.active_goal.has_value()) {
      const usize active = brain.active_goal.value();
      if (active >= brain.goals.size() || !goal_can_continue(active)) {
        if (active < brain.goals.size()) {
          stop_goal(active, entity::GoalStopReason::Completed);
        } else {
          brain.active_goal = std::nullopt;
        }
      }
    }

    std::optional<usize> candidate;
    for (usize index = 0; index < brain.goals.size(); ++index) {
      if (goal_can_start(index)) {
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
      tick_goal(brain.active_goal.value());
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
    const EntityGoalRegistry* goal_registry,
    const entity::VehicleDefinitionRegistry* vehicle_registry,
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
        transform.rotation =
            yaw_rotation(model_yaw_from_horizontal_direction(velocity.linear));
      }
      entities.update_chunk(handle);
      ++updated;
    }
    return updated;
  }

  const WorldCollisionQuery collision_query(world, *block_registry, true);
  const physics::KinematicConfig physics_config;
  const auto view = registry.view<
      entity::RuntimeEntity,
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::EntityCollision>();
  for (const entt::entity handle : view) {
    if (registry.all_of<entity::PlayerEntity>(handle)) {
      continue;
    }
    const auto& runtime = view.get<entity::RuntimeEntity>(handle);
    auto& transform = view.get<entity::EntityTransform>(handle);
    auto& velocity = view.get<entity::EntityVelocity>(handle);
    const auto& collision = view.get<entity::EntityCollision>(handle);
    entity::VehiclePhysicsComponent* vehicle_physics =
        registry.try_get<entity::VehiclePhysicsComponent>(handle);
    if (vehicle_physics != nullptr) {
      const entity::RideableComponent* rideable =
          registry.try_get<entity::RideableComponent>(handle);
      if (rideable_has_occupant(rideable)) {
        continue;
      }

      const entity::VehicleDefinition* definition =
          vehicle_registry == nullptr
              ? nullptr
              : vehicle_registry->find(vehicle_physics->definition);
      const Aabb3f bounds = entity_body_bounds(transform.position, collision.box);
      const bool supported = physics::has_support_below(
          collision_query,
          bounds,
          physics_config.collision_epsilon * 2.0F);
      const bool grounded =
          supported && velocity.linear.y <= physics_config.collision_epsilon;
      vehicle_physics->grounded = grounded;
      if (grounded) {
        vehicle_physics->ground_contact_time += static_cast<f32>(tick_seconds);
        vehicle_physics->air_time = 0.0F;
        if (velocity.linear.y < 0.0F) {
          velocity.linear.y = 0.0F;
        }
      } else {
        vehicle_physics->air_time += static_cast<f32>(tick_seconds);
        vehicle_physics->ground_contact_time = 0.0F;
        velocity.linear.y = std::max(
            velocity.linear.y -
                physics_config.gravity_per_second *
                    std::max(0.0F, vehicle_gravity_scale(*vehicle_physics, definition)) *
                    static_cast<f32>(tick_seconds),
            -physics_config.terminal_velocity);
      }

      const f32 authored_drag =
          std::clamp(vehicle_ground_drag(*vehicle_physics, definition), 0.0F, 1.0F);
      const f32 per_second_ticks =
          static_cast<f32>(tick_seconds) * static_cast<f32>(DefaultBaseTps);
      const f32 drag =
          grounded ? authored_drag : std::max(authored_drag, 0.99F);
      const f32 drag_factor = std::pow(drag, std::max(0.0F, per_second_ticks));
      velocity.linear.x *= drag_factor;
      velocity.linear.z *= drag_factor;
      if (horizontal_length(velocity.linear) < VehicleStopSpeedBlocksPerSecond) {
        velocity.linear.x = 0.0F;
        velocity.linear.z = 0.0F;
      }
      vehicle_physics->angular_velocity =
          vehicle_physics->angular_velocity * drag_factor;
      if (length(vehicle_physics->angular_velocity) <
          VehicleStopAngularRadiansPerSecond) {
        vehicle_physics->angular_velocity = {};
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
      vehicle_physics->linear_velocity = velocity.linear;
      vehicle_physics->grounded = resolved.grounded;
      update_unmounted_ground_vehicle_motion_animation(
          registry,
          handle,
          *vehicle_physics,
          transform.rotation,
          velocity.linear);
      entities.update_chunk(handle);
      ++updated;
      continue;
    }
    entity::MovementIntent* intent = registry.try_get<entity::MovementIntent>(handle);
    const entity::EntityMovement* movement = registry.try_get<entity::EntityMovement>(handle);

    const Aabb3f bounds = entity_body_bounds(transform.position, collision.box);
    const bool has_ground_support = physics::has_support_below(
        collision_query,
        bounds,
        physics_config.collision_epsilon * 2.0F);
    const bool grounded =
        has_ground_support && velocity.linear.y <= physics_config.collision_epsilon;

    if (intent != nullptr && movement != nullptr) {
      const Vec3f direction = normalize_or_zero(intent->desired_direction);
      const f32 speed = std::max(0.0F, movement->walk_speed) *
                        std::max(0.0F, intent->speed_multiplier);
      if (grounded) {
        velocity.linear.x = direction.x * speed;
        velocity.linear.z = direction.z * speed;
      } else {
        velocity.linear.x =
            velocity.linear.x * (1.0F - AirborneIntentBlend) +
            direction.x * speed * AirborneIntentBlend;
        velocity.linear.z =
            velocity.linear.z * (1.0F - AirborneIntentBlend) +
            direction.z * speed * AirborneIntentBlend;
      }
      if (direction.y > 0.0F) {
        velocity.linear.y = std::max(velocity.linear.y, direction.y * speed);
      }
    }

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
    if (intent != nullptr && movement != nullptr && grounded &&
        (resolved.hit_x || resolved.hit_z)) {
      if (can_jump_up_one_block(
              collision_query,
              bounds,
              movement_delta,
              physics_config.collision_epsilon)) {
        velocity.linear.y = std::max(velocity.linear.y, physics_config.jump_velocity);
      } else if (auto* brain = registry.try_get<entity::BrainComponent>(handle);
                 brain != nullptr) {
        if (goal_registry != nullptr &&
            brain->active_goal.has_value() &&
            brain->active_goal.value() < brain->goals.size()) {
          const usize active = brain->active_goal.value();
          entity::GoalSlot& goal = brain->goals[active];
          if (const EntityGoalHandler* handler = goal_registry->find(goal.type);
              handler != nullptr && handler->blocked) {
            EntityGoalBlockedContext context{
                world,
                runtime.entity_id,
                world.tick_index(),
                collision_query,
                bounds,
                *brain,
                goal,
                active,
                intent,
                movement_delta,
                physics_config.collision_epsilon};
            handler->blocked(context);
          }
        }
      }
    }

    const Vec3f facing_direction =
        has_horizontal_motion(resolved.applied_delta) ? resolved.applied_delta : velocity.linear;
    if (has_horizontal_motion(facing_direction)) {
      transform.rotation = yaw_rotation(model_yaw_from_horizontal_direction(facing_direction));
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
    std::erase_if(
        animation.active,
        [](const entity::EntityAnimationPlayback& active) {
          return active.name.empty();
        });
    if (animation.active.empty()) {
      continue;
    }
    for (entity::EntityAnimationPlayback& active : animation.active) {
      active.time_seconds += tick_seconds;
    }
    ++updated;
  }
  return updated;
}

u32 WorldEntitySimulation::tick_entities(
    WorldRuntime& world,
    WorldEntityStore& entities,
    WorldRegionRuntime& regions,
    const voxel::BlockRegistry* block_registry,
    const EntityGoalRegistry* goal_registry,
    const entity::VehicleDefinitionRegistry* vehicle_registry) {
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
    update_player_animation_state(
        registry.get_or_emplace<entity::EntityAnimationState>(handle),
        player_state);
    entities.update_chunk(handle);
    ++updated;
  }

  updated += tick_brains(world, entities, goal_registry);
  updated += tick_entity_physics(
      world,
      entities,
      block_registry,
      goal_registry,
      vehicle_registry,
      tick_seconds);
  updated += tick_entity_animations(entities, tick_seconds);

  const auto transform_view = registry.view<entity::EntityTransform, entity::ChunkRelevance>();
  for (const entt::entity handle : transform_view) {
    entities.update_chunk(handle);
  }
  return updated;
}

u32 WorldEntitySimulation::tick_dropped_items(
    WorldRuntime& world,
    WorldEntityStore& entities,
    Seconds tick_seconds,
    const voxel::BlockRegistry* block_registry,
    const player::ItemRegistry* item_registry) {
  u32 updated = 0;
  if (tick_seconds <= 0.0F) {
    return updated;
  }

  entt::registry& registry = entities.registry();
  const auto view = registry.view<
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::DroppedItem>();
  const physics::KinematicConfig physics_config;
  const std::optional<WorldCollisionQuery> collision_query =
      block_registry == nullptr
          ? std::optional<WorldCollisionQuery>{}
          : std::optional<WorldCollisionQuery>{std::in_place, world, *block_registry, true};
  for (const entt::entity handle : view) {
    auto& transform = view.get<entity::EntityTransform>(handle);
    auto& velocity = view.get<entity::EntityVelocity>(handle);
    auto& dropped = view.get<entity::DroppedItem>(handle);
    bool changed = false;
    if (dropped.pickup_delay_seconds > 0.0F) {
      dropped.pickup_delay_seconds =
          std::max<Seconds>(0.0F, dropped.pickup_delay_seconds - tick_seconds);
      changed = true;
    }

    if (collision_query.has_value()) {
      const Aabb3f bounds = dropped_item_bounds(transform.position);
      const DroppedItemLiquidProbe liquid = probe_dropped_item_liquid(
          collision_query.value(),
          bounds,
          physics_config.collision_epsilon);

      if (liquid.overlaps_liquid) {
        constexpr f32 LiquidDrag = 0.8F;
        constexpr f32 SurfaceFloatDepth = 0.08F;
        velocity.linear.x *= LiquidDrag;
        velocity.linear.z *= LiquidDrag;

        const f32 buoyancy = dropped_item_buoyancy(dropped, item_registry);
        if (buoyancy > 0.0F) {
          const f32 target_y = liquid.highest_surface_y - SurfaceFloatDepth;
          if (transform.position.y < target_y - physics_config.collision_epsilon) {
            velocity.linear.y = std::min(
                std::max(velocity.linear.y, buoyancy),
                physics_config.terminal_velocity);
          } else if (velocity.linear.y > 0.0F) {
            velocity.linear.y = 0.0F;
          }
        } else if (buoyancy < 0.0F) {
          velocity.linear.y = std::max(
              std::min(velocity.linear.y, buoyancy),
              -physics_config.terminal_velocity);
        } else {
          velocity.linear.y = 0.0F;
        }
      } else {
        const bool grounded = physics::has_support_below(
            collision_query.value(),
            bounds,
            physics_config.collision_epsilon * 2.0F);
        if (!grounded) {
          velocity.linear.y = std::max(
              velocity.linear.y - physics_config.gravity_per_second * tick_seconds,
              -physics_config.terminal_velocity);
        } else if (velocity.linear.y < 0.0F) {
          velocity.linear.y = 0.0F;
        }
      }

      const Vec3f movement_delta = velocity.linear * tick_seconds;
      const physics::MovementResolution resolved = physics::resolve_movement(
          collision_query.value(),
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
      entities.update_chunk(handle);
      changed = true;
    }

    if (changed) {
      ++updated;
    }
  }
  return updated;
}

} // namespace arv::world::detail

namespace arv::world {

u32 WorldRuntime::tick_entities(
    const voxel::BlockRegistry* block_registry,
    const EntityGoalRegistry* goal_registry,
    const entity::VehicleDefinitionRegistry* vehicle_registry) {
  assert_owner_thread();
  return impl_->entity_simulation.tick_entities(
      *this,
      impl_->entities,
      impl_->regions,
      block_registry,
      goal_registry,
      vehicle_registry);
}

u32 WorldRuntime::tick_dropped_items(
    Seconds tick_seconds,
    const voxel::BlockRegistry* block_registry,
    const player::ItemRegistry* item_registry) {
  assert_owner_thread();
  return impl_->entity_simulation.tick_dropped_items(
      *this,
      impl_->entities,
      tick_seconds,
      block_registry,
      item_registry);
}

} // namespace arv::world
