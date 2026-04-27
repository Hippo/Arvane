#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/player/permissions.hpp"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arv::world {

struct RegionTag {};
struct ZoneTag {};

using RegionID = ID<RegionTag, u32>;
using ZoneID = ID<ZoneTag, u32>;

enum class RegionShapeType : u8 {
  Aabb,
};

struct RegionShape {
  RegionShapeType type{RegionShapeType::Aabb};
  Vec3f min{};
  Vec3f max{};
};

struct Zone {
  ZoneID id{};
  std::string name{};
  i32 priority{};
  player::CorePermissionSet core{};
  player::CustomPermissionMap permissions{};
};

struct Region {
  RegionID id{};
  std::string name{};
  WorldID world{};
  i32 priority{};
  RegionShape shape{};
  std::vector<ZoneID> zones{};
  player::CorePermissionSet core{};
  player::CustomPermissionMap permissions{};
};

struct RegionQuery {
  WorldID world{};
  Vec3f position{};
};

struct PlayerRegionState {
  std::vector<RegionID> active_regions{};
};

struct RegionMembershipChange {
  std::vector<RegionID> entered{};
  std::vector<RegionID> exited{};
};

class RegionRegistry {
public:
  [[nodiscard]] ZoneID add_zone(Zone zone);
  [[nodiscard]] RegionID add_region(Region region);

  [[nodiscard]] const Zone* zone(ZoneID id) const noexcept;
  [[nodiscard]] const Region* region(RegionID id) const noexcept;
  [[nodiscard]] std::vector<RegionID> find_regions_at(RegionQuery query) const;
  [[nodiscard]] player::PermissionLayer permission_layer_at(RegionQuery query) const;

  [[nodiscard]] RegionMembershipChange update_membership(
      PlayerRegionState& state,
      RegionQuery query) const;

  [[nodiscard]] bool empty() const noexcept {
    return regions_.empty() && zones_.empty();
  }

private:
  [[nodiscard]] static bool contains(const RegionShape& shape, Vec3f position) noexcept;
  [[nodiscard]] bool zone_exists(ZoneID id) const noexcept;

  std::vector<Zone> zones_{};
  std::vector<Region> regions_{};
  std::unordered_map<std::string, ZoneID> zone_names_{};
  std::unordered_map<std::string, RegionID> region_names_{};
};

} // namespace arv::world
