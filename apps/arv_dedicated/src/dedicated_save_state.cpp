#include "dedicated_save_state.hpp"

#include "dedicated_worldgen_loader.hpp"

#include "arv/core/log.hpp"

#include <algorithm>
#include <chrono>
#include <span>
#include <string>
#include <utility>

namespace arv::dedicated {
namespace {

[[nodiscard]] u64 generated_world_seed() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return static_cast<u64>(now == 0 ? 1 : now);
}

[[nodiscard]] server::ServerPlayerSaveState to_server_player_save(
    const persistence::PlayerSaveData& player) {
  server::ServerPlayerSaveState save;
  save.profile_name = player.profile_name;
  save.world_name = player.world_name;
  save.position = player.position;
  save.velocity = player.velocity;
  save.yaw_radians = player.yaw_radians;
  save.pitch_radians = player.pitch_radians;
  save.grounded = player.grounded;
  save.movement_mode = player.movement_mode;
  save.game_mode_id = player.game_mode_id;
  save.selected_hotbar_slot = player.selected_hotbar_slot;
  save.inventory = player.inventory;
  return save;
}

[[nodiscard]] persistence::PlayerSaveData to_persistence_player_save(
    const server::ServerPlayerSaveState& player) {
  persistence::PlayerSaveData save;
  save.profile_name = player.profile_name;
  save.world_name = player.world_name;
  save.position = player.position;
  save.velocity = player.velocity;
  save.yaw_radians = player.yaw_radians;
  save.pitch_radians = player.pitch_radians;
  save.grounded = player.grounded;
  save.movement_mode = player.movement_mode;
  save.game_mode_id = player.game_mode_id;
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
        return existing.profile_name == save.profile_name &&
               existing.world_name == save.world_name;
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
    saved.lighting_valid = !chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting);
    chunks.emplace_back(std::move(saved));
  }
  return chunks;
}

std::vector<persistence::PlayerSaveData> capture_player_saves(
    const server::ServerRuntime& server,
    std::string_view world_name) {
  std::vector<persistence::PlayerSaveData> players;
  for (const server::ServerPlayerSaveState& save : server.saved_player_states()) {
    upsert_player_save(players, to_persistence_player_save(save));
  }

  for (const SessionID session_id : server.sessions().session_ids()) {
    const server::ServerSession* session = server.sessions().find(session_id);
    if (session == nullptr) {
      continue;
    }

    const player::PlayerState* player_state =
        server.voxel_world().find_player(session->player_id);
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
    upsert_player_save(players, std::move(save));
  }

  std::sort(players.begin(), players.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.world_name != rhs.world_name) {
      return lhs.world_name < rhs.world_name;
    }
    return lhs.profile_name < rhs.profile_name;
  });
  return players;
}

