#include "arv/server/world_registry.hpp"

#include <algorithm>
#include <utility>

namespace arv::server {

bool WorldRegistry::register_world(WorldRecord record) {
  const WorldID id = record.id;
  if (find(record.name) != nullptr) {
    return false;
  }
  return worlds_.emplace(id, std::move(record)).second;
}

bool WorldRegistry::upsert_world(WorldRecord record) {
  if (record.name.empty() || record.id.value == 0U) {
    return false;
  }
  for (auto iter = worlds_.begin(); iter != worlds_.end(); ++iter) {
    if (iter->second.name == record.name && iter->first != record.id) {
      return false;
    }
  }
  worlds_[record.id] = std::move(record);
  return true;
}

void WorldRegistry::ensure_default_world() {
  if (find(DefaultWorldID) != nullptr) {
    return;
  }
  register_world({
      DefaultWorldID,
      "default",
      WorldRuntimeState::Running,
      1,
      {},
      0U,
      {},
      "default",
      std::nullopt,
      true,
      true,
      {}});
}

void WorldRegistry::clear() noexcept {
  worlds_.clear();
}

WorldRecord* WorldRegistry::find(WorldID id) noexcept {
  const auto found = worlds_.find(id);
  return found == worlds_.end() ? nullptr : &found->second;
}

const WorldRecord* WorldRegistry::find(WorldID id) const noexcept {
  const auto found = worlds_.find(id);
  return found == worlds_.end() ? nullptr : &found->second;
}

WorldRecord* WorldRegistry::find(std::string_view name) noexcept {
  for (auto& [id, record] : worlds_) {
    (void)id;
    if (record.name == name) {
      return &record;
    }
  }
  return nullptr;
}

const WorldRecord* WorldRegistry::find(std::string_view name) const noexcept {
  for (const auto& [id, record] : worlds_) {
    (void)id;
    if (record.name == name) {
      return &record;
    }
  }
  return nullptr;
}

bool WorldRegistry::erase(std::string_view name) {
  for (auto iter = worlds_.begin(); iter != worlds_.end(); ++iter) {
    if (iter->second.name == name) {
      worlds_.erase(iter);
      return true;
    }
  }
  return false;
}

bool WorldRegistry::rename(std::string_view old_name, std::string new_name) {
  if (new_name.empty() || find(new_name) != nullptr) {
    return false;
  }
  WorldRecord* record = find(old_name);
  if (record == nullptr) {
    return false;
  }
  record->name = std::move(new_name);
  return true;
}

bool WorldRegistry::set_default(std::string_view name) {
  WorldRecord* next_default = find(name);
  if (next_default == nullptr) {
    return false;
  }
  for (auto& [id, record] : worlds_) {
    (void)id;
    record.is_default = false;
  }
  next_default->is_default = true;
  next_default->load_on_start = true;
  return true;
}

std::optional<std::string> WorldRegistry::default_world_name() const {
  for (const auto& [id, record] : worlds_) {
    (void)id;
    if (record.is_default) {
      return record.name;
    }
  }
  return std::nullopt;
}

std::vector<WorldRecord> WorldRegistry::records() const {
  std::vector<WorldRecord> out;
  out.reserve(worlds_.size());
  for (const auto& [id, record] : worlds_) {
    (void)id;
    out.push_back(record);
  }
  std::sort(out.begin(), out.end(), [](const WorldRecord& lhs, const WorldRecord& rhs) {
    if (lhs.is_default != rhs.is_default) {
      return lhs.is_default;
    }
    return lhs.name < rhs.name;
  });
  return out;
}

WorldID WorldRegistry::next_available_id() const noexcept {
  WorldID::value_type next = DefaultWorldID.value;
  for (const auto& [id, record] : worlds_) {
    (void)record;
    next = std::max(next, static_cast<WorldID::value_type>(id.value + 1U));
  }
  return WorldID{next};
}

usize WorldRegistry::size() const noexcept {
  return worlds_.size();
}

} // namespace arv::server
