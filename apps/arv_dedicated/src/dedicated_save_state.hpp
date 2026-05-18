#pragma once

#include "dedicated_error.hpp"

#include "arv/core/expected.hpp"
#include "arv/core/location.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/io/config.hpp"
#include "arv/persistence/save.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::text {
class LanguageCatalog;
} // namespace arv::text

namespace arv::dedicated {

struct DedicatedLoadedWorld {
  std::filesystem::path server_save_root;

  persistence::LoadedSaveData loaded_save;

  world::WorldGenerationConfig active_generation_config;
  u64 world_seed{};
  world::WorldHeightSettings world_height{};
  arv::Location world_spawn{};
  TickIndex initial_tick_index{};

  std::vector<server::ServerPlayerSaveState> saved_players;
};

[[nodiscard]] Expected<DedicatedLoadedWorld, DedicatedError> load_or_create_dedicated_world(
    const io::EngineConfig& config,
    const world::WorldGenerationConfig& default_generation_config,
    const voxel::BlockRegistry& block_registry,
    u64 block_registry_hash,
    const player::ItemRegistry& item_registry,
    u64 item_registry_hash,
    const arv::text::LanguageCatalog& language,
    std::span<const persistence::RequiredPackMetadata> required_packs);

[[nodiscard]] Expected<void, DedicatedError> save_dedicated_world(
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
    std::span<const persistence::RequiredPackMetadata> required_packs);

[[nodiscard]] std::vector<persistence::SavedChunkSection> capture_saved_chunks(
    const world::WorldRuntime& world);

[[nodiscard]] std::vector<persistence::PlayerSaveData> capture_player_saves(
    const server::ServerRuntime& server,
    std::string_view world_name);

} // namespace arv::dedicated
