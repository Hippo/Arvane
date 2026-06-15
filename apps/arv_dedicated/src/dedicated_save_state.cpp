#include "dedicated_save_state.hpp"

#include "dedicated_worldgen_loader.hpp"

#include "arv/core/log.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/text/language.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <utility>

namespace arv::dedicated {
namespace {

[[nodiscard]] u64 generated_world_seed() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return static_cast<u64>(now == 0 ? 1 : now);
}

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& object,
    std::string_view key) noexcept {
  return object.get(key);
}

[[nodiscard]] std::string string_value_or(
    const io::DataValue& object,
    std::string_view key,
    std::string fallback = {}) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && value->kind() == io::DataKind::String
             ? std::string{value->as_string_or({})}
             : std::move(fallback);
}

[[nodiscard]] io::DataValue pack_lock_to_value(const DedicatedPackLockInfo& info) {
  io::DataValue root = io::DataValue::object();
  root.set("format_version", static_cast<u64>(1U));
  io::DataValue packs = io::DataValue::array();
  for (const DedicatedPackLockPack& pack : info.packs) {
    io::DataValue entry = io::DataValue::object();
    entry.set("pack_id", pack.id);
    entry.set("version", pack.version);
    entry.set("content_hash", pack.content_hash);
    entry.set("client_bundle_hash", pack.client_bundle_hash);
    packs.push(std::move(entry));
  }
  root.set("packs", std::move(packs));
  root.set("registry_hash", info.registry_hash);
  root.set("created_with_arvane_version", info.created_with_arvane_version);
  return root;
}

[[nodiscard]] Expected<DedicatedPackLockInfo, DedicatedError> pack_lock_from_value(
    const io::DataValue& root) {
  if (root.kind() != io::DataKind::Object) {
    return Expected<DedicatedPackLockInfo, DedicatedError>::failure(
        dedicated_error("pack-lock", "pack-lock.json root must be an object"));
  }
  const io::DataValue* format = member(root, "format_version");
  if (format == nullptr || !io::is_integer_value(*format) ||
      format->as_u64_or(0U) != 1U) {
    return Expected<DedicatedPackLockInfo, DedicatedError>::failure(
        dedicated_error("pack-lock", "unsupported pack-lock format_version"));
  }

  DedicatedPackLockInfo info;
  info.registry_hash = string_value_or(root, "registry_hash");
  info.created_with_arvane_version =
      string_value_or(root, "created_with_arvane_version");
  const io::DataValue* packs = member(root, "packs");
  const io::DataValue::Array* pack_array =
      packs == nullptr ? nullptr : packs->array_if();
  if (pack_array == nullptr) {
    return Expected<DedicatedPackLockInfo, DedicatedError>::failure(
        dedicated_error("pack-lock", "pack-lock.json packs must be an array"));
  }
  info.packs.reserve(pack_array->size());
  for (const io::DataValue& node : *pack_array) {
    if (node.kind() != io::DataKind::Object) {
      return Expected<DedicatedPackLockInfo, DedicatedError>::failure(
          dedicated_error("pack-lock", "pack-lock pack entries must be objects"));
    }
    DedicatedPackLockPack pack;
    pack.id = string_value_or(node, "pack_id");
    pack.version = string_value_or(node, "version");
    pack.content_hash = string_value_or(node, "content_hash");
    pack.client_bundle_hash = string_value_or(node, "client_bundle_hash");
    info.packs.push_back(std::move(pack));
  }
  return Expected<DedicatedPackLockInfo, DedicatedError>::success(std::move(info));
}

[[nodiscard]] bool pack_lock_matches(
    const DedicatedPackLockInfo& expected,
    const DedicatedPackLockInfo& actual,
    std::string& mismatch) {
  if (actual.registry_hash != expected.registry_hash) {
    mismatch = "registry hash mismatch";
    return false;
  }
  if (actual.packs.size() != expected.packs.size()) {
    mismatch = "pack count mismatch";
    return false;
  }
  for (usize index = 0; index < expected.packs.size(); ++index) {
    const DedicatedPackLockPack& lhs = expected.packs[index];
    const DedicatedPackLockPack& rhs = actual.packs[index];
    if (lhs.id != rhs.id || lhs.version != rhs.version ||
        lhs.content_hash != rhs.content_hash ||
        lhs.client_bundle_hash != rhs.client_bundle_hash) {
      mismatch = "pack mismatch for index " + std::to_string(index);
      return false;
    }
  }
  return true;
}

