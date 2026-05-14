#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult api_register_permission_node(
    void* context,
    const ArvPermissionNodeDefinition* definition) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (definition == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "permission node definition is null");
  }
  const std::string node{abi_view(definition->node)};
  if (!player::is_valid_custom_permission_node(node)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
  }
  if (!permission_node_owned_by_plugin(ctx->info(), node)) {
    return make_result(
        ctx,
        ARV_RESULT_PERMISSION_DENIED,
        "permission node is not owned by plugin pack");
  }
  const auto duplicate = std::find_if(
      ctx->impl->permission_nodes.begin(),
      ctx->impl->permission_nodes.end(),
      [&](const PermissionNodeRegistration& registration) {
        return registration.node == node;
      });
  if (duplicate != ctx->impl->permission_nodes.end()) {
    return make_result(ctx, ARV_RESULT_DUPLICATE_ID, "duplicate permission node");
  }
  ctx->impl->permission_nodes.push_back({
      ctx->plugin_index,
      node,
      abi_string(definition->description),
      permission_value_from_abi(definition->default_value),
  });
  return ok(ctx);
}

ArvResult api_permission_check_core(
    void* context,
    ArvPlayerID player,
    ArvCorePermission permission,
    bool* out_allowed) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.check_core");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_allowed == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_allowed is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  *out_allowed =
      resolved_core_permission_value(player_state->resolved_permissions.core, permission);
  return ok(ctx);
}

ArvResult api_permission_check_custom(
    void* context,
    ArvPlayerID player,
    ArvStringView node,
    ArvPermissionValue* out_value) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.check_custom");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_value == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_value is null");
  }
  const std::string node_text{abi_view(node)};
  if (!player::is_valid_custom_permission_node(node_text)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  *out_value = permission_value_to_abi(
      player_state->resolved_permissions.custom_value(node_text));
  return ok(ctx);
}

ArvResult api_permission_resolved_core(
    void* context,
    ArvPlayerID player,
    ArvResolvedCorePermissions* out_permissions) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.resolved_core");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_permissions == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_permissions is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  *out_permissions = resolved_core_to_abi(player_state->resolved_permissions.core);
  return ok(ctx);
}

ArvResult api_permission_register_group(
    void* context,
    const ArvPermissionGroupDefinition* definition) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (definition == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "permission group definition is null");
  }
  const std::string id{abi_view(definition->id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (definition->inherit_count != 0U && definition->inherits == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "permission group inherits is null");
  }
  if (definition->custom_permission_count != 0U &&
      definition->custom_permissions == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "permission group custom permissions is null");
  }
  const auto duplicate = std::find_if(
      ctx->impl->permission_groups.begin(),
      ctx->impl->permission_groups.end(),
      [&](const PermissionGroupRegistration& registration) {
        return registration.group.id == id;
      });
  if (duplicate != ctx->impl->permission_groups.end()) {
    return make_result(ctx, ARV_RESULT_DUPLICATE_ID, "duplicate permission group");
  }

  player::PermissionGroup group;
  group.id = id;
  group.priority = definition->priority;
  group.core = core_permissions_from_abi(definition->core);
  group.inherits.reserve(definition->inherit_count);
  for (usize index = 0; index < definition->inherit_count; ++index) {
    group.inherits.push_back(abi_string(definition->inherits[index]));
  }
  for (usize index = 0; index < definition->custom_permission_count; ++index) {
    const ArvCustomPermissionEntry& entry = definition->custom_permissions[index];
    const std::string node{abi_view(entry.node)};
    if (!player::is_valid_custom_permission_node(node)) {
      return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
    }
    group.custom[node] = permission_value_from_abi(entry.value);
  }
  ctx->impl->permission_groups.push_back({ctx->plugin_index, std::move(group), false});
  return ok(ctx);
}

ArvResult api_permission_has_group(
    void* context,
    ArvPlayerID player,
    ArvStringView group_id,
    bool* out_has_group) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.has_group");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_has_group == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_has_group is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  const std::string id{abi_view(group_id)};
  *out_has_group = std::find(
                       player_state->permission_overrides.groups.begin(),
                       player_state->permission_overrides.groups.end(),
                       id) != player_state->permission_overrides.groups.end();
  return ok(ctx);
}

ArvResult api_permission_add_group(
    void* context,
    ArvPlayerID player,
    ArvStringView group_id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.add_group");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string id{abi_view(group_id)};
  if (runtime_permission_groups(*ctx->impl).find(id) == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "permission group not found");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  auto& groups = player_state->permission_overrides.groups;
  if (std::find(groups.begin(), groups.end(), id) == groups.end()) {
    groups.push_back(id);
    refresh_plugin_player_permissions(*ctx->impl, *player_state);
  }
  return ok(ctx);
}

ArvResult api_permission_remove_group(
    void* context,
    ArvPlayerID player,
    ArvStringView group_id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.remove_group");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  const std::string id{abi_view(group_id)};
  auto& groups = player_state->permission_overrides.groups;
  const usize old_size = groups.size();
  groups.erase(std::remove(groups.begin(), groups.end(), id), groups.end());
  if (groups.size() != old_size) {
    refresh_plugin_player_permissions(*ctx->impl, *player_state);
  }
  return ok(ctx);
}

uint64_t api_permission_groups(
    void* context,
    ArvPlayerID player,
    ArvStringView* out_groups,
    uint64_t max_groups) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || tls_thread_context != PluginThreadContext::Sync) {
    return 0U;
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return 0U;
  }
  const std::vector<std::string>& groups = player_state->permission_overrides.groups;
  if (out_groups != nullptr && max_groups > 0U) {
    const uint64_t count = std::min<uint64_t>(groups.size(), max_groups);
    for (uint64_t index = 0; index < count; ++index) {
      out_groups[index] = stable_view(groups[index]);
    }
  }
  return groups.size();
}

} // namespace

void fill_permission_api(PluginAPIStorage& storage) {
  storage.permissions_api = {
      &unsupported_no_arg,
      &api_register_permission_node,
      &api_permission_check_core,
      &api_permission_check_custom,
      &api_permission_resolved_core,
      &api_permission_register_group,
      &api_permission_has_group,
      &api_permission_add_group,
      &api_permission_remove_group,
      &api_permission_groups,
  };
}

} // namespace arv::plugin
