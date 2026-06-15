#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

[[nodiscard]] ArvBlockPos block_pos_to_abi(BlockPos pos) noexcept {
  return {pos.value.x, pos.value.y, pos.value.z};
}

ArvResult api_player_current_selection(
    void* context,
    ArvPlayerID player_id,
    ArvPlayerSelection* out_selection) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "players.current_selection");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_selection == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_selection is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player_id);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }

  ArvPlayerSelection selection{};
  if (player_state->selection.world.has_value()) {
    selection.flags |= ARV_PLAYER_SELECTION_HAS_WORLD;
    selection.world = player_state->selection.world->value;
  }
  if (player_state->selection.a.has_value()) {
    selection.flags |= ARV_PLAYER_SELECTION_HAS_A;
    selection.a = block_pos_to_abi(player_state->selection.a.value());
  }
  if (player_state->selection.b.has_value()) {
    selection.flags |= ARV_PLAYER_SELECTION_HAS_B;
    selection.b = block_pos_to_abi(player_state->selection.b.value());
  }
  *out_selection = selection;
  return ok(ctx);
}

uint64_t api_online_players(
    void* context,
    ArvPlayerID* out_players,
    uint64_t max_players) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || tls_thread_context != PluginThreadContext::Sync ||
      !ctx->impl->runtime.online_players) {
    return 0U;
  }
  const std::vector<PlayerID> players = ctx->impl->runtime.online_players();
  if (out_players != nullptr && max_players > 0U) {
    const uint64_t count = std::min<uint64_t>(players.size(), max_players);
    for (uint64_t index = 0; index < count; ++index) {
      out_players[index] = players[index].value;
    }
  }
  return players.size();
}

ArvStringView api_player_profile_id(void* context, ArvPlayerID player_id) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || tls_thread_context != PluginThreadContext::Sync ||
      !ctx->impl->runtime.player_stable_id) {
    return literal_view("");
  }
  const std::optional<std::string> profile_id =
      ctx->impl->runtime.player_stable_id(PlayerID{player_id});
  ctx->result_message = profile_id.value_or(std::string{});
  return stable_view(ctx->result_message);
}

ArvStringView api_player_display_name(void* context, ArvPlayerID player_id) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || tls_thread_context != PluginThreadContext::Sync ||
      !ctx->impl->runtime.player_display_name) {
    return literal_view("");
  }
  const std::optional<std::string> display_name =
      ctx->impl->runtime.player_display_name(PlayerID{player_id});
  ctx->result_message = display_name.value_or(std::string{});
  return stable_view(ctx->result_message);
}

bool api_player_find_by_profile_id(
    void* context,
    ArvStringView profile_id,
    ArvPlayerID* out_player) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || out_player == nullptr ||
      tls_thread_context != PluginThreadContext::Sync ||
      !ctx->impl->runtime.online_players ||
      !ctx->impl->runtime.player_stable_id) {
    return false;
  }
  const std::string_view target = abi_view(profile_id);
  for (const PlayerID player_id : ctx->impl->runtime.online_players()) {
    const std::optional<std::string> stable_id =
        ctx->impl->runtime.player_stable_id(player_id);
    if (stable_id.has_value() && stable_id.value() == target) {
      *out_player = player_id.value;
      return true;
    }
  }
  return false;
}

} // namespace

void fill_player_api(PluginAPIStorage& storage) {
  storage.players_api = {
      &api_player_current_selection,
      &api_online_players,
      &api_player_profile_id,
      &api_player_display_name,
      &api_player_find_by_profile_id,
  };
}

} // namespace arv::plugin
