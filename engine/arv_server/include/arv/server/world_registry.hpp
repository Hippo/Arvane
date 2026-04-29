#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"

#include <string>

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
  bool register_world(WorldRecord record);
  void ensure_default_world();
  [[nodiscard]] WorldRecord* find(WorldID id) noexcept;
  [[nodiscard]] const WorldRecord* find(WorldID id) const noexcept;
  [[nodiscard]] usize size() const noexcept;

private:
  StableHashMap<WorldID, WorldRecord> worlds_;
};

} // namespace arv::server
