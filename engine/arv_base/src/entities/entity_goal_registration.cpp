#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/physics/collision_resolution.hpp"
#include "arv/world/entity_goal_utils.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/world_queries.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <optional>

namespace arv::base {
namespace {

inline constexpr u32 DefaultTargetMemoryTicks = 40U;
inline constexpr u32 DefaultAttackAnimationTicks = 8U;
inline constexpr f32 DefaultTargetRadius = 16.0F;
inline constexpr f32 DefaultAttackRange = 1.8F;
inline constexpr f32 DefaultReturnHomeRadius = 12.0F;
inline constexpr f32 DefaultAvoidFluidRadius = 3.0F;
inline constexpr f32 FluidAvoidProbeDistance = 1.2F;
inline constexpr f32 FluidEscapeProbeDistance = 1.5F;
inline constexpr f32 SwimFallbackHorizontalScale = 0.35F;

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

[[nodiscard]] Vec3f entity_collision_box(
    world::WorldRuntime& world,
    EntityID entity_id) noexcept {
  const entity::EntityCollision* collision = world.find_entity_collision(entity_id);
  return collision == nullptr ? Vec3f{0.8F, 1.8F, 0.8F} : collision->box;
}

[[nodiscard]] Vec3f bounds_center(Aabb3f bounds) noexcept {
  return {
      (bounds.min.x + bounds.max.x) * 0.5F,
      (bounds.min.y + bounds.max.y) * 0.5F,
      (bounds.min.z + bounds.max.z) * 0.5F};
}

[[nodiscard]] Vec3f horizontal_unit(Vec3f value) noexcept {
  return normalize_or_zero({value.x, 0.0F, value.z});
}

[[nodiscard]] Vec3f entity_center(
    world::WorldRuntime& world,
    EntityID entity_id,
    Vec3f fallback) noexcept {
  const entity::EntityTransform* transform = world.find_entity_transform(entity_id);
  if (transform == nullptr) {
    return fallback;
  }
  const Vec3f box = entity_collision_box(world, entity_id);
  return transform->position + Vec3f{0.0F, std::max(0.0F, box.y) * 0.5F, 0.0F};
}

[[nodiscard]] bool entity_alive(
    world::WorldRuntime& world,
    EntityID entity_id) noexcept {
  if (!world.entity_exists(entity_id)) {
    return false;
  }
  const entity::EntityHealth* health = world.find_entity_health(entity_id);
  return health == nullptr || health->current > 0.0F;
}

[[nodiscard]] bool target_has_line_of_sight(
    const world::EntityGoalContext& context,
    EntityID target) noexcept {
  if (!context.goal.require_line_of_sight) {
    return true;
  }
  if (context.block_registry == nullptr) {
    return false;
  }
  const Vec3f origin = entity_center(
      context.world,
      context.entity_id,
      context.transform.position);
  const Vec3f target_center = entity_center(
      context.world,
      target,
      context.transform.position);
  return world::world_has_line_of_sight(
      context.world,
      *context.block_registry,
      origin,
      target_center);
}

[[nodiscard]] bool blackboard_target_valid(
    world::EntityGoalContext& context,
    bool require_line_of_sight = true) {
  if (!context.brain.blackboard.target_entity.has_value()) {
    return false;
  }
  const EntityID target = context.brain.blackboard.target_entity.value();
  if (!entity_alive(context.world, target) ||
      context.world.find_entity_transform(target) == nullptr) {
    context.brain.blackboard.target_entity.reset();
    return false;
  }
  if (require_line_of_sight && !target_has_line_of_sight(context, target)) {
    return false;
  }
  return true;
}

[[nodiscard]] f32 configured_radius(const entity::GoalSlot& goal) noexcept {
  return goal.radius > 0.0F ? goal.radius : DefaultTargetRadius;
}

[[nodiscard]] f32 configured_range(const entity::GoalSlot& goal, f32 fallback) noexcept {
  return goal.range > 0.0F ? goal.range : fallback;
}

[[nodiscard]] std::optional<EntityID> find_nearest_player_target(
    world::EntityGoalContext& context) {
  if (!context.goal.target.empty() && context.goal.target != "nearest_player") {
    return std::nullopt;
  }

  std::optional<EntityID> best;
  f32 best_distance_sq = configured_radius(context.goal) * configured_radius(context.goal);
  for (const PlayerID player_id : context.world.player_ids()) {
    const std::optional<EntityID> entity_id = context.world.entity_for_player(player_id);
    if (!entity_id.has_value() || !entity_alive(context.world, entity_id.value())) {
      continue;
    }
    const entity::EntityTransform* transform =
        context.world.find_entity_transform(entity_id.value());
    if (transform == nullptr) {
      continue;
    }
    const f32 distance_sq =
        distance_squared(context.transform.position, transform->position);
    if (distance_sq > best_distance_sq) {
      continue;
    }
    if (!target_has_line_of_sight(context, entity_id.value())) {
      continue;
    }
    best_distance_sq = distance_sq;
    best = entity_id.value();
  }
  return best;
}

[[nodiscard]] Vec3f target_direction(
    world::EntityGoalContext& context,
    EntityID target) noexcept {
  const entity::EntityTransform* target_transform =
      context.world.find_entity_transform(target);
  if (target_transform == nullptr) {
    return {};
  }
  return target_transform->position - context.transform.position;
}

[[nodiscard]] bool target_farther_than(
    world::EntityGoalContext& context,
    f32 range) {
  if (!blackboard_target_valid(context, false)) {
    return false;
  }
  const entity::EntityTransform* target_transform =
      context.world.find_entity_transform(context.brain.blackboard.target_entity.value());
  if (target_transform == nullptr) {
    return false;
  }
  return distance_squared(context.transform.position, target_transform->position) >
         range * range;
}

[[nodiscard]] bool target_within(
    world::EntityGoalContext& context,
    f32 range) {
  return blackboard_target_valid(context) && !target_farther_than(context, range);
}

[[nodiscard]] bool world_cell_contains_fluid(
    const world::EntityGoalContext& context,
    BlockPos pos) noexcept {
  if (!context.world.get_fluid(pos).empty()) {
    return true;
  }
  return context.block_registry != nullptr &&
         world::world_cell_contains_fluid(
             context.world,
             *context.block_registry,
             pos);
}

[[nodiscard]] u32 fluid_cell_count_in_bounds(
    const world::EntityGoalContext& context,
    Aabb3f bounds) noexcept {
  const i32 min_x = static_cast<i32>(std::floor(bounds.min.x));
  const i32 min_y = static_cast<i32>(std::floor(bounds.min.y));
  const i32 min_z = static_cast<i32>(std::floor(bounds.min.z));
  const i32 max_x = static_cast<i32>(std::ceil(bounds.max.x));
  const i32 max_y = static_cast<i32>(std::ceil(bounds.max.y));
  const i32 max_z = static_cast<i32>(std::ceil(bounds.max.z));
  u32 count = 0U;
  for (i32 z = min_z; z < max_z; ++z) {
    for (i32 y = min_y; y < max_y; ++y) {
      for (i32 x = min_x; x < max_x; ++x) {
        if (world_cell_contains_fluid(context, {{x, y, z}})) {
          ++count;
        }
      }
    }
  }
  return count;
}

[[nodiscard]] bool bounds_overlap_fluid(
    const world::EntityGoalContext& context,
    Aabb3f bounds) noexcept {
  return fluid_cell_count_in_bounds(context, bounds) > 0U;
}

[[nodiscard]] bool entity_in_fluid(world::EntityGoalContext& context) {
  const Aabb3f bounds = entity_body_bounds(
      context.transform.position,
      entity_collision_box(context.world, context.entity_id));
  return bounds_overlap_fluid(context, bounds);
}

[[nodiscard]] bool candidate_is_solid_free(
    world::EntityGoalContext& context,
    Aabb3f candidate) {
  if (context.block_registry == nullptr) {
    return false;
  }
  const world::WorldCollisionQuery solid_query{
      context.world,
      *context.block_registry,
      true};
  return !physics::overlaps_solid_blocks(solid_query, candidate, 0.001F);
}

[[nodiscard]] bool candidate_has_support_below(
    world::EntityGoalContext& context,
    Aabb3f candidate) {
  if (context.block_registry == nullptr) {
    return false;
  }
  const world::WorldCollisionQuery support_query{
      context.world,
      *context.block_registry,
      false};
  return physics::has_support_below(support_query, candidate, 0.02F);
}

[[nodiscard]] bool candidate_is_dry_and_supported(
    world::EntityGoalContext& context,
    Aabb3f current_bounds,
    Vec3f direction,
    f32 probe_distance = FluidEscapeProbeDistance) {
  if (!world::has_horizontal_motion(direction)) {
    return false;
  }
  direction = horizontal_unit(direction);
  const Aabb3f candidate = translated(current_bounds, direction * probe_distance);
  return !bounds_overlap_fluid(context, candidate) &&
         candidate_is_solid_free(context, candidate) &&
         candidate_has_support_below(context, candidate);
}

struct FluidAwareness {
  Vec3f away_direction{};
  u32 fluid_cells{};
};

[[nodiscard]] std::optional<FluidAwareness> fluid_awareness_near_bounds(
    const world::EntityGoalContext& context,
    Aabb3f bounds,
    f32 radius) noexcept {
  radius = std::max(0.25F, radius);
  const f32 half_width =
      std::max(bounds.max.x - bounds.min.x, bounds.max.z - bounds.min.z) * 0.5F;
  const f32 search_radius = radius + half_width + 0.75F;
  const f32 search_radius_sq = search_radius * search_radius;
  const Vec3f center = bounds_center(bounds);
  const i32 min_x = static_cast<i32>(std::floor(bounds.min.x - radius));
  const i32 min_y = static_cast<i32>(std::floor(bounds.min.y - radius));
  const i32 min_z = static_cast<i32>(std::floor(bounds.min.z - radius));
  const i32 max_x = static_cast<i32>(std::ceil(bounds.max.x + radius));
  const i32 max_y = static_cast<i32>(std::ceil(bounds.max.y + radius));
  const i32 max_z = static_cast<i32>(std::ceil(bounds.max.z + radius));

  FluidAwareness awareness;
  for (i32 z = min_z; z < max_z; ++z) {
    for (i32 y = min_y; y < max_y; ++y) {
      for (i32 x = min_x; x < max_x; ++x) {
        if (!world_cell_contains_fluid(context, {{x, y, z}})) {
          continue;
        }
        const Vec3f fluid_center{
            static_cast<f32>(x) + 0.5F,
            static_cast<f32>(y) + 0.5F,
            static_cast<f32>(z) + 0.5F};
        const Vec3f away{center.x - fluid_center.x, 0.0F, center.z - fluid_center.z};
        const f32 distance_sq = horizontal_length_squared(away);
        if (distance_sq > search_radius_sq) {
          continue;
        }
        ++awareness.fluid_cells;
        if (distance_sq > 0.0001F) {
          awareness.away_direction =
              awareness.away_direction + away * (1.0F / std::max(0.25F, distance_sq));
        }
      }
    }
  }

  if (awareness.fluid_cells == 0U) {
    return std::nullopt;
  }
  awareness.away_direction = horizontal_unit(awareness.away_direction);
  return awareness;
}

[[nodiscard]] std::optional<Vec3f> avoid_fluid_direction(
    world::EntityGoalContext& context) {
  if (context.block_registry == nullptr) {
    return std::nullopt;
  }

  const Aabb3f bounds = entity_body_bounds(
      context.transform.position,
      entity_collision_box(context.world, context.entity_id));
  if (bounds_overlap_fluid(context, bounds)) {
    return std::nullopt;
  }

  const f32 radius =
      context.goal.radius > 0.0F ? context.goal.radius : DefaultAvoidFluidRadius;
  const std::optional<FluidAwareness> awareness =
      fluid_awareness_near_bounds(context, bounds, radius);
  if (!awareness.has_value()) {
    return std::nullopt;
  }

  Vec3f preferred = horizontal_unit(context.goal.state.movement_direction);
  if (!world::has_horizontal_motion(preferred)) {
    preferred = world::deterministic_wander_direction(
        context.entity_id,
        context.tick_index,
        context.goal_index);
  }
  preferred = horizontal_unit(preferred);
  const Vec3f away = awareness->away_direction;
  const u32 current_near_fluid = awareness->fluid_cells;
  Vec3f best{};
  f32 best_score = -1000000.0F;

  const auto consider = [&](Vec3f candidate, f32 bonus) {
    candidate = horizontal_unit(candidate);
    if (!world::has_horizontal_motion(candidate)) {
      return;
    }

    const Aabb3f candidate_bounds =
        translated(bounds, candidate * FluidAvoidProbeDistance);
    if (bounds_overlap_fluid(context, candidate_bounds) ||
        !candidate_is_solid_free(context, candidate_bounds) ||
        !candidate_has_support_below(context, candidate_bounds)) {
      return;
    }

    const std::optional<FluidAwareness> candidate_awareness =
        fluid_awareness_near_bounds(context, candidate_bounds, radius);
    const u32 candidate_near_fluid =
        candidate_awareness.has_value() ? candidate_awareness->fluid_cells : 0U;
    const f32 away_alignment =
        world::has_horizontal_motion(away) ? dot(candidate, away) : 0.0F;
    if (away_alignment < -0.25F && candidate_near_fluid >= current_near_fluid) {
      return;
    }

    const f32 near_fluid_delta =
        static_cast<f32>(static_cast<i32>(current_near_fluid) -
                         static_cast<i32>(candidate_near_fluid));
    f32 score = near_fluid_delta * 4.0F + away_alignment * 3.0F + bonus;
    if (candidate_near_fluid == 0U) {
      score += 8.0F;
    }
    if (score > best_score) {
      best_score = score;
      best = candidate;
    }
  };

  consider(away, 2.0F);
  consider(preferred, 0.25F);
  consider(preferred * -1.0F, 0.0F);
  consider({1.0F, 0.0F, 0.0F}, 0.0F);
  consider({-1.0F, 0.0F, 0.0F}, 0.0F);
  consider({0.0F, 0.0F, 1.0F}, 0.0F);
  consider({0.0F, 0.0F, -1.0F}, 0.0F);
  for (u32 attempt = 0U; attempt < 4U; ++attempt) {
    consider(
        world::deterministic_wander_direction(
            context.entity_id,
            context.tick_index + attempt + 3U,
            context.goal_index + attempt + 1U),
        0.0F);
  }

  if (!world::has_horizontal_motion(best)) {
    return std::nullopt;
  }
  return best;
}

[[nodiscard]] Vec3f swim_escape_direction(world::EntityGoalContext& context) {
  const Aabb3f bounds = entity_body_bounds(
      context.transform.position,
      entity_collision_box(context.world, context.entity_id));
  Vec3f fallback = context.goal.state.movement_direction;
  if (!world::has_horizontal_motion(fallback)) {
    fallback = world::deterministic_wander_direction(
        context.entity_id,
        context.tick_index,
        context.goal_index);
  }

  const u32 current_fluid_cells = fluid_cell_count_in_bounds(context, bounds);
  const std::optional<FluidAwareness> awareness =
      fluid_awareness_near_bounds(context, bounds, DefaultAvoidFluidRadius);
  const Vec3f away =
      awareness.has_value() ? awareness->away_direction : Vec3f{};
  Vec3f best{};
  f32 best_score = -1000000.0F;
  u32 best_fluid_cells = current_fluid_cells;
  bool best_dry_supported = false;

  const auto consider = [&](Vec3f candidate, f32 bonus) {
    candidate = horizontal_unit(candidate);
    if (!world::has_horizontal_motion(candidate)) {
      return;
    }

    const Aabb3f candidate_bounds =
        translated(bounds, candidate * FluidEscapeProbeDistance);
    if (!candidate_is_solid_free(context, candidate_bounds)) {
      return;
    }
    const u32 candidate_fluid_cells =
        fluid_cell_count_in_bounds(context, candidate_bounds);
    const bool dry = candidate_fluid_cells == 0U;
    const bool supported = candidate_has_support_below(context, candidate_bounds);
    if (dry && !supported) {
      return;
    }

    const f32 away_alignment =
        world::has_horizontal_motion(away) ? dot(candidate, away) : 0.0F;
    const f32 fluid_delta =
        static_cast<f32>(static_cast<i32>(current_fluid_cells) -
                         static_cast<i32>(candidate_fluid_cells));
    f32 score = fluid_delta * 12.0F + away_alignment * 4.0F + bonus;
    if (dry) {
      score += 60.0F;
    }
    if (supported) {
      score += 6.0F;
    }
    if (candidate_is_dry_and_supported(context, bounds, candidate)) {
      score += 20.0F;
    }
    if (candidate_fluid_cells >= current_fluid_cells &&
        away_alignment <= 0.0F && !supported) {
      score -= 12.0F;
    }

    if (score > best_score) {
      best_score = score;
      best = candidate;
      best_fluid_cells = candidate_fluid_cells;
      best_dry_supported = dry && supported;
    }
  };

  consider(away, 3.0F);
  consider(fallback, 0.5F);
  consider(fallback * -1.0F, 0.0F);
  consider({1.0F, 0.0F, 0.0F}, 0.0F);
  consider({-1.0F, 0.0F, 0.0F}, 0.0F);
  consider({0.0F, 0.0F, 1.0F}, 0.0F);
  consider({0.0F, 0.0F, -1.0F}, 0.0F);
  consider(
      world::deterministic_wander_direction(
          context.entity_id,
          context.tick_index + 3U,
          context.goal_index),
      0.0F);
  consider(
      world::deterministic_wander_direction(
          context.entity_id,
          context.tick_index + 7U,
          context.goal_index),
      0.0F);

  if (world::has_horizontal_motion(best) &&
      (best_dry_supported || best_fluid_cells < current_fluid_cells ||
       (world::has_horizontal_motion(away) && dot(best, away) > 0.25F))) {
    context.goal.state.movement_direction = best;
    const f32 horizontal_scale = best_dry_supported ? 1.0F : 0.75F;
    return {best.x * horizontal_scale, 1.0F, best.z * horizontal_scale};
  }

  fallback = normalize_or_zero({fallback.x, 0.0F, fallback.z});
  context.goal.state.movement_direction = fallback;
  return {
      fallback.x * SwimFallbackHorizontalScale,
      1.0F,
      fallback.z * SwimFallbackHorizontalScale};
}

[[nodiscard]] world::EntityGoalHandler make_idle_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Movement);
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
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Movement);
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

