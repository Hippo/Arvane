#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace arv {

enum class PermissionValue {
  Unset = ARV_PERMISSION_UNSET,
  Allow = ARV_PERMISSION_ALLOW,
  Deny = ARV_PERMISSION_DENY,
};

enum class CorePermission {
  CanTakeDamage = ARV_CORE_PERMISSION_CAN_TAKE_DAMAGE,
  CanModifyWorld = ARV_CORE_PERMISSION_CAN_MODIFY_WORLD,
  CanInteract = ARV_CORE_PERMISSION_CAN_INTERACT,
  CanOpenInventory = ARV_CORE_PERMISSION_CAN_OPEN_INVENTORY,
  HasCreativeInventory = ARV_CORE_PERMISSION_HAS_CREATIVE_INVENTORY,
  CanFly = ARV_CORE_PERMISSION_CAN_FLY,
  HasCollision = ARV_CORE_PERMISSION_HAS_COLLISION,
  IsVisibleToOthers = ARV_CORE_PERMISSION_IS_VISIBLE_TO_OTHERS,
};

using CorePermissionSet = ArvCorePermissionSet;
using ResolvedCorePermissions = ArvResolvedCorePermissions;
using CustomPermissionEntry = ArvCustomPermissionEntry;

struct PermissionNodeDefinition {
  std::string_view node{};
  std::string_view description{};
  PermissionValue default_value{PermissionValue::Unset};
};

struct PermissionGroupDefinition {
  std::string_view id{};
  std::int32_t priority{};
  std::vector<std::string_view> inherits{};
  CorePermissionSet core{};
  std::vector<CustomPermissionEntry> custom_permissions{};
};

class Permissions {
public:
  explicit constexpr Permissions(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result unsupported() const {
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->unsupported == nullptr) {
      return Result::unsupported("permissions API is unavailable");
    }
    return Result{api_->permissions->unsupported(api_->permissions_ctx)};
  }

  [[nodiscard]] Result register_permission_node(
      const PermissionNodeDefinition& definition) const {
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->register_permission_node == nullptr) {
      return Result::unsupported("permissions API is unavailable");
    }
    ArvPermissionNodeDefinition abi_definition{};
    abi_definition.node = view(definition.node);
    abi_definition.description = view(definition.description);
    abi_definition.default_value =
        static_cast<ArvPermissionValue>(definition.default_value);
    return Result{api_->permissions->register_permission_node(
        api_->permissions_ctx,
        &abi_definition)};
  }

  [[nodiscard]] ValueResult<bool> check_core(
      ArvPlayerID player,
      CorePermission permission) const {
    bool allowed{};
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->check_core == nullptr) {
      return {Result::unsupported("permissions API is unavailable"), allowed};
    }
    return {
        Result{api_->permissions->check_core(
            api_->permissions_ctx,
            player,
            static_cast<ArvCorePermission>(permission),
            &allowed)},
        allowed,
    };
  }

  [[nodiscard]] ValueResult<PermissionValue> check_custom(
      ArvPlayerID player,
      std::string_view node) const {
    ArvPermissionValue value{ARV_PERMISSION_UNSET};
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->check_custom == nullptr) {
      return {Result::unsupported("permissions API is unavailable"), PermissionValue::Unset};
    }
    Result result{api_->permissions->check_custom(
        api_->permissions_ctx,
        player,
        view(node),
        &value)};
    return {result, static_cast<PermissionValue>(value)};
  }

  [[nodiscard]] ValueResult<ResolvedCorePermissions> resolved_core(
      ArvPlayerID player) const {
    ResolvedCorePermissions permissions{};
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->resolved_core == nullptr) {
      return {Result::unsupported("permissions API is unavailable"), permissions};
    }
    return {
        Result{api_->permissions->resolved_core(
            api_->permissions_ctx,
            player,
            &permissions)},
        permissions,
    };
  }

  [[nodiscard]] Result register_group(
      const PermissionGroupDefinition& definition) const {
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->register_group == nullptr) {
      return Result::unsupported("permissions API is unavailable");
    }
    std::vector<ArvStringView> inherits;
    inherits.reserve(definition.inherits.size());
    for (std::string_view inherited : definition.inherits) {
      inherits.push_back(view(inherited));
    }

    ArvPermissionGroupDefinition abi_definition{};
    abi_definition.id = view(definition.id);
    abi_definition.priority = definition.priority;
    abi_definition.inherits = inherits.data();
    abi_definition.inherit_count = inherits.size();
    abi_definition.core = definition.core;
    abi_definition.custom_permissions = definition.custom_permissions.data();
    abi_definition.custom_permission_count =
        definition.custom_permissions.size();
    return Result{api_->permissions->register_group(
        api_->permissions_ctx,
        &abi_definition)};
  }

  [[nodiscard]] ValueResult<bool> has_group(
      ArvPlayerID player,
      std::string_view group_id) const {
    bool has{};
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->has_group == nullptr) {
      return {Result::unsupported("permissions API is unavailable"), has};
    }
    return {
        Result{api_->permissions->has_group(
            api_->permissions_ctx,
            player,
            view(group_id),
            &has)},
        has,
    };
  }

  [[nodiscard]] Result add_group(
      ArvPlayerID player,
      std::string_view group_id) const {
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->add_group == nullptr) {
      return Result::unsupported("permissions API is unavailable");
    }
    return Result{api_->permissions->add_group(
        api_->permissions_ctx,
        player,
        view(group_id))};
  }

  [[nodiscard]] Result remove_group(
      ArvPlayerID player,
      std::string_view group_id) const {
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->remove_group == nullptr) {
      return Result::unsupported("permissions API is unavailable");
    }
    return Result{api_->permissions->remove_group(
        api_->permissions_ctx,
        player,
        view(group_id))};
  }

  [[nodiscard]] std::vector<std::string_view> groups(ArvPlayerID player) const {
    std::vector<std::string_view> result;
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->groups == nullptr) {
      return result;
    }
    const std::uint64_t count =
        api_->permissions->groups(api_->permissions_ctx, player, nullptr, 0U);
    std::vector<ArvStringView> groups(count);
    const std::uint64_t written = api_->permissions->groups(
        api_->permissions_ctx,
        player,
        groups.data(),
        groups.size());
    result.reserve(written);
    for (std::uint64_t index = 0; index < written && index < groups.size(); ++index) {
      result.push_back(view(groups[index]));
    }
    return result;
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
