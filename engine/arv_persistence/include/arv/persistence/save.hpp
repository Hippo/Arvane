#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/location.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/entity/components.hpp"
#include "arv/persistence/region_file.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_state.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_time.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::persistence {

inline constexpr u32 CurrentSaveFormatVersion = 1;
inline constexpr std::string_view CurrentGameVersion = "0.1.0";

struct SaveMetadata {
  u32 save_format_version{CurrentSaveFormatVersion};
  std::string game_version{std::string{CurrentGameVersion}};
  // Legacy compatibility fields. New save files do not emit registry hashes.
  u64 block_registry_hash{};
  u64 item_registry_hash{};
  std::string name{};
};

struct RequiredPackMetadata {
  std::string id{};
};

struct WorldMetadata {
  std::string name{"default"};
  u64 seed{1};
  TickIndex tick_index{};
  world::WorldGenerationConfig generation_config{};
  // Compatibility mirror; generated-world height is derived from generation_config.
  i32 min_chunk_y{};
  i32 max_chunk_y{};
  // Legacy compatibility fields. New save files do not emit registry hashes.
  u64 block_registry_hash{};
  u64 item_registry_hash{};
  world::WorldClock clock{world::make_world_clock()};
  // Legacy compatibility fields. New save files do not emit saved pack metadata.
  std::vector<RequiredPackMetadata> required_packs{};
  bool content_lock_enabled{};
  std::optional<Location> spawn_point{};
};

struct PlayerSaveData {
  crypto::PlayerProfileID profile_id{};
  std::string profile_name{"local_player"};
  std::string display_name{"local_player"};
  std::string world_name{"default"};
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  bool grounded{};
  player::MovementMode movement_mode{player::MovementMode::Walking};
  std::string game_mode_id{std::string{player::SurvivalGameModeID}};
  std::string selected_skin_hash{};
  bool selected_skin_slim_model{};
  std::optional<Location> respawn_location{};
  u8 selected_hotbar_slot{};
  player::Inventory inventory{player::DefaultPlayerInventorySlotCount};
};

struct LoadedSaveData {
  bool has_save_metadata{};
  bool has_world_metadata{};
  SaveMetadata save{};
  WorldMetadata world{};
  std::vector<SavedChunkSection> chunks{};
  std::vector<PlayerSaveData> players{};
  std::vector<entity::PersistentEntityRecord> entities{};
};

struct SavedWorldSummary {
  std::string server_save_name{};
  std::string world_name{};
  std::filesystem::path server_save_root{};
  std::filesystem::path world_root{};
};

struct SavedServerSummary {
  std::string name{};
  std::string server_save_name{};
  std::string primary_world_name{};
  std::filesystem::path server_save_root{};
};

struct WorldFileOperationOptions {
  // 0 means automatic: use a capped, short-lived file-operation fan-out for
  // world load/save phases.
  u32 max_threads{};
};

[[nodiscard]] u32 resolve_world_file_operation_threads(
    WorldFileOperationOptions options = {}) noexcept;
[[nodiscard]] u64 hash_block_registry(const voxel::BlockRegistry& registry) noexcept;
[[nodiscard]] u64 hash_item_registry(const player::ItemRegistry& registry) noexcept;
void upsert_player_save(
    std::vector<PlayerSaveData>& players,
    PlayerSaveData save);
[[nodiscard]] std::vector<SavedServerSummary> discover_saved_servers(
    const std::filesystem::path& saves_root);
[[nodiscard]] std::vector<SavedWorldSummary> discover_saved_worlds(
    const std::filesystem::path& saves_root);

Expected<void, PersistenceError> save_server_root(
    const std::filesystem::path& root,
    const SaveMetadata& save,
    const WorldMetadata& world,
    std::span<const SavedChunkSection> chunks,
    std::span<const PlayerSaveData> players,
    std::span<const entity::PersistentEntityRecord> entities,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry,
    WorldFileOperationOptions options = {});
Expected<void, PersistenceError> save_server_root(
    const std::filesystem::path& root,
    const SaveMetadata& save,
    const WorldMetadata& world,
    std::span<const SavedChunkSection> chunks,
    std::span<const PlayerSaveData> players,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry,
    WorldFileOperationOptions options = {});
[[nodiscard]] Expected<LoadedSaveData, PersistenceError> load_server_root(
    const std::filesystem::path& root,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry,
    WorldFileOperationOptions options = {});
[[nodiscard]] Expected<LoadedSaveData, PersistenceError> load_server_root(
    const std::filesystem::path& root,
    std::string_view world_name,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry,
    WorldFileOperationOptions options = {});

} // namespace arv::persistence
