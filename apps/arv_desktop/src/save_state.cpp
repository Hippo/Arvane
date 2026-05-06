#include "save_state.hpp"

#include <algorithm>
#include <utility>

namespace arv::desktop {
namespace {

[[nodiscard]] persistence::PlayerSaveData to_persistence_player_save(
    const server::ServerPlayerSaveState& player) {
  persistence::PlayerSaveData save;
  save.profile_id = player.profile_id;
  save.profile_name = player.profile_name;
  save.display_name = player.display_name;
  save.world_name = player.world_name;
  save.position = player.position;
  save.velocity = player.velocity;
  save.yaw_radians = player.yaw_radians;
  save.pitch_radians = player.pitch_radians;
  save.grounded = player.grounded;
  save.movement_mode = player.movement_mode;
  save.game_mode_id = player.game_mode_id;
  save.permission_overrides = player.permission_overrides;
  save.selected_hotbar_slot = player.selected_hotbar_slot;
  save.inventory = player.inventory;
  return save;
}

void upsert_player_save(
    std::vector<persistence::PlayerSaveData>& players,
    persistence::PlayerSaveData save) {
  const auto found = std::find_if(
      players.begin(),
      players.end(),
      [&](const persistence::PlayerSaveData& existing) {
        const bool profile_matches =
            !save.profile_id.empty()
                ? existing.profile_id == save.profile_id
                : existing.profile_name == save.profile_name;
        return profile_matches && existing.world_name == save.world_name;
      });
  if (found == players.end()) {
    players.emplace_back(std::move(save));
  } else {
    *found = std::move(save);
  }
}

} // namespace

std::vector<persistence::SavedChunkSection> capture_saved_chunks(
    const world::WorldRuntime& world) {
  std::vector<ChunkSectionCoord> coords = world.loaded_chunk_coords();
  std::sort(coords.begin(), coords.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.value.x != rhs.value.x) {
      return lhs.value.x < rhs.value.x;
    }
    if (lhs.value.z != rhs.value.z) {
      return lhs.value.z < rhs.value.z;
    }
    return lhs.value.y < rhs.value.y;
  });

  std::vector<persistence::SavedChunkSection> chunks;
  chunks.reserve(coords.size());
  for (const ChunkSectionCoord coord : coords) {
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }

    persistence::SavedChunkSection saved;
    saved.coord = coord;
    saved.blocks = chunk->blocks();
    saved.lights = chunk->lights();
    saved.fluids = chunk->fluids();
    saved.lighting_valid = !chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting);
    chunks.emplace_back(std::move(saved));
  }
  return chunks;
}

std::vector<persistence::PlayerSaveData> capture_player_saves(
    const server::ServerRuntime& server,
    std::string_view world_name) {
  std::vector<SessionID> session_ids = server.sessions().session_ids();
  std::sort(session_ids.begin(), session_ids.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.value < rhs.value;
  });

  std::vector<persistence::PlayerSaveData> players;
  players.reserve(server.saved_player_states().size() + session_ids.size());
  for (const server::ServerPlayerSaveState& save : server.saved_player_states()) {
    upsert_player_save(players, to_persistence_player_save(save));
  }

  for (const SessionID session_id : session_ids) {
    const server::ServerSession* session = server.sessions().find(session_id);
    if (session == nullptr) {
      continue;
    }

    const player::PlayerState* player_state = server.voxel_world().find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    persistence::PlayerSaveData save;
    save.profile_id = session->profile_id;
    save.profile_name = session->profile_name;
    save.display_name = session->display_name;
    save.world_name = std::string{world_name};
    save.position = player_state->position;
    save.velocity = player_state->velocity;
    save.yaw_radians = player_state->yaw_radians;
    save.pitch_radians = player_state->pitch_radians;
    save.grounded = player_state->grounded;
    save.movement_mode = player_state->movement_mode;
    save.game_mode_id = player_state->game_mode_id;
    save.permission_overrides = player_state->permission_overrides;
    save.selected_hotbar_slot = player_state->selected_hotbar_slot;
    save.inventory = player_state->inventory;
    upsert_player_save(players, std::move(save));
  }

  std::sort(players.begin(), players.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.world_name != rhs.world_name) {
      return lhs.world_name < rhs.world_name;
    }
    if (lhs.profile_id != rhs.profile_id) {
      return lhs.profile_id < rhs.profile_id;
    }
    return lhs.profile_name < rhs.profile_name;
  });
  return players;
}

std::vector<entity::PersistentEntityRecord> capture_saved_entities(
    const world::WorldRuntime& world) {
  return world.persistent_entity_records();
}

void apply_loaded_player_saves(
    server::ServerRuntime& server,
    std::string_view world_name,
    const std::vector<persistence::PlayerSaveData>& loaded_players) {
  for (const SessionID session_id : server.sessions().session_ids()) {
    const server::ServerSession* session = server.sessions().find(session_id);
    if (session == nullptr) {
      continue;
    }

    const auto found = std::find_if(
        loaded_players.begin(),
        loaded_players.end(),
        [&](const persistence::PlayerSaveData& player) {
          const bool profile_matches =
              !session->profile_id.empty()
                  ? player.profile_id == session->profile_id
                  : player.profile_name == session->profile_name;
          return profile_matches && player.world_name == world_name;
        });
    if (found == loaded_players.end()) {
      continue;
    }

    player::PlayerState& player_state = server.voxel_world().ensure_player(session->player_id);
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
  }
}

std::vector<server::ServerPlayerSaveState> to_server_player_saves(
    const std::vector<persistence::PlayerSaveData>& players) {
  std::vector<server::ServerPlayerSaveState> saves;
  saves.reserve(players.size());
  for (const persistence::PlayerSaveData& player : players) {
    server::ServerPlayerSaveState save;
    save.profile_id = player.profile_id;
    save.profile_name = player.profile_name;
    save.display_name = player.display_name;
    save.world_name = player.world_name;
    save.position = player.position;
    save.velocity = player.velocity;
    save.yaw_radians = player.yaw_radians;
    save.pitch_radians = player.pitch_radians;
    save.grounded = player.grounded;
    save.movement_mode = player.movement_mode;
    save.game_mode_id = player.game_mode_id;
    save.permission_overrides = player.permission_overrides;
    save.selected_hotbar_slot = player.selected_hotbar_slot;
    save.inventory = player.inventory;
    saves.emplace_back(std::move(save));
  }
  return saves;
}

} // namespace arv::desktop
