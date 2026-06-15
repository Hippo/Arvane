#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

[[nodiscard]] bool plain_group_id_valid(std::string_view id) noexcept {
  return command_name_valid(id);
}

[[nodiscard]] bool set_core_permission_by_abi(
    player::CorePermissionSet& permissions,
    ArvCorePermission permission,
    player::TriState value) noexcept {
  switch (permission) {
    case ARV_CORE_PERMISSION_CAN_TAKE_DAMAGE:
      permissions.can_take_damage = value;
      return true;
    case ARV_CORE_PERMISSION_CAN_MODIFY_WORLD:
      permissions.can_modify_world = value;
      return true;
    case ARV_CORE_PERMISSION_CAN_INTERACT:
      permissions.can_interact = value;
      return true;
    case ARV_CORE_PERMISSION_CAN_OPEN_INVENTORY:
      permissions.can_open_inventory = value;
      return true;
    case ARV_CORE_PERMISSION_HAS_CREATIVE_INVENTORY:
      permissions.has_creative_inventory = value;
      return true;
    case ARV_CORE_PERMISSION_CAN_FLY:
      permissions.can_fly = value;
      return true;
    case ARV_CORE_PERMISSION_HAS_COLLISION:
      permissions.has_collision = value;
      return true;
    case ARV_CORE_PERMISSION_IS_VISIBLE_TO_OTHERS:
      permissions.is_visible_to_others = value;
      return true;
  }
  return false;
}

[[nodiscard]] Expected<player::PermissionGroup, ArvResult> permission_group_from_abi(
    PluginCallContext* ctx,
    const ArvPermissionGroupDefinition* definition,
    bool require_owned_resource_id) {
  if (definition == nullptr) {
    return Expected<player::PermissionGroup, ArvResult>::failure(make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "permission group definition is null"));
  }
  const std::string id{abi_view(definition->id)};
  if (require_owned_resource_id) {
    if (auto error = validate_owned_resource_id(ctx->info(), id); error.has_value()) {
      return Expected<player::PermissionGroup, ArvResult>::failure(
          make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value()));
    }
  } else if (!plain_group_id_valid(id)) {
    return Expected<player::PermissionGroup, ArvResult>::failure(make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "permission group id must be non-empty and contain no whitespace"));
  }
  if (definition->inherit_count != 0U && definition->inherits == nullptr) {
    return Expected<player::PermissionGroup, ArvResult>::failure(make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "permission group inherits is null"));
  }
  if (definition->custom_permission_count != 0U &&
      definition->custom_permissions == nullptr) {
    return Expected<player::PermissionGroup, ArvResult>::failure(make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "permission group custom permissions is null"));
  }

  player::PermissionGroup group;
  group.id = id;
  group.priority = definition->priority;
  group.core = core_permissions_from_abi(definition->core);
  group.inherits.reserve(definition->inherit_count);
  for (usize index = 0; index < definition->inherit_count; ++index) {
    const std::string inherited{abi_view(definition->inherits[index])};
    if (!plain_group_id_valid(inherited)) {
      return Expected<player::PermissionGroup, ArvResult>::failure(make_result(
          ctx,
          ARV_RESULT_INVALID_ARGUMENT,
          "permission group inherit id must be non-empty and contain no whitespace"));
    }
    group.inherits.push_back(inherited);
  }
  for (usize index = 0; index < definition->custom_permission_count; ++index) {
    const ArvCustomPermissionEntry& entry = definition->custom_permissions[index];
    const std::string node{abi_view(entry.node)};
    if (!player::is_valid_custom_permission_node(node)) {
      return Expected<player::PermissionGroup, ArvResult>::failure(make_result(
          ctx,
          ARV_RESULT_INVALID_ARGUMENT,
          "invalid permission node"));
    }
    group.custom[node] = permission_value_from_abi(entry.value);
  }
  return Expected<player::PermissionGroup, ArvResult>::success(std::move(group));
}

void refresh_online_players(PluginManagerImpl& impl) {
  if (!impl.runtime.online_players || impl.runtime.default_world == nullptr) {
    return;
  }
  for (const PlayerID player_id : impl.runtime.online_players()) {
    if (player::PlayerState* player_state =
            impl.runtime.default_world->find_player(player_id);
        player_state != nullptr) {
      refresh_plugin_player_permissions(impl, *player_state);
    }
  }
}

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
  auto parsed = permission_group_from_abi(ctx, definition, true);
  if (!parsed) {
    return parsed.error();
  }
  const std::string id = parsed.value().id;
  const auto duplicate = std::find_if(
      ctx->impl->permission_groups.begin(),
      ctx->impl->permission_groups.end(),
      [&](const PermissionGroupRegistration& registration) {
        return registration.group.id == id;
      });
  if (duplicate != ctx->impl->permission_groups.end()) {
    return make_result(ctx, ARV_RESULT_DUPLICATE_ID, "duplicate permission group");
  }

  ctx->impl->permission_groups.push_back(
      {ctx->plugin_index, std::move(parsed.value()), false});
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

