#include "world_runtime_internal.hpp"

#include <algorithm>
#include <cmath>

namespace arv::world {

DamageResult WorldRuntime::apply_damage(const DamageRequest& request) noexcept {
  DamageResult result;
  entity::EntityHealth* health = find_entity_health(request.target);
  if (health == nullptr) {
    return result;
  }

  result.previous_health = health->current;
  result.new_health = health->current;

  if (health->invulnerable || health->current <= 0.0F ||
      !std::isfinite(request.amount) || request.amount <= 0.0F) {
    return result;
  }

  health->current = std::clamp(health->current - request.amount, 0.0F, health->max);
  result.applied = health->current < result.previous_health;
  result.killed = result.previous_health > 0.0F && health->current <= 0.0F;
  if (result.killed && player_for_entity(request.target).has_value()) {
    health->current = health->max;
  }
  result.new_health = health->current;
  return result;
}

} // namespace arv::world
