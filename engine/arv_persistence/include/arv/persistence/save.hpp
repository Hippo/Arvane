#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_state.hpp"
#include "arv/persistence/region_file.hpp"
#include "arv/world/world_generation.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace arv::persistence {

inline constexpr u32 CurrentSaveFormatVersion = 1;
inline constexpr std::string_view CurrentGameVersion = "0.1.0";

struct SaveMetadata {
  u32 save_format_version{CurrentSaveFormatVersion};
  std::string game_version{std::string{CurrentGameVersion}};
  u64 block_registry_hash{};
  u64 item_registry_hash{};
};

struct WorldMetadata {
  std::string name{"default"};
  u64 seed{1};
  TickIndex tick_index{};
  world::WorldGenerationConfig generation_config{};
  i32 min_chunk_y{};
  i32 max_chunk_y{};
  u64 block_registry_hash{};
  u64 item_registry_hash{};
};

struct PlayerSaveData {
  std::string profile_name{"local_player"};
  std::string world_name{"default"};
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  bool grounded{};
  player::MovementMode movement_mode{player::MovementMode::Walking};
  std::string game_mode_id{std::string{player::SurvivalGameModeID}};
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

[[nodiscard]] u64 hash_block_registry(const voxel::BlockRegistry& registry) noexcept;
[[nodiscard]] u64 hash_item_registry(const player::ItemRegistry& registry) noexcept;

Expected<void, PersistenceError> save_server_root(
    const std::filesystem::path& root,
    const SaveMetadata& save,
    const WorldMetadata& world,
    std::span<const SavedChunkSection> chunks,
    std::span<const PlayerSaveData> players,
    std::span<const entity::PersistentEntityRecord> entities,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry);
Expected<void, PersistenceError> save_server_root(
    const std::filesystem::path& root,
    const SaveMetadata& save,
    const WorldMetadata& world,
    std::span<const SavedChunkSection> chunks,
    std::span<const PlayerSaveData> players,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry);
[[nodiscard]] Expected<LoadedSaveData, PersistenceError> load_server_root(
    const std::filesystem::path& root,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry);

} // namespace arv::persistence