[[nodiscard]] world::EntityGoalHandler make_avoid_fluid_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Movement);
  handler.can_start = [](world::EntityGoalContext& context) {
    return avoid_fluid_direction(context).has_value();
  };
  handler.can_continue = [](world::EntityGoalContext& context) {
    return context.goal.state.remaining_ticks > 0U &&
           avoid_fluid_direction(context).has_value();
  };
  handler.start = [](world::EntityGoalContext& context) {
    if (const std::optional<Vec3f> direction = avoid_fluid_direction(context);
        direction.has_value()) {
      context.goal.state.movement_direction = direction.value();
      return;
    }
    context.goal.state.movement_direction = world::deterministic_wander_direction(
        context.entity_id,
        context.tick_index,
        context.goal_index);
  };
  handler.tick = [](world::EntityGoalContext& context) {
    const std::optional<Vec3f> direction = avoid_fluid_direction(context);
    if (!direction.has_value()) {
      context.clear_movement_intent();
      return;
    }
    context.goal.state.movement_direction = direction.value();
    entity::MovementIntent& intent = context.movement_intent();
    intent.desired_direction = direction.value();
    intent.speed_multiplier = std::max(0.0F, context.goal.speed_multiplier);
    intent.jump = false;
    intent.sprint = false;
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_swim_in_fluid_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Movement);
  handler.can_start = entity_in_fluid;
  handler.can_continue = entity_in_fluid;
  handler.start = [](world::EntityGoalContext& context) {
    context.goal.state.movement_direction = world::deterministic_wander_direction(
        context.entity_id,
        context.tick_index,
        context.goal_index);
  };
  handler.tick = [](world::EntityGoalContext& context) {
    entity::MovementIntent& intent = context.movement_intent();
    intent.desired_direction = swim_escape_direction(context);
    intent.speed_multiplier = std::max(0.0F, context.goal.speed_multiplier);
    intent.jump = true;
    intent.sprint = false;
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_nearest_target_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Target);
  handler.can_start = [](world::EntityGoalContext& context) {
    return context.goal.target.empty() || context.goal.target == "nearest_player";
  };
  handler.can_continue = [](world::EntityGoalContext& context) {
    return context.goal.state.remaining_ticks > 0U;
  };
  handler.tick = [](world::EntityGoalContext& context) {
    const u32 memory_ticks =
        context.goal.memory_ticks == 0U ? DefaultTargetMemoryTicks : context.goal.memory_ticks;
    if (context.brain.blackboard.target_entity.has_value() &&
        context.brain.blackboard.target_memory_until > context.tick_index &&
        blackboard_target_valid(context)) {
      return;
    }

    const std::optional<EntityID> target = find_nearest_player_target(context);
    if (target.has_value()) {
      context.brain.blackboard.target_entity = target.value();
      context.brain.blackboard.target_memory_until = context.tick_index + memory_ticks;
      return;
    }

    if (context.brain.blackboard.target_memory_until <= context.tick_index) {
      context.brain.blackboard.target_entity.reset();
    }
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_move_to_target_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Movement);
  handler.can_start = [](world::EntityGoalContext& context) {
    return target_farther_than(context, configured_range(context.goal, DefaultAttackRange));
  };
  handler.can_continue = [](world::EntityGoalContext& context) {
    return context.goal.state.remaining_ticks > 0U &&
           target_farther_than(context, configured_range(context.goal, DefaultAttackRange));
  };
  handler.tick = [](world::EntityGoalContext& context) {
    if (!context.brain.blackboard.target_entity.has_value()) {
      context.clear_movement_intent();
      return;
    }
    entity::MovementIntent& intent = context.movement_intent();
    intent.desired_direction = normalize_or_zero(target_direction(
        context,
        context.brain.blackboard.target_entity.value()));
    intent.speed_multiplier = std::max(0.0F, context.goal.speed_multiplier);
    intent.jump = false;
    intent.sprint = intent.speed_multiplier > 1.0F;
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_look_at_target_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Look);
  handler.can_start = [](world::EntityGoalContext& context) {
    return blackboard_target_valid(context);
  };
  handler.can_continue = [](world::EntityGoalContext& context) {
    return context.goal.state.remaining_ticks > 0U && blackboard_target_valid(context);
  };
  handler.tick = [](world::EntityGoalContext& context) {
    if (!context.brain.blackboard.target_entity.has_value()) {
      return;
    }
    const Vec3f direction = target_direction(
        context,
        context.brain.blackboard.target_entity.value());
    if (!world::has_horizontal_motion(direction)) {
      return;
    }
    (void)context.world.set_entity_transform(
        context.entity_id,
        context.transform.position,
        yaw_rotation(model_yaw_from_horizontal_direction(direction)));
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_melee_attack_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Attack);
  handler.can_start = [](world::EntityGoalContext& context) {
    return context.goal.damage > 0.0F &&
           target_within(context, configured_range(context.goal, DefaultAttackRange));
  };
  handler.can_continue = [](world::EntityGoalContext& context) {
    return context.goal.state.remaining_ticks > 0U &&
           context.goal.damage > 0.0F &&
           target_within(context, configured_range(context.goal, DefaultAttackRange));
  };
  handler.tick = [](world::EntityGoalContext& context) {
    if (context.goal.state.cooldown_until > context.tick_index ||
        !context.brain.blackboard.target_entity.has_value() ||
        !target_within(context, configured_range(context.goal, DefaultAttackRange))) {
      return;
    }
    const EntityID target = context.brain.blackboard.target_entity.value();
    const world::DamageResult damage = context.world.apply_damage({
        context.entity_id,
        target,
        context.goal.damage,
        world::DamageCause::EntityAttack,
        true});
    context.goal.state.cooldown_until =
        context.tick_index + std::max<u32>(1U, context.goal.cooldown_ticks);
    if (damage.applied && !context.goal.attack_animation.empty()) {
      context.goal.state.triggered_animation = context.goal.attack_animation;
      context.goal.state.triggered_animation_until =
          context.tick_index + DefaultAttackAnimationTicks;
    }
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_flee_from_target_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Movement);
  handler.can_start = [](world::EntityGoalContext& context) {
    return blackboard_target_valid(context);
  };
  handler.can_continue = [](world::EntityGoalContext& context) {
    return context.goal.state.remaining_ticks > 0U && blackboard_target_valid(context);
  };
  handler.tick = [](world::EntityGoalContext& context) {
    if (!context.brain.blackboard.target_entity.has_value()) {
      context.clear_movement_intent();
      return;
    }
    entity::MovementIntent& intent = context.movement_intent();
    intent.desired_direction = normalize_or_zero(target_direction(
        context,
        context.brain.blackboard.target_entity.value()) * -1.0F);
    intent.speed_multiplier = std::max(0.0F, context.goal.speed_multiplier);
    intent.jump = false;
    intent.sprint = intent.speed_multiplier > 1.0F;
  };
  return handler;
}

