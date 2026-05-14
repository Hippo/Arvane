#include "arv/permissions.hpp"

namespace arv {

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
