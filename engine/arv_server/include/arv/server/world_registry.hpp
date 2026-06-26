#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/location.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
  std::string path{};
  u64 seed{};
  std::string generator_id{};
  std::string world_type{"default"};
  std::optional<Location> spawn{};
  bool load_on_start{};
  bool is_default{};
  std::string created_at{};
};

class WorldRegistry {
public:
  bool register_world(WorldRecord record);
  bool upsert_world(WorldRecord record);
  void ensure_default_world();
  void clear() noexcept;
  [[nodiscard]] WorldRecord* find(WorldID id) noexcept;
  [[nodiscard]] const WorldRecord* find(WorldID id) const noexcept;
  [[nodiscard]] WorldRecord* find(std::string_view name) noexcept;
  [[nodiscard]] const WorldRecord* find(std::string_view name) const noexcept;
  [[nodiscard]] bool erase(std::string_view name);
  [[nodiscard]] bool rename(std::string_view old_name, std::string new_name);
  bool set_default(std::string_view name);
  [[nodiscard]] std::optional<std::string> default_world_name() const;
  [[nodiscard]] std::vector<WorldRecord> records() const;
  [[nodiscard]] WorldID next_available_id() const noexcept;
  [[nodiscard]] usize size() const noexcept;

private:
  StableHashMap<WorldID, WorldRecord> worlds_;
};

} // namespace arv::server
