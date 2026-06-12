#include "arv/asset/pack.hpp"
#include "arv/asset/pack_bundle.hpp"
#include "arv/client/client_pack_sync.hpp"
#include "arv/crypto/hash.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

std::string bundle_hash(char digit) {
  return std::string(arv::crypto::hash_256_size * 2U, digit);
}

void write_pack_manifest(
    const std::filesystem::path& packs_root,
    std::string_view id,
    std::string_view name_space) {
  write_text(
      packs_root / std::string{id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{id} + "\"\n"
                            "display_name: \"" +
          std::string{id} + "\"\n"
                            "version: \"1.0.0\"\n"
                            "namespaces:\n"
                            "  - \"" +
          std::string{name_space} + "\"\n"
                                    "side: \"both\"\n"
                                    "dependencies: []\n"
                                    "load_after: []\n"
                                    "load_before: []\n"
                                    "capabilities:\n"
                                    "  assets: true\n"
                                    "  data: true\n"
                                    "  native_plugin: false\n");
}

arv::protocol::PackBundleDescriptor descriptor_for(
    const arv::asset::ClientPackBundleBuildResult& bundle) {
  return {
      "sync_pack",
      "Sync Pack",
      "1.0.0",
      bundle.bundle_hash,
      static_cast<arv::u64>(bundle.apack_bytes.size()),
      bundle.uncompressed_size};
}

} // namespace

TEST(ClientPackSyncPlanTests, RejectsInvalidAndDuplicateManifestEntries) {
  ScopedTempDir temp("arvane_pack_sync_plan_invalid_");

  arv::protocol::ClientboundPackManifest invalid;
  invalid.registry_hash = bundle_hash('c');
  invalid.required_bundles.push_back({
      "bad",
      "Bad",
      "1.0.0",
      "not-a-hash",
      1U,
      1U});
  auto invalid_plan = arv::client::prepare_client_pack_sync_manifest(
      temp.path / "cache",
      std::move(invalid));
  ASSERT_FALSE(invalid_plan);
  EXPECT_EQ(invalid_plan.error().status, arv::protocol::PackSyncStatus::VerificationFailed);
  EXPECT_EQ(invalid_plan.error().failed_hash, "not-a-hash");

  arv::protocol::ClientboundPackManifest invalid_registry;
  invalid_registry.registry_hash = "not-a-hash";
  auto invalid_registry_plan = arv::client::prepare_client_pack_sync_manifest(
      temp.path / "cache",
      std::move(invalid_registry));
  ASSERT_FALSE(invalid_registry_plan);
  EXPECT_EQ(
      invalid_registry_plan.error().status,
      arv::protocol::PackSyncStatus::VerificationFailed);

  const std::string hash = bundle_hash('a');
  arv::protocol::ClientboundPackManifest duplicate;
  duplicate.registry_hash = bundle_hash('d');
  duplicate.required_bundles.push_back({"one", "One", "1.0.0", hash, 4U, 8U});
  duplicate.required_bundles.push_back({"two", "Two", "1.0.0", hash, 4U, 8U});
  auto duplicate_plan = arv::client::prepare_client_pack_sync_manifest(
      temp.path / "cache",
      std::move(duplicate));
  ASSERT_FALSE(duplicate_plan);
  EXPECT_EQ(duplicate_plan.error().status, arv::protocol::PackSyncStatus::VerificationFailed);
  EXPECT_EQ(duplicate_plan.error().failed_hash, hash);
}

TEST(ClientPackSyncPlanTests, UsesVerifiedCacheAndCleansStaleTemporaryEntries) {
  ScopedTempDir temp("arvane_pack_sync_plan_cache_");
  const std::filesystem::path packs = temp.path / "packs";
  write_text(packs / "enabled.yaml", "enabled:\n  - \"sync_pack\"\n");
  write_pack_manifest(packs, "sync_pack", "sync_pack");
  write_text(packs / "sync_pack/data/sync_pack/blocks.json", "{\"blocks\":[]}\n");
  write_text(packs / "sync_pack/assets/sync_pack/textures/block/test.png", "png");

  auto pack_manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(pack_manager) << pack_manager.error().describe();
  auto bundle = arv::asset::build_client_pack_bundle(
      pack_manager.value(),
      "{\"format_version\":1,\"blocks\":[]}");
  ASSERT_TRUE(bundle) << bundle.error().message;

  const std::filesystem::path bundle_file = temp.path / "server" / "bundle.apack";
  auto write_bundle = arv::asset::write_client_pack_bundle_file(bundle_file, bundle.value());
  ASSERT_TRUE(write_bundle) << write_bundle.error().message;

  const std::filesystem::path cache_root = temp.path / "client_home" / "cache" / "server-packs";
  auto install = arv::asset::install_client_pack_bundle_from_file(
      bundle_file,
      cache_root,
      bundle.value().bundle_hash);
  ASSERT_TRUE(install) << install.error().message;

  const std::filesystem::path stale_partial = cache_root / "tmp" / "stale.partial";
  write_text(stale_partial / "manifest.json", "partial\n");

  arv::protocol::ClientboundPackManifest manifest;
  manifest.registry_hash = bundle.value().registry_hash;
  manifest.max_bundle_size = static_cast<arv::u64>(bundle.value().apack_bytes.size());
  manifest.required_bundles.push_back(descriptor_for(bundle.value()));

  auto plan = arv::client::prepare_client_pack_sync_manifest(
      cache_root,
      std::move(manifest));
  ASSERT_TRUE(plan) << plan.error().reason;
  EXPECT_EQ(plan.value().status.status, arv::protocol::PackSyncStatus::AlreadyCached);
  EXPECT_EQ(plan.value().status.already_cached_bundle_hashes.size(), 1U);
  EXPECT_TRUE(plan.value().status.needed_bundle_hashes.empty());
  EXPECT_TRUE(plan.value().downloads.empty());
  ASSERT_EQ(plan.value().loaded_bundle_paths.size(), 1U);
  EXPECT_EQ(plan.value().loaded_bundle_paths.front(), install.value().cache_path);
  EXPECT_FALSE(std::filesystem::exists(stale_partial));
}
