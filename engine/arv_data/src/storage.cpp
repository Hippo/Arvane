#include "arv/data/storage.hpp"

#include "arv/data/codecs.hpp"
#include "arv/io/files.hpp"

#include <atomic>
#include <fstream>
#include <system_error>
#include <utility>

namespace arv::data {
namespace {

DataStorageError storage_error(
    DataStorageCode code,
    std::filesystem::path file,
    std::string message) {
  return {code, std::move(file), std::move(message)};
}

[[nodiscard]] bool path_segment_safe(std::string_view segment) noexcept {
  if (segment.empty() || segment == "." || segment == "..") {
    return false;
  }
  for (const char ch : segment) {
    const bool safe = (ch >= 'a' && ch <= 'z') ||
                      (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') ||
                      ch == '_' || ch == '-' || ch == '.';
    if (!safe) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] StoragePathResult path_from_resource_path(
    std::string_view path,
    std::string_view extension) {
  std::filesystem::path relative;
  usize start = 0U;
  while (start <= path.size()) {
    const usize slash = path.find('/', start);
    const usize end = slash == std::string_view::npos ? path.size() : slash;
    const std::string_view segment = path.substr(start, end - start);
    if (!path_segment_safe(segment)) {
      return StoragePathResult::failure(storage_error(
          DataStorageCode::InvalidArgument,
          {},
          "resource key path contains an unsafe segment"));
    }
    relative /= std::string{segment};
    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1U;
  }
  if (!extension.empty()) {
    relative += std::string{extension};
  }
  return StoragePathResult::success(std::move(relative));
}

[[nodiscard]] StorageSaveResult write_bytes_atomic(
    const std::filesystem::path& path,
    std::span<const u8> bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return StorageSaveResult::failure(storage_error(
        DataStorageCode::InternalError,
        path,
        "failed to create parent directory: " + error.message()));
  }

  static std::atomic<u64> temp_counter{1U};
  const std::filesystem::path temp =
      path.string() + ".tmp." +
      std::to_string(temp_counter.fetch_add(1U, std::memory_order_relaxed));
  {
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!output) {
      return StorageSaveResult::failure(storage_error(
          DataStorageCode::InternalError,
          temp,
          "failed to open temp file for writing"));
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      return StorageSaveResult::failure(storage_error(
          DataStorageCode::InternalError,
          temp,
          "failed to write temp file"));
    }
  }
  std::filesystem::rename(temp, path, error);
  if (error) {
    std::error_code cleanup_error;
    std::filesystem::remove(temp, cleanup_error);
    return StorageSaveResult::failure(storage_error(
        DataStorageCode::InternalError,
        path,
        "failed to publish file: " + error.message()));
  }
  return StorageSaveResult::success();
}

[[nodiscard]] StorageRemoveResult remove_file(const std::filesystem::path& path) {
  std::error_code error;
  const bool removed = std::filesystem::remove(path, error);
  if (error) {
    return StorageRemoveResult::failure(storage_error(
        DataStorageCode::InternalError,
        path,
        "failed to remove file: " + error.message()));
  }
  if (!removed) {
    return StorageRemoveResult::failure(storage_error(
        DataStorageCode::NotFound,
        path,
        "file was not found"));
  }
  return StorageRemoveResult::success();
}

[[nodiscard]] StorageLoadResult load_yaml_document(const std::filesystem::path& path) {
  auto text = io::read_text_file_contents(path);
  if (!text.has_value()) {
    return StorageLoadResult::failure(storage_error(
        DataStorageCode::NotFound,
        path,
        "file was not found"));
  }
  auto decoded = decode_yaml(text.value());
  if (!decoded) {
    return StorageLoadResult::failure(storage_error(
        DataStorageCode::Corrupt,
        path,
        decoded.error().describe()));
  }
  return StorageLoadResult::success(std::move(decoded.value()));
}

[[nodiscard]] StorageSaveResult save_yaml_document(
    const std::filesystem::path& path,
    const DataDocument& document) {
  auto encoded = encode_yaml(document);
  if (!encoded) {
    return StorageSaveResult::failure(storage_error(
        DataStorageCode::InvalidArgument,
        path,
        encoded.error().describe()));
  }
  const std::string& text = encoded.value();
  return write_bytes_atomic(
      path,
      std::span{reinterpret_cast<const u8*>(text.data()), text.size()});
}

[[nodiscard]] StorageLoadResult load_binary_document(const std::filesystem::path& path) {
  auto bytes = io::read_binary_file(path);
  if (!bytes.has_value()) {
    return StorageLoadResult::failure(storage_error(
        DataStorageCode::NotFound,
        path,
        "file was not found"));
  }
  auto decoded = decode_binary(bytes.value());
  if (!decoded) {
    return StorageLoadResult::failure(storage_error(
        DataStorageCode::Corrupt,
        path,
        decoded.error().describe()));
  }
  return StorageLoadResult::success(std::move(decoded.value()));
}

[[nodiscard]] StorageSaveResult save_binary_document(
    const std::filesystem::path& path,
    const DataDocument& document) {
  auto encoded = encode_binary(document);
  if (!encoded) {
    return StorageSaveResult::failure(storage_error(
        DataStorageCode::InvalidArgument,
        path,
        encoded.error().describe()));
  }
  return write_bytes_atomic(path, encoded.value());
}

[[nodiscard]] std::string world_scope(WorldID world) {
  return std::to_string(world.value);
}

[[nodiscard]] std::string block_column_scope(BlockPos pos) {
  const ChunkSectionCoord section = block_to_chunk(pos);
  const ChunkCoord column = chunk_to_column(section);
  return std::to_string(column.x) + "_" + std::to_string(column.z);
}

[[nodiscard]] std::string block_scope(BlockPos pos) {
  return std::to_string(pos.value.x) + "_" + std::to_string(pos.value.y) +
         "_" + std::to_string(pos.value.z);
}

[[nodiscard]] std::string chunk_scope(ChunkCoord pos) {
  return std::to_string(pos.x) + "_" + std::to_string(pos.z);
}

} // namespace

std::string DataStorageError::describe() const {
  if (file.empty()) {
    return message;
  }
  return file.generic_string() + ": " + message;
}

StoragePathResult safe_resource_relative_path(
    std::string_view owner_pack_id,
    const asset::ResourceID& key,
    std::string_view extension) {
  if (owner_pack_id.empty()) {
    return StoragePathResult::failure(storage_error(
        DataStorageCode::InvalidArgument,
        {},
        "owner pack id is empty"));
  }
  if (key.name_space.empty() || key.path.empty()) {
    return StoragePathResult::failure(storage_error(
        DataStorageCode::InvalidArgument,
        {},
        "resource key is empty"));
  }
  StoragePathResult relative = path_from_resource_path(key.path, extension);
  if (!relative) {
    return relative;
  }
  if (key.name_space == owner_pack_id) {
    return relative;
  }
  StoragePathResult namespace_path = path_from_resource_path(key.name_space, {});
  if (!namespace_path) {
    return namespace_path;
  }
  return StoragePathResult::success(namespace_path.value() / relative.value());
}

ConfigStorageService::ConfigStorageService(
    std::filesystem::path config_root,
    std::filesystem::path world_save_root)
    : config_root_(std::move(config_root)),
      world_save_root_(std::move(world_save_root)) {
}

StorageLoadResult ConfigStorageService::load_global_config(
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  return load_yaml_document(global_path(owner_pack_id, key));
}

StorageSaveResult ConfigStorageService::save_global_config(
    std::string_view owner_pack_id,
    const asset::ResourceID& key,
    const DataDocument& document) const {
  return save_yaml_document(global_path(owner_pack_id, key), document);
}

bool ConfigStorageService::exists_global_config(
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  std::error_code error;
  return std::filesystem::is_regular_file(global_path(owner_pack_id, key), error);
}

StorageRemoveResult ConfigStorageService::remove_global_config(
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  return remove_file(global_path(owner_pack_id, key));
}

StorageLoadResult ConfigStorageService::load_world_config(
    WorldID world,
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  return load_yaml_document(world_path(world, owner_pack_id, key));
}

StorageSaveResult ConfigStorageService::save_world_config(
    WorldID world,
    std::string_view owner_pack_id,
    const asset::ResourceID& key,
    const DataDocument& document) const {
  return save_yaml_document(world_path(world, owner_pack_id, key), document);
}

bool ConfigStorageService::exists_world_config(
    WorldID world,
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  std::error_code error;
  return std::filesystem::is_regular_file(world_path(world, owner_pack_id, key), error);
}

StorageRemoveResult ConfigStorageService::remove_world_config(
    WorldID world,
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  return remove_file(world_path(world, owner_pack_id, key));
}

std::filesystem::path ConfigStorageService::global_path(
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  auto relative = safe_resource_relative_path(owner_pack_id, key, ".yaml");
  return config_root_ / "plugins" / std::string{owner_pack_id} /
         (relative ? relative.value() : std::filesystem::path{"invalid.yaml"});
}

std::filesystem::path ConfigStorageService::world_path(
    WorldID world,
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  auto relative = safe_resource_relative_path(owner_pack_id, key, ".yaml");
  return world_save_root_ / "plugin_config" / std::to_string(world.value) /
         std::string{owner_pack_id} /
         (relative ? relative.value() : std::filesystem::path{"invalid.yaml"});
}

PersistentDataStorageService::PersistentDataStorageService(std::filesystem::path save_root)
    : save_root_(std::move(save_root)) {
}

StorageLoadResult PersistentDataStorageService::load(
    const PersistentScope& scope,
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  return load_binary_document(path_for(scope, owner_pack_id, key));
}

StorageSaveResult PersistentDataStorageService::save(
    const PersistentScope& scope,
    std::string_view owner_pack_id,
    const asset::ResourceID& key,
    const DataDocument& document) const {
  return save_binary_document(path_for(scope, owner_pack_id, key), document);
}

bool PersistentDataStorageService::exists(
    const PersistentScope& scope,
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  std::error_code error;
  return std::filesystem::is_regular_file(path_for(scope, owner_pack_id, key), error);
}

StorageRemoveResult PersistentDataStorageService::remove(
    const PersistentScope& scope,
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  return remove_file(path_for(scope, owner_pack_id, key));
}

std::filesystem::path PersistentDataStorageService::path_for(
    const PersistentScope& scope,
    std::string_view owner_pack_id,
    const asset::ResourceID& key) const {
  auto relative = safe_resource_relative_path(owner_pack_id, key, ".bin");
  std::filesystem::path root = save_root_ / "plugin_data" / std::string{owner_pack_id};
  switch (scope.kind) {
    case PersistentScopeKind::Server:
      root /= "server";
      break;
    case PersistentScopeKind::World:
      root /= "world";
      root /= world_scope(scope.world);
      break;
    case PersistentScopeKind::Player:
      root /= "players";
      root /= scope.player_stable_id.empty() ? "invalid" : scope.player_stable_id;
      break;
    case PersistentScopeKind::Entity:
      root /= "entities";
      root /= scope.entity_uuid.empty() ? "invalid" : scope.entity_uuid;
      break;
    case PersistentScopeKind::Block:
      root /= "blocks";
      root /= block_column_scope(scope.block_pos);
      root /= block_scope(scope.block_pos);
      break;
    case PersistentScopeKind::Chunk:
      root /= "chunks";
      root /= chunk_scope(scope.chunk_pos);
      break;
  }
  return root / (relative ? relative.value() : std::filesystem::path{"invalid.bin"});
}

} // namespace arv::data
