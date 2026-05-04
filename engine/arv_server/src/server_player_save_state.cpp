#include "server_runtime_internal.hpp"

#include <algorithm>
#include <utility>

namespace arv::server::detail {

void ServerPlayerSaveCache::load(std::span<const ServerPlayerSaveState> saves) {
  states_.assign(saves.begin(), saves.end());
}

bool ServerPlayerSaveCache::apply_to_player(
    std::string_view world_name,
    const ServerSession& session,
    player::PlayerState& player_state) const {
  const auto found = std::find_if(
      states_.begin(),
      states_.end(),
      [&](const ServerPlayerSaveState& save) {
        const bool profile_matches =
            !session.profile_id.empty()
                ? save.profile_id == session.profile_id
                : save.profile_name == session.profile_name;
        return profile_matches && save.world_name == world_name;
      });
  if (found == states_.end()) {
    return false;
  }

  player_state.position = found->position;
  player_state.velocity = found->velocity;
  player_state.yaw_radians = found->yaw_radians;
  player_state.pitch_radians = found->pitch_radians;
  player_state.grounded = found->grounded;
  player_state.movement_mode = found->movement_mode;
  player_state.game_mode_id = found->game_mode_id;
  player_state.selected_hotbar_slot = found->selected_hotbar_slot;
  player_state.inventory = found->inventory;
  return true;
}

void ServerPlayerSaveCache::record(
    std::string_view world_name,
    const ServerSession& session,
    const player::PlayerState& player_state) {
  ServerPlayerSaveState save;
  save.profile_id = session.profile_id;
  save.profile_name = session.profile_name;
  save.display_name = session.display_name;
  save.world_name = world_name;
  save.position = player_state.position;
  save.velocity = player_state.velocity;
  save.yaw_radians = player_state.yaw_radians;
  save.pitch_radians = player_state.pitch_radians;
  save.grounded = player_state.grounded;
  save.movement_mode = player_state.movement_mode;
  save.game_mode_id = player_state.game_mode_id;
  save.selected_hotbar_slot = player_state.selected_hotbar_slot;
  save.inventory = player_state.inventory;

  const auto found = std::find_if(
      states_.begin(),
      states_.end(),
      [&](const ServerPlayerSaveState& existing) {
        const bool profile_matches =
            !save.profile_id.empty()
                ? existing.profile_id == save.profile_id
                : existing.profile_name == save.profile_name;
        return profile_matches && existing.world_name == save.world_name;
      });
  if (found == states_.end()) {
    states_.emplace_back(std::move(save));
  } else {
    *found = std::move(save);
  }
}

const std::vector<ServerPlayerSaveState>& ServerPlayerSaveCache::states() const noexcept {
  return states_;
}

bool ServerRuntimeImpl::apply_saved_player_state(
    const ServerSession& session,
    player::PlayerState& player_state) {
  return player_saves_.apply_to_player(world_name_, session, player_state);
}

void ServerRuntimeImpl::record_player_save_state(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  player_saves_.record(world_name_, session, player_state);
}

} // namespace arv::server::detail
