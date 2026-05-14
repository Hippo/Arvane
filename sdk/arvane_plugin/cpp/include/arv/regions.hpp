#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace arv {

enum class RegionRuleValue {
  Unset = ARV_REGION_RULE_UNSET,
  Allow = ARV_REGION_RULE_ALLOW,
  Deny = ARV_REGION_RULE_DENY,
};

using RegionHandle = ArvRegionHandle;
using RegionQuery = ArvRegionQuery;
using RegionDefinition = ArvRegionDefinition;
using ZoneDefinition = ArvZoneDefinition;
using RegionInfo = ArvRegionInfo;

class Regions {
public:
  explicit Regions(const ArvAPI* api) noexcept;

  [[nodiscard]] ValueResult<RegionHandle> create_runtime_region(
      const RegionDefinition& definition) const;

  [[nodiscard]] Result register_region_flag(
      const ArvRegionFlagDefinition& definition) const;

  [[nodiscard]] ValueResult<RegionHandle> register_runtime_zone(
      const ZoneDefinition& definition) const;

  [[nodiscard]] Result remove_runtime_zone(RegionHandle handle) const;

  [[nodiscard]] ValueResult<RegionRuleValue> region_flag(
      std::string_view region_id,
      std::string_view flag) const;

  [[nodiscard]] ValueResult<RegionRuleValue> query_flag(
      const RegionQuery& query,
      std::string_view flag) const;

  [[nodiscard]] Result set_region_flag(
      RegionHandle region,
      std::string_view flag,
      RegionRuleValue value) const;

  [[nodiscard]] Result set_zone_flag(
      RegionHandle zone,
      std::string_view flag,
      RegionRuleValue value) const;

  [[nodiscard]] Result remove_runtime_region(RegionHandle handle) const;

  [[nodiscard]] std::vector<RegionInfo> find_regions_at(
      const RegionQuery& query) const;

  [[nodiscard]] std::vector<RegionInfo> active_regions(ArvPlayerID player) const;

  [[nodiscard]] bool is_player_in_region(
      ArvPlayerID player,
      std::string_view region_id) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
