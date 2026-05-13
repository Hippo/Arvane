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
  explicit constexpr Regions(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] ValueResult<RegionHandle> create_runtime_region(
      const RegionDefinition& definition) const {
    RegionHandle handle{};
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->create_runtime_region == nullptr) {
      return {Result::unsupported("regions API is unavailable"), handle};
    }
    return {
        Result{api_->regions->create_runtime_region(
            api_->regions_ctx,
            &definition,
            &handle)},
        handle,
    };
  }

  [[nodiscard]] Result register_region_flag(
      const ArvRegionFlagDefinition& definition) const {
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->register_region_flag == nullptr) {
      return Result::unsupported("regions API is unavailable");
    }
    return Result{api_->regions->register_region_flag(
        api_->regions_ctx,
        &definition)};
  }

  [[nodiscard]] ValueResult<RegionHandle> register_runtime_zone(
      const ZoneDefinition& definition) const {
    RegionHandle handle{};
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->register_runtime_zone == nullptr) {
      return {Result::unsupported("regions API is unavailable"), handle};
    }
    return {
        Result{api_->regions->register_runtime_zone(
            api_->regions_ctx,
            &definition,
            &handle)},
        handle,
    };
  }

  [[nodiscard]] Result remove_runtime_zone(RegionHandle handle) const {
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->remove_runtime_zone == nullptr) {
      return Result::unsupported("regions API is unavailable");
    }
    return Result{api_->regions->remove_runtime_zone(api_->regions_ctx, handle)};
  }

  [[nodiscard]] ValueResult<RegionRuleValue> region_flag(
      std::string_view region_id,
      std::string_view flag) const {
    ArvRegionRuleValue value{ARV_REGION_RULE_UNSET};
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->region_flag == nullptr) {
      return {Result::unsupported("regions API is unavailable"), RegionRuleValue::Unset};
    }
    Result result{api_->regions->region_flag(
        api_->regions_ctx,
        view(region_id),
        view(flag),
        &value)};
    return {result, static_cast<RegionRuleValue>(value)};
  }

  [[nodiscard]] ValueResult<RegionRuleValue> query_flag(
      const RegionQuery& query,
      std::string_view flag) const {
    ArvRegionRuleValue value{ARV_REGION_RULE_UNSET};
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->query_flag == nullptr) {
      return {Result::unsupported("regions API is unavailable"), RegionRuleValue::Unset};
    }
    Result result{api_->regions->query_flag(
        api_->regions_ctx,
        &query,
        view(flag),
        &value)};
    return {result, static_cast<RegionRuleValue>(value)};
  }

  [[nodiscard]] Result set_region_flag(
      RegionHandle region,
      std::string_view flag,
      RegionRuleValue value) const {
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->set_region_flag == nullptr) {
      return Result::unsupported("regions API is unavailable");
    }
    return Result{api_->regions->set_region_flag(
        api_->regions_ctx,
        region,
        view(flag),
        static_cast<ArvRegionRuleValue>(value))};
  }

  [[nodiscard]] Result set_zone_flag(
      RegionHandle zone,
      std::string_view flag,
      RegionRuleValue value) const {
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->set_zone_flag == nullptr) {
      return Result::unsupported("regions API is unavailable");
    }
    return Result{api_->regions->set_zone_flag(
        api_->regions_ctx,
        zone,
        view(flag),
        static_cast<ArvRegionRuleValue>(value))};
  }

  [[nodiscard]] Result remove_runtime_region(RegionHandle handle) const {
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->remove_runtime_region == nullptr) {
      return Result::unsupported("regions API is unavailable");
    }
    return Result{api_->regions->remove_runtime_region(api_->regions_ctx, handle)};
  }

  [[nodiscard]] std::vector<RegionInfo> find_regions_at(
      const RegionQuery& query) const {
    std::vector<RegionInfo> result;
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->find_regions_at == nullptr) {
      return result;
    }
    const std::uint64_t count =
        api_->regions->find_regions_at(api_->regions_ctx, &query, nullptr, 0U);
    result.resize(count);
    const std::uint64_t written = api_->regions->find_regions_at(
        api_->regions_ctx,
        &query,
        result.data(),
        result.size());
    result.resize(written);
    return result;
  }

  [[nodiscard]] std::vector<RegionInfo> active_regions(ArvPlayerID player) const {
    std::vector<RegionInfo> result;
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->active_regions == nullptr) {
      return result;
    }
    const std::uint64_t count =
        api_->regions->active_regions(api_->regions_ctx, player, nullptr, 0U);
    result.resize(count);
    const std::uint64_t written = api_->regions->active_regions(
        api_->regions_ctx,
        player,
        result.data(),
        result.size());
    result.resize(written);
    return result;
  }

  [[nodiscard]] bool is_player_in_region(
      ArvPlayerID player,
      std::string_view region_id) const {
    if (api_ == nullptr || api_->regions == nullptr ||
        api_->regions->is_player_in_region == nullptr) {
      return false;
    }
    return api_->regions->is_player_in_region(
        api_->regions_ctx,
        player,
        view(region_id));
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
