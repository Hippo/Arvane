#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/physics/collision.hpp"
#include "arv/voxel/block.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::world {

class WorldRuntime;

struct EntityGoalRegistryError {
  std::string message{};
};

struct EntityGoalContext {
  WorldRuntime& world;
  EntityID entity_id{};
  TickIndex tick_index{};
  const entity::EntityTransform& transform;
  entity::BrainComponent& brain;
  entity::GoalSlot& goal;
  usize goal_index{};
  FunctionRef<void(std::string_view)> set_animation{};
  FunctionRef<void()> clear_movement_intent{};
  FunctionRef<entity::MovementIntent&()> movement_intent{};
};

struct EntityGoalBlockedContext {
  WorldRuntime& world;
  EntityID entity_id{};
  TickIndex tick_index{};
  const physics::ICollisionQuery& collision_query;
  Aabb3f bounds{};
  entity::BrainComponent& brain;
  entity::GoalSlot& goal;
  usize goal_index{};
  entity::MovementIntent* movement_intent{};
  Vec3f blocked_delta{};
  f32 collision_epsilon{};
};

struct EntityGoalHandler {
  std::function<bool(EntityGoalContext&)> can_start{};
  std::function<bool(EntityGoalContext&)> can_continue{};
  std::function<void(EntityGoalContext&)> start{};
  std::function<void(EntityGoalContext&)> tick{};
  std::function<void(EntityGoalContext&, entity::GoalStopReason)> stop{};
  std::function<void(EntityGoalBlockedContext&)> blocked{};
};

class EntityGoalRegistry {
public:
  using Result = Expected<void, EntityGoalRegistryError>;

  [[nodiscard]] Result register_handler(std::string id, EntityGoalHandler handler);
  [[nodiscard]] const EntityGoalHandler* find(std::string_view id) const noexcept;
  [[nodiscard]] bool contains(std::string_view id) const noexcept;

private:
  std::vector<std::pair<std::string, EntityGoalHandler>> handlers_;
};

} // namespace arv::world
