#include "arv/world/entity_goal_registry.hpp"

#include <utility>

namespace arv::world {

EntityGoalRegistry::Result EntityGoalRegistry::register_handler(
    std::string id,
    EntityGoalHandler handler) {
  if (id.empty()) {
    return Result::failure({"entity goal handler id is empty"});
  }
  if (contains(id)) {
    return Result::failure({"duplicate entity goal handler id: " + id});
  }
  handlers_.push_back({std::move(id), std::move(handler)});
  return Result::success();
}

const EntityGoalHandler* EntityGoalRegistry::find(std::string_view id) const noexcept {
  for (const auto& [handler_id, handler] : handlers_) {
    if (handler_id == id) {
      return &handler;
    }
  }
  return nullptr;
}

bool EntityGoalRegistry::contains(std::string_view id) const noexcept {
  return find(id) != nullptr;
}

} // namespace arv::world
