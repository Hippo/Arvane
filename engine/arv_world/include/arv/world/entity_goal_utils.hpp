#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/physics/collision_query.hpp"

namespace arv::world {

[[nodiscard]] bool has_horizontal_motion(Vec3f value) noexcept;

[[nodiscard]] Vec3f deterministic_wander_direction(
    EntityID entity_id,
    TickIndex tick,
    usize goal_index) noexcept;

[[nodiscard]] Vec3f random_clear_wander_direction(
    EntityID entity_id,
    TickIndex tick,
    usize goal_index,
    const physics::ICollisionQuery& query,
    const Aabb3f& bounds,
    Vec3f blocked_delta,
    f32 collision_epsilon);

} // namespace arv::world
