#include "arv/permissions.hpp"

namespace arv {
namespace {

struct AbiGroupDefinition {
  std::vector<ArvStringView> inherits{};
  ArvPermissionGroupDefinition definition{};
};

[[nodiscard]] AbiGroupDefinition make_abi_group_definition(
    const PermissionGroupDefinition& definition) {
  AbiGroupDefinition abi;
  abi.inherits.reserve(definition.inherits.size());
  for (std::string_view inherited : definition.inherits) {
    abi.inherits.push_back(view(inherited));
  }
  abi.definition.id = view(definition.id);
  abi.definition.priority = definition.priority;
  abi.definition.inherits = abi.inherits.data();
  abi.definition.inherit_count = abi.inherits.size();
  abi.definition.core = definition.core;
  abi.definition.custom_permissions = definition.custom_permissions.data();
  abi.definition.custom_permission_count =
      definition.custom_permissions.size();
  return abi;
}

} // namespace

Permissions::Permissions(const ArvAPI* api) noexcept : api_(api) {
}

Result Permissions::unsupported() const {
  if (api_ == nullptr || api_->permissions == nullptr ||
      api_->permissions->unsupported == nullptr) {
    return Result::unsupported("permissions API is unavailable");
  }
  return Result{api_->permissions->unsupported(api_->permissions_ctx)};
}

Result Permissions::register_permission_node(
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

ValueResult<bool> Permissions::check_core(
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

ValueResult<PermissionValue> Permissions::check_custom(
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

ValueResult<ResolvedCorePermissions> Permissions::resolved_core(
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

Result Permissions::register_group(
    const PermissionGroupDefinition& definition) const {
  if (api_ == nullptr || api_->permissions == nullptr ||
      api_->permissions->register_group == nullptr) {
    return Result::unsupported("permissions API is unavailable");
  }
  AbiGroupDefinition abi_definition = make_abi_group_definition(definition);
  return Result{api_->permissions->register_group(
      api_->permissions_ctx,
      &abi_definition.definition)};
}

Result Permissions::upsert_group(
    const PermissionGroupDefinition& definition) const {
  if (api_ == nullptr || api_->permissions == nullptr ||
      api_->permissions->upsert_group == nullptr) {
    return Result::unsupported("permissions API upsert_group is unavailable");
  }
  AbiGroupDefinition abi_definition = make_abi_group_definition(definition);
  return Result{api_->permissions->upsert_group(
      api_->permissions_ctx,
      &abi_definition.definition)};
}

Result Permissions::remove_registered_group(std::string_view group_id) const {
  if (api_ == nullptr || api_->permissions == nullptr ||
      api_->permissions->remove_registered_group == nullptr) {
    return Result::unsupported("permissions API remove_registered_group is unavailable");
  }
  return Result{api_->permissions->remove_registered_group(
      api_->permissions_ctx,
      view(group_id))};
}

ValueResult<bool> Permissions::has_group(
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

Result Permissions::add_group(
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

Result Permissions::remove_group(
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

Result Permissions::set_groups(
    ArvPlayerID player,
    std::span<const std::string_view> groups) const {
  if (api_ == nullptr || api_->permissions == nullptr ||
      api_->permissions->set_groups == nullptr) {
    return Result::unsupported("permissions API set_groups is unavailable");
  }
  std::vector<ArvStringView> abi_groups;
  abi_groups.reserve(groups.size());
  for (std::string_view group : groups) {
    abi_groups.push_back(view(group));
  }
  return Result{api_->permissions->set_groups(
      api_->permissions_ctx,
      player,
      abi_groups.data(),
      abi_groups.size())};
}

Result Permissions::set_core_override(
    ArvPlayerID player,
    CorePermission permission,
    PermissionValue value) const {
  if (api_ == nullptr || api_->permissions == nullptr ||
      api_->permissions->set_core_override == nullptr) {
    return Result::unsupported("permissions API set_core_override is unavailable");
  }
  return Result{api_->permissions->set_core_override(
      api_->permissions_ctx,
      player,
      static_cast<ArvCorePermission>(permission),
      static_cast<ArvPermissionValue>(value))};
}

Result Permissions::set_custom_override(
    ArvPlayerID player,
    std::string_view node,
    PermissionValue value) const {
  if (api_ == nullptr || api_->permissions == nullptr ||
      api_->permissions->set_custom_override == nullptr) {
    return Result::unsupported("permissions API set_custom_override is unavailable");
  }
  return Result{api_->permissions->set_custom_override(
      api_->permissions_ctx,
      player,
      view(node),
      static_cast<ArvPermissionValue>(value))};
}

Result Permissions::set_overrides(
    ArvPlayerID player,
    CorePermissionSet core,
    std::span<const CustomPermissionEntry> custom_permissions) const {
  if (api_ == nullptr || api_->permissions == nullptr ||
      api_->permissions->set_overrides == nullptr) {
    return Result::unsupported("permissions API set_overrides is unavailable");
  }
  return Result{api_->permissions->set_overrides(
      api_->permissions_ctx,
      player,
      core,
      custom_permissions.data(),
      custom_permissions.size())};
}

std::vector<std::string_view> Permissions::groups(ArvPlayerID player) const {
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

} // namespace arv
