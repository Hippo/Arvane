#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/player/permissions.hpp"

#include <span>
#include <string>
#include <string_view>
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
  bool active{true};
  std::string name{};
  i32 priority{};
  player::CorePermissionSet core{};
  player::CustomPermissionMap permissions{};
};

struct Region {
  RegionID id{};
  bool active{true};
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
  [[nodiscard]] bool remove_zone(ZoneID id);
  [[nodiscard]] bool remove_region(RegionID id);

  [[nodiscard]] const Zone* zone(ZoneID id) const noexcept;
  [[nodiscard]] const Region* region(RegionID id) const noexcept;
  [[nodiscard]] const Zone* zone(std::string_view name) const noexcept;
  [[nodiscard]] const Region* region(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<RegionID> find_regions_at(RegionQuery query) const;
  [[nodiscard]] player::PermissionLayer permission_layer_for_regions(
      std::span<const RegionID> region_ids) const;
  [[nodiscard]] player::PermissionLayer permission_layer_at(RegionQuery query) const;
  [[nodiscard]] u64 revision() const noexcept;

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
  HashMap<std::string, ZoneID> zone_names_{};
  HashMap<std::string, RegionID> region_names_{};
  u64 revision_{};
};

} // namespace arv::world
