#include "arv/server/world_registry.hpp"

#include <utility>

namespace arv::server {

bool WorldRegistry::register_world(WorldRecord record) {
  const WorldID id = record.id;
  return worlds_.emplace(id, std::move(record)).second;
}

void WorldRegistry::ensure_default_world() {
  if (find(DefaultWorldID) != nullptr) {
    return;
  }
  register_world({DefaultWorldID, "default", WorldRuntimeState::Running, 1});
}

WorldRecord* WorldRegistry::find(WorldID id) noexcept {
  const auto found = worlds_.find(id);
  return found == worlds_.end() ? nullptr : &found->second;
}

const WorldRecord* WorldRegistry::find(WorldID id) const noexcept {
  const auto found = worlds_.find(id);
  return found == worlds_.end() ? nullptr : &found->second;
}

usize WorldRegistry::size() const noexcept {
  return worlds_.size();
}

} // namespace arv::server
