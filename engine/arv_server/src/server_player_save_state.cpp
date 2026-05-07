#include "server_runtime_internal.hpp"

#include "arv/protocol/skins.hpp"

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
  player_state.permission_overrides = found->permission_overrides;
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
  save.permission_overrides = player_state.permission_overrides;
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
    save.selected_skin_hash = found->selected_skin_hash;
    *found = std::move(save);
  }
}

const std::vector<ServerPlayerSaveState>& ServerPlayerSaveCache::states() const noexcept {
  return states_;
}

std::string ServerPlayerSaveCache::selected_skin_hash_for(
    std::string_view world_name,
    const ServerSession& session) const {
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
  if (found == states_.end() ||
      !protocol::is_valid_skin_hash(found->selected_skin_hash)) {
    return {};
  }
  return found->selected_skin_hash;
}

void ServerPlayerSaveCache::set_selected_skin_hash_for(
    std::string_view world_name,
    const ServerSession& session,
    std::string skin_hash) {
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
  if (!skin_hash.empty() && !protocol::is_valid_skin_hash(skin_hash)) {
    skin_hash.clear();
  }
  if (found != states_.end()) {
    found->selected_skin_hash = std::move(skin_hash);
    return;
  }
  ServerPlayerSaveState save;
  save.profile_id = session.profile_id;
  save.profile_name = session.profile_name;
  save.display_name = session.display_name;
  save.world_name = std::string{world_name};
  save.selected_skin_hash = std::move(skin_hash);
  states_.push_back(std::move(save));
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

std::string ServerRuntimeImpl::selected_skin_hash_for_session(
    const ServerSession& session) const {
  return player_saves_.selected_skin_hash_for(world_name_, session);
}

void ServerRuntimeImpl::set_selected_skin_hash_for_session(
    const ServerSession& session,
    std::string skin_hash) {
  player_saves_.set_selected_skin_hash_for(world_name_, session, std::move(skin_hash));
}

} // namespace arv::server::detail
