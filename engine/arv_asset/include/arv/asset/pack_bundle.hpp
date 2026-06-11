#pragma once

#include "arv/asset/pack.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::asset {

inline constexpr std::string_view ClientPackBundleMagic = "APACK";
inline constexpr u32 ClientPackBundleFormatVersion = 1U;

struct PackBundleError {
  std::string message{};
};

struct ClientPackBundleLimits {
  u64 max_compressed_size{256ULL * 1024ULL * 1024ULL};
  u64 max_uncompressed_size{512ULL * 1024ULL * 1024ULL};
  u64 max_file_size{64ULL * 1024ULL * 1024ULL};
  u32 max_files{100'000U};
};

struct ClientPackBundleFileInfo {
  std::string path{};
  u64 size{};
  std::string hash{};
};

struct ClientPackBundleBuildResult {
  std::string bundle_hash{};
  std::string registry_hash{};
  std::string manifest_json{};
  std::vector<u8> apack_bytes{};
  u64 compressed_size{};
  u64 uncompressed_size{};
  std::vector<ClientPackBundleFileInfo> files{};
};

struct ClientPackBundleInstallResult {
  std::string bundle_hash{};
  std::filesystem::path cache_path{};
  bool cache_hit{};
};

using PackBundleBuildResult = Expected<ClientPackBundleBuildResult, PackBundleError>;
using PackBundleInstallResult = Expected<ClientPackBundleInstallResult, PackBundleError>;
using PackBundleResult = Expected<void, PackBundleError>;

[[nodiscard]] bool is_valid_bundle_hash(std::string_view hash) noexcept;
[[nodiscard]] std::filesystem::path client_pack_cache_path(
    const std::filesystem::path& cache_root,
    std::string_view bundle_hash);
[[nodiscard]] bool client_pack_cache_contains(
    const std::filesystem::path& cache_root,
    std::string_view bundle_hash);
[[nodiscard]] bool is_client_bundle_relative_path_allowed(std::string_view path) noexcept;

[[nodiscard]] PackBundleBuildResult build_client_pack_bundle(
    const PackManager& packs,
    std::string registry_snapshot_json,
    ClientPackBundleLimits limits = {});

[[nodiscard]] PackBundleResult write_client_pack_bundle_file(
    const std::filesystem::path& path,
    const ClientPackBundleBuildResult& bundle);

[[nodiscard]] PackBundleInstallResult install_client_pack_bundle_from_file(
    const std::filesystem::path& apack_path,
    const std::filesystem::path& cache_root,
    std::string_view expected_bundle_hash,
    ClientPackBundleLimits limits = {});

} // namespace arv::asset
