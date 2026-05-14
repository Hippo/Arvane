#include "arv/world/entity_goal_utils.hpp"

#include "arv/physics/collision_resolution.hpp"

#include <algorithm>
#include <cmath>

namespace arv::world {
namespace {

[[nodiscard]] f32 positive_fraction(f32 value) noexcept {
  const f32 whole = std::floor(value);
  return value - whole;
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

} // namespace

bool has_horizontal_motion(Vec3f value) noexcept {
  return value.x * value.x + value.z * value.z > 0.000001F;
}

Vec3f deterministic_wander_direction(
    EntityID entity_id,
    TickIndex tick,
    usize goal_index) noexcept {
  const f32 seed =
      static_cast<f32>(entity_id.value * 37U + static_cast<u32>(goal_index) * 101U) +
      static_cast<f32>(tick % 100000U) * 0.173F;
  const f32 angle = positive_fraction(std::sin(seed) * 43758.5453F) * TauRadians;
  return {std::cos(angle), 0.0F, std::sin(angle)};
}

Vec3f random_clear_wander_direction(
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

} // namespace arv::world