[[nodiscard]] world::EntityGoalHandler make_return_home_goal_handler() {
  world::EntityGoalHandler handler;
  handler.categories = world::goal_category_mask(world::EntityGoalCategory::Movement);
  handler.can_start = [](world::EntityGoalContext& context) {
    const f32 radius = context.goal.radius > 0.0F
                           ? context.goal.radius
                           : DefaultReturnHomeRadius;
    return distance_squared(context.transform.position, context.brain.blackboard.home_position) >
           radius * radius;
  };
  handler.can_continue = [](world::EntityGoalContext& context) {
    const f32 range = configured_range(context.goal, 1.0F);
    return context.goal.state.remaining_ticks > 0U &&
           distance_squared(context.transform.position, context.brain.blackboard.home_position) >
               range * range;
  };
  handler.tick = [](world::EntityGoalContext& context) {
    entity::MovementIntent& intent = context.movement_intent();
    intent.desired_direction =
        normalize_or_zero(context.brain.blackboard.home_position - context.transform.position);
    intent.speed_multiplier = std::max(0.0F, context.goal.speed_multiplier);
    intent.jump = false;
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
      std::string{ids::AvoidFluidGoalID},
      make_avoid_fluid_goal_handler());
  assert(result && "avoid-fluid goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::SwimInFluidGoalID},
      make_swim_in_fluid_goal_handler());
  assert(result && "swim-in-fluid goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::FloatInFluidGoalID},
      make_swim_in_fluid_goal_handler());
  assert(result && "float-in-fluid goal alias registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::NearestTargetGoalID},
      make_nearest_target_goal_handler());
  assert(result && "nearest-target goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::MoveToTargetGoalID},
      make_move_to_target_goal_handler());
  assert(result && "move-to-target goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::LookAtTargetGoalID},
      make_look_at_target_goal_handler());
  assert(result && "look-at-target goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::MeleeAttackGoalID},
      make_melee_attack_goal_handler());
  assert(result && "melee-attack goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::FleeFromTargetGoalID},
      make_flee_from_target_goal_handler());
  assert(result && "flee-from-target goal registration failed");
  (void)result;

  result = registry.register_handler(
      std::string{ids::ReturnHomeGoalID},
      make_return_home_goal_handler());
  assert(result && "return-home goal registration failed");
  (void)result;
}

} // namespace arv::base
