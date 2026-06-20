#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/pack_sync.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::client {

struct ClientPackSyncError {
  protocol::PackSyncStatus status{protocol::PackSyncStatus::VerificationFailed};
  std::string reason{};
  std::string failed_hash{};
};

struct ClientPackSyncDownloadPlan {
  protocol::PackBundleDescriptor descriptor{};
};

struct ClientPackSyncManifestPlan {
  std::string registry_hash{};
  std::vector<std::string> required_bundle_hashes{};
  std::vector<std::string> loaded_bundle_hashes{};
  std::vector<std::filesystem::path> loaded_bundle_paths{};
  std::vector<std::string> missing_bundle_hashes{};
  std::vector<ClientPackSyncDownloadPlan> downloads{};
  protocol::ServerboundPackSyncStatus status{};
};

using ClientPackSyncManifestPlanResult =
    Expected<ClientPackSyncManifestPlan, ClientPackSyncError>;

[[nodiscard]] ClientPackSyncManifestPlanResult prepare_client_pack_sync_manifest(
    const std::filesystem::path& cache_root,
    protocol::ClientboundPackManifest manifest);

[[nodiscard]] std::vector<std::filesystem::path> copy_client_pack_cache_paths(
    std::span<const std::filesystem::path> paths);

[[nodiscard]] bool client_pack_cache_paths_equal(
    std::span<const std::filesystem::path> left,
    std::span<const std::filesystem::path> right);

[[nodiscard]] bool client_pack_cache_identity_matches(
    std::string_view active_registry_snapshot_json,
    std::span<const std::filesystem::path> active_pack_cache_paths,
    std::string_view registry_snapshot_json,
    std::span<const std::filesystem::path> pack_cache_paths);

} // namespace arv::client
