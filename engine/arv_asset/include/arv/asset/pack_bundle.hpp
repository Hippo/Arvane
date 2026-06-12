#pragma once

#include "arv/asset/pack_bundle_cache.hpp"
#include "arv/asset/pack.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace arv::asset {

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

using PackBundleBuildResult = Expected<ClientPackBundleBuildResult, PackBundleError>;

[[nodiscard]] PackBundleBuildResult build_client_pack_bundle(
    const PackManager& packs,
    std::string registry_snapshot_json,
    ClientPackBundleLimits limits = {});

[[nodiscard]] PackBundleResult write_client_pack_bundle_file(
    const std::filesystem::path& path,
    const ClientPackBundleBuildResult& bundle);

} // namespace arv::asset
