#include "arv/client/client_pack_sync.hpp"

#include "arv/asset/pack_bundle_cache.hpp"

#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] ClientPackSyncError pack_sync_error(
    protocol::PackSyncStatus status,
    std::string reason,
    std::string failed_hash = {}) {
  return ClientPackSyncError{status, std::move(reason), std::move(failed_hash)};
}

} // namespace

ClientPackSyncManifestPlanResult prepare_client_pack_sync_manifest(
    const std::filesystem::path& cache_root,
    protocol::ClientboundPackManifest manifest) {
  if (manifest.pack_sync_protocol_version != protocol::PackSyncProtocolVersion) {
    return ClientPackSyncManifestPlanResult::failure(pack_sync_error(
        protocol::PackSyncStatus::VerificationFailed,
        "Server pack sync protocol is not supported."));
  }

  auto cleanup = asset::clean_client_pack_cache_temporary_files(cache_root);
  if (!cleanup) {
    return ClientPackSyncManifestPlanResult::failure(pack_sync_error(
        protocol::PackSyncStatus::VerificationFailed,
        cleanup.error().message));
  }

  if (!asset::is_valid_bundle_hash(manifest.registry_hash)) {
    return ClientPackSyncManifestPlanResult::failure(pack_sync_error(
        protocol::PackSyncStatus::VerificationFailed,
        "Server sent an invalid registry snapshot hash."));
  }

  ClientPackSyncManifestPlan plan;
  plan.registry_hash = std::move(manifest.registry_hash);
  HashSet<std::string> seen_bundle_hashes;

  for (protocol::PackBundleDescriptor& descriptor : manifest.required_bundles) {
    if (!asset::is_valid_bundle_hash(descriptor.bundle_hash) ||
        descriptor.compressed_size == 0U ||
        (manifest.max_bundle_size != 0U &&
         descriptor.compressed_size > manifest.max_bundle_size)) {
      return ClientPackSyncManifestPlanResult::failure(pack_sync_error(
          protocol::PackSyncStatus::VerificationFailed,
          "Server sent an invalid pack manifest.",
          descriptor.bundle_hash));
    }
    if (!seen_bundle_hashes.insert(descriptor.bundle_hash).second) {
      return ClientPackSyncManifestPlanResult::failure(pack_sync_error(
          protocol::PackSyncStatus::VerificationFailed,
          "Server sent duplicate pack bundle descriptors.",
          descriptor.bundle_hash));
    }

    plan.required_bundle_hashes.push_back(descriptor.bundle_hash);
    if (asset::client_pack_cache_contains(cache_root, descriptor.bundle_hash)) {
      plan.loaded_bundle_hashes.push_back(descriptor.bundle_hash);
      plan.loaded_bundle_paths.push_back(
          asset::client_pack_cache_path(cache_root, descriptor.bundle_hash));
      plan.status.already_cached_bundle_hashes.push_back(descriptor.bundle_hash);
      continue;
    }

    plan.missing_bundle_hashes.push_back(descriptor.bundle_hash);
    plan.status.needed_bundle_hashes.push_back(descriptor.bundle_hash);
    ClientPackSyncDownloadPlan download;
    download.descriptor = std::move(descriptor);
    plan.downloads.push_back(std::move(download));
  }

  plan.status.status =
      plan.downloads.empty()
          ? protocol::PackSyncStatus::AlreadyCached
          : protocol::PackSyncStatus::NeedsDownload;
  return ClientPackSyncManifestPlanResult::success(std::move(plan));
}

} // namespace arv::client
