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
  explicit Permissions(const ArvAPI* api) noexcept;

  [[nodiscard]] Result unsupported() const;

  [[nodiscard]] Result register_permission_node(
      const PermissionNodeDefinition& definition) const;

  [[nodiscard]] ValueResult<bool> check_core(
      ArvPlayerID player,
      CorePermission permission) const;

  [[nodiscard]] ValueResult<PermissionValue> check_custom(
      ArvPlayerID player,
      std::string_view node) const;

  [[nodiscard]] ValueResult<ResolvedCorePermissions> resolved_core(
      ArvPlayerID player) const;

  [[nodiscard]] Result register_group(
      const PermissionGroupDefinition& definition) const;

  [[nodiscard]] ValueResult<bool> has_group(
      ArvPlayerID player,
      std::string_view group_id) const;

  [[nodiscard]] Result add_group(
      ArvPlayerID player,
      std::string_view group_id) const;

  [[nodiscard]] Result remove_group(
      ArvPlayerID player,
      std::string_view group_id) const;

  [[nodiscard]] std::vector<std::string_view> groups(ArvPlayerID player) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
