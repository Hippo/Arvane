#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/io/data_document.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace arv::io {

struct ResourceKey {
  std::string name_space{};
  std::string path{};

  [[nodiscard]] bool empty() const noexcept {
    return name_space.empty() || path.empty();
  }
};

enum class DataStorageCode : u8 {
  NotFound,
  InvalidArgument,
  PermissionDenied,
  Unsupported,
  Corrupt,
  InternalError,
};

struct DataStorageError {
  DataStorageCode code{DataStorageCode::InternalError};
  std::filesystem::path file{};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

using StorageLoadResult = Expected<DataDocument, DataStorageError>;
using StorageSaveResult = Expected<void, DataStorageError>;
using StorageRemoveResult = Expected<void, DataStorageError>;
using StoragePathResult = Expected<std::filesystem::path, DataStorageError>;

[[nodiscard]] StoragePathResult safe_resource_relative_path(
    std::string_view owner_pack_id,
    const ResourceKey& key,
    std::string_view extension);

class ConfigStorageService {
public:
  explicit ConfigStorageService(std::filesystem::path config_root);

  [[nodiscard]] StorageLoadResult load_config(
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] StorageSaveResult save_config(
      std::string_view owner_pack_id,
      const ResourceKey& key,
      const DataDocument& document) const;
  [[nodiscard]] bool exists_config(
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] StorageRemoveResult remove_config(
      std::string_view owner_pack_id,
      const ResourceKey& key) const;

  [[nodiscard]] StorageLoadResult load_data(
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] StorageSaveResult save_data(
      std::string_view owner_pack_id,
      const ResourceKey& key,
      const DataDocument& document) const;
  [[nodiscard]] bool exists_data(
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] StorageRemoveResult remove_data(
      std::string_view owner_pack_id,
      const ResourceKey& key) const;

  [[nodiscard]] StorageLoadResult load_player_data(
      std::string_view profile_id,
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] StorageSaveResult save_player_data(
      std::string_view profile_id,
      std::string_view owner_pack_id,
      const ResourceKey& key,
      const DataDocument& document) const;
  [[nodiscard]] bool exists_player_data(
      std::string_view profile_id,
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] StorageRemoveResult remove_player_data(
      std::string_view profile_id,
      std::string_view owner_pack_id,
      const ResourceKey& key) const;

  [[nodiscard]] std::filesystem::path config_path(
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] std::filesystem::path data_path(
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] std::filesystem::path player_data_path(
      std::string_view profile_id,
      std::string_view owner_pack_id,
      const ResourceKey& key) const;

private:
  std::filesystem::path config_root_{};
};

enum class PersistentScopeKind : u8 {
  Server,
  World,
  Player,
  Entity,
  Block,
  Chunk,
};

struct PersistentScope {
  PersistentScopeKind kind{PersistentScopeKind::Server};
  WorldID world{};
  PlayerID player{};
  std::string player_stable_id{};
  std::string entity_uuid{};
  BlockPos block_pos{};
  ChunkCoord chunk_pos{};
};

class PersistentDataStorageService {
public:
  explicit PersistentDataStorageService(std::filesystem::path save_root);

  [[nodiscard]] StorageLoadResult load(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] StorageSaveResult save(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const ResourceKey& key,
      const DataDocument& document) const;
  [[nodiscard]] bool exists(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const ResourceKey& key) const;
  [[nodiscard]] StorageRemoveResult remove(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const ResourceKey& key) const;

  [[nodiscard]] std::filesystem::path path_for(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const ResourceKey& key) const;

private:
  std::filesystem::path save_root_{};
};

} // namespace arv::io
