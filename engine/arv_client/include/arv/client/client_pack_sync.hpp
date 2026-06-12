#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/pack_sync.hpp"

#include <filesystem>
#include <string>
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

} // namespace arv::client
