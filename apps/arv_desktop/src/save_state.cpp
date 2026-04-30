#include "save_state.hpp"

#include <algorithm>
#include <utility>

namespace arv::desktop {

std::vector<persistence::SavedChunkSection> capture_saved_chunks(
    const world::WorldRuntime& world) {
  std::vector<ChunkCoord> coords = world.loaded_chunk_coords();
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
  for (const ChunkCoord coord : coords) {
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }

    persistence::SavedChunkSection saved;
    saved.coord = coord;
    saved.blocks = chunk->blocks();
    saved.lights = chunk->lights();
    saved.fluids = chunk->fluids();
    saved.lighting_valid = !chunk->is_dirty(voxel::ChunkDirtyFlags::NeedsRelight);
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
  players.reserve(session_ids.size());
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
    save.profile_name = session->profile_name;
    save.world_name = std::string{world_name};
    save.position = player_state->position;
    save.velocity = player_state->velocity;
    save.yaw_radians = player_state->yaw_radians;
    save.pitch_radians = player_state->pitch_radians;
    save.grounded = player_state->grounded;
    save.movement_mode = player_state->movement_mode;
    save.game_mode_id = player_state->game_mode_id;
    save.selected_hotbar_slot = player_state->selected_hotbar_slot;
    save.inventory = player_state->inventory;
    players.emplace_back(std::move(save));
  }

  return players;
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
          return player.profile_name == session->profile_name &&
                 player.world_name == world_name;
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
    player_state.selected_hotbar_slot = found->selected_hotbar_slot;
    player_state.inventory = found->inventory;
  }
}

} // namespace arv::desktop
