#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"

#include <string>
#include <unordered_map>

namespace arv::server {

inline constexpr WorldID DefaultWorldID{1};

enum class WorldRuntimeState {
  Loading,
  Running,
  Saving,
  Stopped,
};

struct WorldRecord {
  WorldID id{};
  std::string name{};
  WorldRuntimeState state{WorldRuntimeState::Stopped};
  u64 version{};
};

class WorldRegistry {
public:
  bool register_world(WorldRecord record) {
    const WorldID id = record.id;
    return worlds_.emplace(id, std::move(record)).second;
  }

  void ensure_default_world() {
    if (find(DefaultWorldID) != nullptr) {
      return;
    }
    register_world({DefaultWorldID, "default", WorldRuntimeState::Running, 1});
  }

  [[nodiscard]] WorldRecord* find(WorldID id) noexcept {
    const auto found = worlds_.find(id);
    return found == worlds_.end() ? nullptr : &found->second;
  }

  [[nodiscard]] const WorldRecord* find(WorldID id) const noexcept {
    const auto found = worlds_.find(id);
    return found == worlds_.end() ? nullptr : &found->second;
  }

  [[nodiscard]] usize size() const noexcept {
    return worlds_.size();
  }

private:
  std::unordered_map<WorldID, WorldRecord> worlds_;
};

} // namespace arv::server
