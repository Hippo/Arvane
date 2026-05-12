#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::asset {

enum class AssetKind : u8;

struct ResourceID {
  std::string name_space{};
  std::string path{};

  [[nodiscard]] std::string string() const;
  [[nodiscard]] bool empty() const noexcept {
    return name_space.empty() || path.empty();
  }

  [[nodiscard]] static Expected<ResourceID, std::string> parse(std::string_view text);
  [[nodiscard]] static bool valid(std::string_view text) noexcept;
};

enum class PackSide : u8 {
  Client,
  Server,
  Both,
};

struct PackCapabilities {
  bool assets{};
  bool data{};
  bool native_plugin{};
};

struct PackDependency {
  std::string id{};
  std::string version{};
};

struct PackNativeManifest {
  u32 api_version{};
  std::string entrypoint{};
  HashMap<std::string, std::filesystem::path> libraries{};
};

struct PackBuiltinManifest {
  std::string module{};
};

struct PackManifest {
  u32 schema_version{};
  std::string id{};
  std::string display_name{};
  std::string version{};
  std::string description{};
  std::vector<std::string> authors{};
  std::vector<std::string> namespaces{};
  PackSide side{PackSide::Both};
  std::string engine_min_version{};
  std::string engine_max_version{};
  std::vector<PackDependency> dependencies{};
  std::vector<std::string> load_after{};
  std::vector<std::string> load_before{};
  PackCapabilities capabilities{};
  std::optional<PackBuiltinManifest> builtin{};
  std::optional<PackNativeManifest> native{};
};

struct PackError {
  std::string pack_id{};
  std::string pack_version{};
  std::filesystem::path file{};
  std::string resource_id{};
  std::string phase{};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

struct PackSource {
  PackManifest manifest{};
  std::filesystem::path root{};
  u64 content_hash{};
};

struct PackDataFile {
  usize pack_index{};
  std::string pack_id{};
  std::string pack_version{};
  std::string name_space{};
  std::filesystem::path relative_path{};
  std::filesystem::path path{};
};

struct ResolvedPackPath {
  usize pack_index{};
  std::string pack_id{};
  std::string pack_version{};
  std::filesystem::path relative_path{};
  std::filesystem::path path{};
};

struct RequiredPack {
  std::string id{};
  std::string version{};
  u64 content_hash{};
  PackSide required_side{PackSide::Both};
  std::vector<std::string> namespaces{};
  bool native_client_code_required{};
};

using PackManifestResult = Expected<PackManifest, PackError>;
using PackManagerResult = Expected<class PackManager, PackError>;
using PackPathResult = Expected<ResolvedPackPath, PackError>;
using PackTextResult = Expected<std::string, PackError>;
using PackValidationResult = Expected<void, PackError>;

[[nodiscard]] std::string pack_side_to_string(PackSide side);
[[nodiscard]] std::optional<PackSide> pack_side_from_string(std::string_view value);
[[nodiscard]] bool is_valid_pack_id(std::string_view id) noexcept;
[[nodiscard]] bool is_safe_pack_relative_path(const std::filesystem::path& path) noexcept;

[[nodiscard]] PackManifestResult load_pack_manifest(
    const std::filesystem::path& pack_root);

class PackManager {
public:
  PackManager() = default;

  [[nodiscard]] static PackManagerResult load(const std::filesystem::path& arvane_home);
  [[nodiscard]] static PackManagerResult load_from_packs_root(
      const std::filesystem::path& packs_root);

  [[nodiscard]] const std::filesystem::path& packs_root() const noexcept {
    return packs_root_;
  }

  [[nodiscard]] std::span<const PackSource> stack() const noexcept {
    return stack_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return stack_.empty();
  }

  [[nodiscard]] const PackSource* find_pack(std::string_view id) const noexcept;

  [[nodiscard]] PackPathResult resolve_asset_path(
      std::string_view asset_id,
      AssetKind asset_kind) const;

  [[nodiscard]] PackPathResult resolve_data_path(
      std::string_view data_id,
      std::string_view extension) const;

  [[nodiscard]] std::vector<PackDataFile> data_files(
      std::string_view category,
      std::string_view extension = {}) const;

  [[nodiscard]] std::vector<PackDataFile> data_files_at(
      std::string_view relative_path) const;

  [[nodiscard]] PackTextResult load_data_text(
      std::string_view data_id,
      std::string_view extension) const;

  [[nodiscard]] PackTextResult load_merged_json_array_text(
      std::string_view category,
      std::string_view root_key) const;

  [[nodiscard]] std::vector<RequiredPack> required_packs() const;
  [[nodiscard]] PackValidationResult validate_required_packs(
      std::span<const RequiredPack> required) const;

private:
  std::filesystem::path packs_root_{};
  std::vector<PackSource> stack_{};
};

} // namespace arv::asset