Expected<DedicatedLoadedWorld, DedicatedError> load_or_create_dedicated_world(
    const io::EngineConfig& config,
    const world::WorldGenerationConfig& default_generation_config,
    const voxel::BlockRegistry& block_registry,
    u64 block_registry_hash,
    const player::ItemRegistry& item_registry,
    u64 item_registry_hash) {
  DedicatedLoadedWorld loaded_world;
  loaded_world.server_save_root = config.paths.dedicated_server_save_path();
  auto loaded_save_result = persistence::load_server_root(
      loaded_world.server_save_root,
      config.server.world,
      block_registry,
      item_registry);
  if (!loaded_save_result) {
    return Expected<DedicatedLoadedWorld, DedicatedError>::failure(
        dedicated_error("save", loaded_save_result.error().message));
  }

  loaded_world.loaded_save = std::move(loaded_save_result.value());
  loaded_world.active_generation_config = default_generation_config;
  loaded_world.world_seed = generated_world_seed();
  loaded_world.visible_chunk_y =
      visible_worldgen_chunk_y_range(loaded_world.active_generation_config);

  if (loaded_world.loaded_save.has_world_metadata) {
    loaded_world.active_generation_config =
        loaded_world.loaded_save.world.generation_config;
    loaded_world.world_seed = loaded_world.loaded_save.world.seed;
    loaded_world.visible_chunk_y = {
        loaded_world.loaded_save.world.min_chunk_y,
        loaded_world.loaded_save.world.max_chunk_y};
    loaded_world.initial_tick_index = loaded_world.loaded_save.world.tick_index;
    core::info(
        "dedicated",
        "loaded world '{}' from {}: seed={} tick={} chunks={} players={} entities={}",
        loaded_world.loaded_save.world.name,
        loaded_world.server_save_root.string(),
        loaded_world.world_seed,
        loaded_world.initial_tick_index,
        loaded_world.loaded_save.chunks.size(),
        loaded_world.loaded_save.players.size(),
        loaded_world.loaded_save.entities.size());
  } else {
    persistence::SaveMetadata save_metadata;
    save_metadata.name =
        loaded_world.loaded_save.has_save_metadata && !loaded_world.loaded_save.save.name.empty()
            ? loaded_world.loaded_save.save.name
            : std::string{"Dedicated Server"};
    save_metadata.block_registry_hash = block_registry_hash;
    save_metadata.item_registry_hash = item_registry_hash;

    persistence::WorldMetadata world_metadata;
    world_metadata.name = config.server.world;
    world_metadata.seed = loaded_world.world_seed;
    world_metadata.generation_config = loaded_world.active_generation_config;
    world_metadata.min_chunk_y = loaded_world.visible_chunk_y.first;
    world_metadata.max_chunk_y = loaded_world.visible_chunk_y.second;
    world_metadata.block_registry_hash = block_registry_hash;
    world_metadata.item_registry_hash = item_registry_hash;

    auto create_save_result = persistence::save_server_root(
        loaded_world.server_save_root,
        save_metadata,
        world_metadata,
        std::span<const persistence::SavedChunkSection>{},
        std::span<const persistence::PlayerSaveData>{},
        std::span<const entity::PersistentEntityRecord>{},
        block_registry,
        item_registry);
    if (!create_save_result) {
      return Expected<DedicatedLoadedWorld, DedicatedError>::failure(
          dedicated_error("save", create_save_result.error().message));
    }

    loaded_world.loaded_save.has_save_metadata = true;
    loaded_world.loaded_save.save = save_metadata;
    loaded_world.loaded_save.has_world_metadata = true;
    loaded_world.loaded_save.world = world_metadata;
    core::info(
        "dedicated",
        "created world '{}' at {} with seed={}",
        config.server.world,
        loaded_world.server_save_root.string(),
        loaded_world.world_seed);
  }

  if (loaded_world.loaded_save.world.block_registry_hash != 0U &&
      loaded_world.loaded_save.world.block_registry_hash != block_registry_hash) {
    core::warn(
        "save",
        "world block registry hash differs from current registry: saved={} current={}",
        loaded_world.loaded_save.world.block_registry_hash,
        block_registry_hash);
  }
  if (loaded_world.loaded_save.world.item_registry_hash != 0U &&
      loaded_world.loaded_save.world.item_registry_hash != item_registry_hash) {
    core::warn(
        "save",
        "world item registry hash differs from current registry: saved={} current={}",
        loaded_world.loaded_save.world.item_registry_hash,
        item_registry_hash);
  }

  loaded_world.saved_players.reserve(loaded_world.loaded_save.players.size());
  for (const persistence::PlayerSaveData& player : loaded_world.loaded_save.players) {
    loaded_world.saved_players.emplace_back(to_server_player_save(player));
  }

  return Expected<DedicatedLoadedWorld, DedicatedError>::success(std::move(loaded_world));
}

Expected<void, DedicatedError> save_dedicated_world(
    const io::EngineConfig& config,
    const std::filesystem::path& server_save_root,
    const persistence::LoadedSaveData& loaded_save,
    const server::ServerRuntime& server,
    const world::WorldGenerationConfig& active_generation_config,
    u64 world_seed,
    std::pair<i32, i32> visible_chunk_y,
    const voxel::BlockRegistry& block_registry,
    u64 block_registry_hash,
    const player::ItemRegistry& item_registry,
    u64 item_registry_hash) {
  const std::vector<persistence::SavedChunkSection> saved_chunks =
      capture_saved_chunks(server.voxel_world());
  const std::vector<persistence::PlayerSaveData> player_saves =
      capture_player_saves(server, config.server.world);
  const std::vector<entity::PersistentEntityRecord> saved_entities =
      server.voxel_world().persistent_entity_records();
  const persistence::SaveMetadata save_metadata{
      persistence::CurrentSaveFormatVersion,
      std::string{persistence::CurrentGameVersion},
      block_registry_hash,
      item_registry_hash,
      loaded_save.save.name.empty() ? std::string{"Dedicated Server"} : loaded_save.save.name};
  const persistence::WorldMetadata world_metadata{
      config.server.world,
      world_seed,
      server.voxel_world().tick_index(),
      active_generation_config,
      visible_chunk_y.first,
      visible_chunk_y.second,
      block_registry_hash,
      item_registry_hash};
  auto save_result = persistence::save_server_root(
      server_save_root,
      save_metadata,
      world_metadata,
      saved_chunks,
      player_saves,
      saved_entities,
      block_registry,
      item_registry);
  if (!save_result) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error("save", save_result.error().message));
  }
  core::info(
      "dedicated",
      "saved world '{}' to {}: tick={} chunks={} players={} entities={}",
      config.server.world,
      server_save_root.string(),
      world_metadata.tick_index,
      saved_chunks.size(),
      player_saves.size(),
      saved_entities.size());
  return Expected<void, DedicatedError>::success();
}

} // namespace arv::dedicated
