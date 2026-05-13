#pragma once

#include "arv/asset/pack.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/data/document.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace arv::data {

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
    const asset::ResourceID& key,
    std::string_view extension);

class ConfigStorageService {
public:
  ConfigStorageService(
      std::filesystem::path config_root,
      std::filesystem::path world_save_root = {});

  [[nodiscard]] StorageLoadResult load_global_config(
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;
  [[nodiscard]] StorageSaveResult save_global_config(
      std::string_view owner_pack_id,
      const asset::ResourceID& key,
      const DataDocument& document) const;
  [[nodiscard]] bool exists_global_config(
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;
  [[nodiscard]] StorageRemoveResult remove_global_config(
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;

  [[nodiscard]] StorageLoadResult load_world_config(
      WorldID world,
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;
  [[nodiscard]] StorageSaveResult save_world_config(
      WorldID world,
      std::string_view owner_pack_id,
      const asset::ResourceID& key,
      const DataDocument& document) const;
  [[nodiscard]] bool exists_world_config(
      WorldID world,
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;
  [[nodiscard]] StorageRemoveResult remove_world_config(
      WorldID world,
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;

  [[nodiscard]] std::filesystem::path global_path(
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;
  [[nodiscard]] std::filesystem::path world_path(
      WorldID world,
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;

private:
  std::filesystem::path config_root_{};
  std::filesystem::path world_save_root_{};
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
      const asset::ResourceID& key) const;
  [[nodiscard]] StorageSaveResult save(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const asset::ResourceID& key,
      const DataDocument& document) const;
  [[nodiscard]] bool exists(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;
  [[nodiscard]] StorageRemoveResult remove(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;

  [[nodiscard]] std::filesystem::path path_for(
      const PersistentScope& scope,
      std::string_view owner_pack_id,
      const asset::ResourceID& key) const;

private:
  std::filesystem::path save_root_{};
};

} // namespace arv::data
