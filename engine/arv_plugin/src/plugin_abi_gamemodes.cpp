#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult api_register_game_mode(
    void* context,
    const ArvGameModeDefinition* definition) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (definition == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "game mode definition is null");
  }
  if (ctx->impl->options.game_modes == nullptr) {
    return make_result(ctx, ARV_RESULT_UNSUPPORTED, "game mode registry is unavailable");
  }
  if (definition->custom_permission_count != 0U &&
      definition->custom_permissions == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "game mode custom permissions is null");
  }
  const std::string id{abi_view(definition->id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  std::string base_id{abi_view(definition->base_id)};
  if (base_id.empty()) {
    base_id = std::string{player::SurvivalGameModeID};
  }
  player::CustomPermissionMap custom_permissions;
  for (usize index = 0; index < definition->custom_permission_count; ++index) {
    const ArvCustomPermissionEntry& entry = definition->custom_permissions[index];
    const std::string node{abi_view(entry.node)};
    if (!player::is_valid_custom_permission_node(node)) {
      return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
    }
    if (!permission_node_owned_by_plugin(ctx->info(), node)) {
      return make_result(
          ctx,
          ARV_RESULT_PERMISSION_DENIED,
          "permission node is not owned by plugin pack");
    }
    custom_permissions[node] = permission_value_from_abi(entry.value);
  }
  auto result = ctx->impl->options.game_modes->add_custom_mode(
      id,
      base_id,
      core_permissions_from_abi(definition->core),
      std::move(custom_permissions));
  if (!result) {
    return make_result(
        ctx,
        result.error().message.find("duplicate") != std::string::npos
            ? ARV_RESULT_DUPLICATE_ID
            : ARV_RESULT_INVALID_ARGUMENT,
        result.error().message);
  }
  return ok(ctx);
}

bool api_game_mode_exists(void* context, ArvStringView mode_id) {
  PluginCallContext* ctx = call_context(context);
  return ctx != nullptr &&
         runtime_game_modes(*ctx->impl).find(abi_view(mode_id)) != nullptr;
}

void fill_game_mode_info(
    const player::GameModeDefinition& mode,
    ArvGameModeInfo& out_info) noexcept {
  out_info = {
      stable_view(mode.id),
      stable_view(mode.id),
      mode.builtin,
      resolved_core_to_abi(mode.permissions),
  };
}

ArvResult api_game_mode_info(
    void* context,
    ArvStringView mode_id,
    ArvGameModeInfo* out_info) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_info == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_info is null");
  }
  const player::GameModeDefinition* mode =
      runtime_game_modes(*ctx->impl).find(abi_view(mode_id));
  if (mode == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "game mode not found");
  }
  fill_game_mode_info(*mode, *out_info);
  return ok(ctx);
}

ArvResult api_game_mode_current(
    void* context,
    ArvPlayerID player,
    ArvStringView* out_mode_id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "gamemodes.current");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_mode_id == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_mode_id is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  *out_mode_id = stable_view(player_state->game_mode_id);
  return ok(ctx);
}

ArvResult api_game_mode_set(
    void* context,
    ArvPlayerID player,
    ArvStringView mode_id,
    ArvResourceID reason) {
  PluginCallContext* ctx = call_context(context);
  (void)reason;
  if (ArvResult sync = require_sync_context(ctx, "gamemodes.set");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string mode{abi_view(mode_id)};
  if (runtime_game_modes(*ctx->impl).find(mode) == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "game mode not found");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  player_state->game_mode_id = mode;
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  return ok(ctx);
}

ArvResult api_game_mode_base_core_permissions(
    void* context,
    ArvStringView mode_id,
    ArvResolvedCorePermissions* out_permissions) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_permissions == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_permissions is null");
  }
  const std::optional<player::ResolvedCorePermissions> permissions =
      runtime_game_modes(*ctx->impl).permissions_for(abi_view(mode_id));
  if (!permissions.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "game mode not found");
  }
  *out_permissions = resolved_core_to_abi(permissions.value());
  return ok(ctx);
}

uint64_t api_game_mode_list(
    void* context,
    ArvGameModeInfo* out_modes,
    uint64_t max_modes) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return 0U;
  }
  std::span<const player::GameModeDefinition> modes =
      runtime_game_modes(*ctx->impl).modes();
  if (out_modes != nullptr && max_modes > 0U) {
    const uint64_t count = std::min<uint64_t>(max_modes, modes.size());
    for (uint64_t index = 0; index < count; ++index) {
      fill_game_mode_info(modes[index], out_modes[index]);
    }
  }
  return modes.size();
}

} // namespace

void fill_game_mode_api(PluginAPIStorage& storage) {
  storage.game_modes_api = {
      &api_register_game_mode,
      &api_game_mode_exists,
      &api_game_mode_info,
      &api_game_mode_current,
      &api_game_mode_set,
      &api_game_mode_base_core_permissions,
      &api_game_mode_list,
  };
}

} // namespace arv::plugin
