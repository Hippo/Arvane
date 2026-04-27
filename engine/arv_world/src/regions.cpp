#include "arv/world/regions.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace arv::world {
namespace {

void merge_core(
    player::CorePermissionSet& target,
    const player::CorePermissionSet& source,
    bool equal_priority) {
  auto merge = [equal_priority](player::TriState& current, player::TriState incoming) {
    if (incoming == player::TriState::Unset) {
      return;
    }
    if (equal_priority && current == player::TriState::Deny &&
        incoming == player::TriState::Allow) {
      return;
    }
    current = incoming;
  };

  merge(target.can_take_damage, source.can_take_damage);
  merge(target.can_modify_world, source.can_modify_world);
  merge(target.can_interact, source.can_interact);
  merge(target.can_open_inventory, source.can_open_inventory);
  merge(target.has_creative_inventory, source.has_creative_inventory);
  merge(target.can_fly, source.can_fly);
  merge(target.has_collision, source.has_collision);
  merge(target.is_visible_to_others, source.is_visible_to_others);
}

void merge_custom(
    player::CustomPermissionMap& target,
    const player::CustomPermissionMap& source,
    bool equal_priority) {
  for (const auto& [node, incoming] : source) {
    if (incoming == player::TriState::Unset) {
      continue;
    }

    const auto found = target.find(node);
    if (equal_priority && found != target.end() &&
        found->second == player::TriState::Deny &&
        incoming == player::TriState::Allow) {
      continue;
    }
    target[node] = incoming;
  }
}

void merge_layer(
    player::PermissionLayer& target,
    const player::CorePermissionSet& core,
    const player::CustomPermissionMap& custom,
    bool equal_priority) {
  merge_core(target.core, core, equal_priority);
  merge_custom(target.custom, custom, equal_priority);
}

[[nodiscard]] bool contains_region_id(
    std::span<const RegionID> ids,
    RegionID id) noexcept {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

ZoneID RegionRegistry::add_zone(Zone zone) {
  if (!zone.name.empty() && zone_names_.find(zone.name) != zone_names_.end()) {
    return {};
  }

  zone.id = ZoneID{static_cast<u32>(zones_.size() + 1U)};
  if (!zone.name.empty()) {
    zone_names_[zone.name] = zone.id;
  }
  zones_.push_back(std::move(zone));
  return zones_.back().id;
}

RegionID RegionRegistry::add_region(Region region) {
  if (!region.name.empty() && region_names_.find(region.name) != region_names_.end()) {
    return {};
  }
  if (region.shape.min.x > region.shape.max.x ||
      region.shape.min.y > region.shape.max.y ||
      region.shape.min.z > region.shape.max.z) {
    return {};
  }
  for (const ZoneID zone_id : region.zones) {
    if (!zone_exists(zone_id)) {
      return {};
    }
  }

  region.id = RegionID{static_cast<u32>(regions_.size() + 1U)};
  if (!region.name.empty()) {
    region_names_[region.name] = region.id;
  }
  regions_.push_back(std::move(region));
  return regions_.back().id;
}

const Zone* RegionRegistry::zone(ZoneID id) const noexcept {
  if (id.value == 0U || id.value > zones_.size()) {
    return nullptr;
  }
  return &zones_[id.value - 1U];
}

const Region* RegionRegistry::region(RegionID id) const noexcept {
  if (id.value == 0U || id.value > regions_.size()) {
    return nullptr;
  }
  return &regions_[id.value - 1U];
}

std::vector<RegionID> RegionRegistry::find_regions_at(RegionQuery query) const {
  std::vector<RegionID> matches;
  for (const Region& candidate : regions_) {
    if (candidate.world != query.world || !contains(candidate.shape, query.position)) {
      continue;
    }
    matches.push_back(candidate.id);
  }
  return matches;
}

player::PermissionLayer RegionRegistry::permission_layer_at(RegionQuery query) const {
  const std::vector<RegionID> matching_ids = find_regions_at(query);
  player::PermissionLayer layer;
  if (matching_ids.empty()) {
    return layer;
  }

  std::vector<const Zone*> matching_zones;
  for (const RegionID region_id : matching_ids) {
    const Region* current = region(region_id);
    if (current == nullptr) {
      continue;
    }
    for (const ZoneID zone_id : current->zones) {
      const Zone* current_zone = zone(zone_id);
      if (current_zone != nullptr &&
          std::find(matching_zones.begin(), matching_zones.end(), current_zone) == matching_zones.end()) {
        matching_zones.push_back(current_zone);
      }
    }
  }
  std::sort(matching_zones.begin(), matching_zones.end(), [](const Zone* lhs, const Zone* rhs) {
    if (lhs->priority != rhs->priority) {
      return lhs->priority < rhs->priority;
    }
    return lhs->id < rhs->id;
  });

  std::optional<i32> previous_zone_priority;
  for (const Zone* current_zone : matching_zones) {
    const bool equal_priority =
        previous_zone_priority.has_value() &&
        previous_zone_priority.value() == current_zone->priority;
    merge_layer(layer, current_zone->core, current_zone->permissions, equal_priority);
    previous_zone_priority = current_zone->priority;
  }

  std::vector<const Region*> matching_regions;
  matching_regions.reserve(matching_ids.size());
  for (const RegionID region_id : matching_ids) {
    const Region* current = region(region_id);
    if (current != nullptr) {
      matching_regions.push_back(current);
    }
  }
  std::sort(matching_regions.begin(), matching_regions.end(), [](const Region* lhs, const Region* rhs) {
    if (lhs->priority != rhs->priority) {
      return lhs->priority < rhs->priority;
    }
    return lhs->id < rhs->id;
  });

  std::optional<i32> previous_region_priority;
  for (const Region* current : matching_regions) {
    const bool equal_priority =
        previous_region_priority.has_value() &&
        previous_region_priority.value() == current->priority;
    merge_layer(layer, current->core, current->permissions, equal_priority);
    previous_region_priority = current->priority;
  }

  return layer;
}

RegionMembershipChange RegionRegistry::update_membership(
    PlayerRegionState& state,
    RegionQuery query) const {
  std::vector<RegionID> next = find_regions_at(query);
  std::sort(next.begin(), next.end());
  std::sort(state.active_regions.begin(), state.active_regions.end());

  RegionMembershipChange change;
  for (const RegionID id : next) {
    if (!contains_region_id(state.active_regions, id)) {
      change.entered.push_back(id);
    }
  }
  for (const RegionID id : state.active_regions) {
    if (!contains_region_id(next, id)) {
      change.exited.push_back(id);
    }
  }

  state.active_regions = std::move(next);
  return change;
}

bool RegionRegistry::contains(const RegionShape& shape, Vec3f position) noexcept {
  if (shape.type != RegionShapeType::Aabb) {
    return false;
  }
  return position.x >= shape.min.x && position.y >= shape.min.y && position.z >= shape.min.z &&
         position.x <= shape.max.x && position.y <= shape.max.y && position.z <= shape.max.z;
}

bool RegionRegistry::zone_exists(ZoneID id) const noexcept {
  return zone(id) != nullptr;
}

} // namespace arv::world