ArvResult api_permission_upsert_group(
    void* context,
    const ArvPermissionGroupDefinition* definition) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.upsert_group");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (ctx->impl->runtime.permission_groups == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_STATE, "permission group registry is unavailable");
  }
  auto parsed = permission_group_from_abi(ctx, definition, false);
  if (!parsed) {
    return parsed.error();
  }
  auto result = ctx->impl->runtime.permission_groups->upsert_group(
      std::move(parsed.value()));
  if (!result) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, result.error().message);
  }
  refresh_online_players(*ctx->impl);
  return ok(ctx);
}

ArvResult api_permission_remove_registered_group(
    void* context,
    ArvStringView group_id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.remove_registered_group");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (ctx->impl->runtime.permission_groups == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_STATE, "permission group registry is unavailable");
  }
  const std::string id{abi_view(group_id)};
  auto removed = ctx->impl->runtime.permission_groups->remove_group(id);
  if (!removed) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, removed.error().message);
  }
  if (ctx->impl->runtime.online_players && ctx->impl->runtime.default_world != nullptr) {
    for (const PlayerID player_id : ctx->impl->runtime.online_players()) {
      player::PlayerState* player_state =
          ctx->impl->runtime.default_world->find_player(player_id);
      if (player_state == nullptr) {
        continue;
      }
      auto& groups = player_state->permission_overrides.groups;
      groups.erase(std::remove(groups.begin(), groups.end(), id), groups.end());
      refresh_plugin_player_permissions(*ctx->impl, *player_state);
    }
  }
  return ok(ctx);
}

ArvResult api_permission_set_groups(
    void* context,
    ArvPlayerID player,
    const ArvStringView* groups,
    size_t group_count) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.set_groups");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (group_count != 0U && groups == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "groups is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }

  std::vector<std::string> next_groups;
  next_groups.reserve(group_count);
  for (usize index = 0; index < group_count; ++index) {
    const std::string id{abi_view(groups[index])};
    if (runtime_permission_groups(*ctx->impl).find(id) == nullptr) {
      return make_result(
          ctx,
          ARV_RESULT_NOT_FOUND,
          "permission group not found: " + id);
    }
    if (std::find(next_groups.begin(), next_groups.end(), id) == next_groups.end()) {
      next_groups.push_back(id);
    }
  }
  player_state->permission_overrides.groups = std::move(next_groups);
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  return ok(ctx);
}

ArvResult api_permission_set_core_override(
    void* context,
    ArvPlayerID player,
    ArvCorePermission permission,
    ArvPermissionValue value) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.set_core_override");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  if (!set_core_permission_by_abi(
          player_state->permission_overrides.overrides,
          permission,
          permission_value_from_abi(value))) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "unknown core permission");
  }
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  return ok(ctx);
}

ArvResult api_permission_set_custom_override(
    void* context,
    ArvPlayerID player,
    ArvStringView node,
    ArvPermissionValue value) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.set_custom_override");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string node_text{abi_view(node)};
  if (!player::is_valid_custom_permission_node(node_text)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  if (value == ARV_PERMISSION_UNSET) {
    player_state->permission_overrides.custom_overrides.erase(node_text);
  } else {
    player_state->permission_overrides.custom_overrides[node_text] =
        permission_value_from_abi(value);
  }
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  return ok(ctx);
}

ArvResult api_permission_set_overrides(
    void* context,
    ArvPlayerID player,
    ArvCorePermissionSet core,
    const ArvCustomPermissionEntry* custom_permissions,
    size_t custom_permission_count) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.set_overrides");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (custom_permission_count != 0U && custom_permissions == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "custom permissions is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  player::CustomPermissionMap next_custom;
  for (usize index = 0; index < custom_permission_count; ++index) {
    const ArvCustomPermissionEntry& entry = custom_permissions[index];
    const std::string node{abi_view(entry.node)};
    if (!player::is_valid_custom_permission_node(node)) {
      return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
    }
    if (entry.value != ARV_PERMISSION_UNSET) {
      next_custom[node] = permission_value_from_abi(entry.value);
    }
  }
  player_state->permission_overrides.overrides = core_permissions_from_abi(core);
  player_state->permission_overrides.custom_overrides = std::move(next_custom);
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  return ok(ctx);
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
      &api_permission_upsert_group,
      &api_permission_remove_registered_group,
      &api_permission_set_groups,
      &api_permission_set_core_override,
      &api_permission_set_custom_override,
      &api_permission_set_overrides,
  };
}

} // namespace arv::plugin