[[nodiscard]] Expected<std::string, DedicatedError> read_text_file(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return Expected<std::string, DedicatedError>::failure(
        dedicated_error("pack-lock", "failed to open " + path.string()));
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return Expected<std::string, DedicatedError>::success(buffer.str());
}

[[nodiscard]] Expected<void, DedicatedError> write_text_file(
    const std::filesystem::path& path,
    std::string_view text) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error("pack-lock", "failed to create pack-lock directory: " + error.message()));
  }
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error("pack-lock", "failed to open pack-lock for write: " + path.string()));
  }
  output << text;
  output.flush();
  if (!output) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error("pack-lock", "failed to write pack-lock: " + path.string()));
  }
  return Expected<void, DedicatedError>::success();
}

[[nodiscard]] server::ServerPlayerSaveState to_server_player_save(
    const persistence::PlayerSaveData& player) {
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
  save.selected_skin_hash = player.selected_skin_hash;
  save.selected_skin_slim_model = player.selected_skin_slim_model;
  save.respawn_location = player.respawn_location;
  save.selected_hotbar_slot = player.selected_hotbar_slot;
  save.inventory = player.inventory;
  return save;
}

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
  save.selected_skin_hash = player.selected_skin_hash;
  save.selected_skin_slim_model = player.selected_skin_slim_model;
  save.respawn_location = player.respawn_location;
  save.selected_hotbar_slot = player.selected_hotbar_slot;
  save.inventory = player.inventory;
  return save;
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
  std::vector<persistence::PlayerSaveData> players;
  for (const server::ServerPlayerSaveState& save : server.saved_player_states()) {
    persistence::upsert_player_save(players, to_persistence_player_save(save));
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
    const auto previous = std::find_if(
        server.saved_player_states().begin(),
        server.saved_player_states().end(),
        [&](const server::ServerPlayerSaveState& existing) {
          const bool profile_matches =
              !save.profile_id.empty()
                  ? existing.profile_id == save.profile_id
                  : existing.profile_name == save.profile_name;
          return profile_matches && existing.world_name == save.world_name;
        });
    if (previous != server.saved_player_states().end()) {
      save.selected_skin_hash = previous->selected_skin_hash;
      save.selected_skin_slim_model = previous->selected_skin_slim_model;
    }
    save.respawn_location = player_state->respawn_location;
    save.selected_hotbar_slot = player_state->selected_hotbar_slot;
    save.inventory = player_state->inventory;
    persistence::upsert_player_save(players, std::move(save));
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

Expected<DedicatedLoadedWorld, DedicatedError> load_or_create_dedicated_world(
    const io::EngineConfig& config,
    const world::WorldGenerationConfig& default_generation_config,
    const voxel::BlockRegistry& block_registry,
    u64 block_registry_hash,
    const player::ItemRegistry& item_registry,
    u64 item_registry_hash,
    const arv::text::LanguageCatalog& language,
    std::span<const persistence::RequiredPackMetadata> required_packs) {
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
  loaded_world.world_height =
      world::world_height_settings_from_generation_config(loaded_world.active_generation_config);
  loaded_world.world_spawn =
      world::generate_world_spawn_location(
          loaded_world.active_generation_config,
          loaded_world.world_seed);

  if (loaded_world.loaded_save.has_world_metadata) {
    loaded_world.active_generation_config =
        world::refresh_generation_config_for_current_dimension(
            loaded_world.loaded_save.world.generation_config,
            default_generation_config);
    loaded_world.world_seed = loaded_world.loaded_save.world.seed;
    loaded_world.world_height =
        world::world_height_settings_from_generation_config(loaded_world.active_generation_config);
    loaded_world.world_spawn =
        loaded_world.loaded_save.world.spawn_point.has_value()
            ? loaded_world.loaded_save.world.spawn_point.value()
            : world::generate_world_spawn_location(
                  loaded_world.active_generation_config,
                  loaded_world.world_seed);
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
            : arv::text::text_or_fallback(
                  &language,
                  "server.save.default_name",
                  "Dedicated Server");
    save_metadata.block_registry_hash = block_registry_hash;
    save_metadata.item_registry_hash = item_registry_hash;

    persistence::WorldMetadata world_metadata;
    world_metadata.name = config.server.world;
    world_metadata.seed = loaded_world.world_seed;
    world_metadata.generation_config = loaded_world.active_generation_config;
    world_metadata.min_chunk_y = loaded_world.world_height.min_chunk_y;
    world_metadata.max_chunk_y = loaded_world.world_height.max_chunk_y;
    world_metadata.block_registry_hash = block_registry_hash;
    world_metadata.item_registry_hash = item_registry_hash;
    world_metadata.required_packs.assign(required_packs.begin(), required_packs.end());
    world_metadata.content_lock_enabled = true;
    world_metadata.spawn_point = loaded_world.world_spawn;

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
  if (loaded_world.loaded_save.has_world_metadata &&
      loaded_world.loaded_save.world.content_lock_enabled &&
      !loaded_world.loaded_save.world.required_packs.empty()) {
    for (const persistence::RequiredPackMetadata& expected :
         loaded_world.loaded_save.world.required_packs) {
      const auto found = std::find_if(
          required_packs.begin(),
          required_packs.end(),
          [&](const persistence::RequiredPackMetadata& actual) {
            return actual.id == expected.id;
          });
      if (found == required_packs.end()) {
        return Expected<DedicatedLoadedWorld, DedicatedError>::failure(
            dedicated_error(
                "packs",
                "world requires pack '" + expected.id + "', but it is not active"));
      }
    }
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
    world::WorldHeightSettings world_height,
    const voxel::BlockRegistry& block_registry,
    u64 block_registry_hash,
    const player::ItemRegistry& item_registry,
    u64 item_registry_hash,
    const arv::text::LanguageCatalog& language,
    std::span<const persistence::RequiredPackMetadata> required_packs) {
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
      loaded_save.save.name.empty()
          ? arv::text::text_or_fallback(
                &language,
                "server.save.default_name",
                "Dedicated Server")
          : loaded_save.save.name};
  persistence::WorldMetadata world_metadata{
      config.server.world,
      world_seed,
      server.voxel_world().tick_index(),
      active_generation_config,
      world_height.min_chunk_y,
      world_height.max_chunk_y,
      block_registry_hash,
      item_registry_hash,
      server.voxel_world().clock()};
  world_metadata.required_packs.assign(required_packs.begin(), required_packs.end());
  world_metadata.content_lock_enabled = true;
  world_metadata.spawn_point = server.voxel_world().spawn_point();
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

Expected<void, DedicatedError> check_or_create_world_pack_lock(
    const std::filesystem::path& server_root,
    std::string_view world_name,
    const DedicatedPackLockInfo& lock_info) {
  const std::filesystem::path lock_path =
      server_root / "worlds" / std::string{world_name} / "pack-lock.json";
  std::error_code error;
  if (!std::filesystem::exists(lock_path, error)) {
    io::DataValue root = pack_lock_to_value(lock_info);
    auto encoded = io::encode_json_value(root);
    if (!encoded) {
      return Expected<void, DedicatedError>::failure(
          dedicated_error("pack-lock", "failed to encode pack-lock.json: " + encoded.error().describe()));
    }
    auto write = write_text_file(lock_path, encoded.value());
    if (!write) {
      return write;
    }
    core::info(
        "dedicated",
        "created pack lock for world '{}' at {}",
        world_name,
        lock_path.string());
    return Expected<void, DedicatedError>::success();
  }
  if (error) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error("pack-lock", "failed to inspect pack-lock.json: " + error.message()));
  }

  auto text = read_text_file(lock_path);
  if (!text) {
    return Expected<void, DedicatedError>::failure(text.error());
  }
  auto decoded = io::decode_json_value(text.value());
  if (!decoded) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error("pack-lock", "failed to parse pack-lock.json: " + decoded.error().describe()));
  }
  auto loaded = pack_lock_from_value(decoded.value().root);
  if (!loaded) {
    return Expected<void, DedicatedError>::failure(loaded.error());
  }
  std::string mismatch;
  if (!pack_lock_matches(lock_info, loaded.value(), mismatch)) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error(
            "pack-lock",
            "world pack-lock.json does not match current server packs: " + mismatch));
  }
  return Expected<void, DedicatedError>::success();
}

} // namespace arv::dedicated
