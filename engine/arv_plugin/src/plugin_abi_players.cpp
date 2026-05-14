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

} // namespace

void fill_player_api(PluginAPIStorage& storage) {
  storage.players_api = {
      &api_player_current_selection,
  };
}

} // namespace arv::plugin
