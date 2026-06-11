#include "arv/asset/pack.hpp"
#include "arv/asset/pack_bundle.hpp"
#include "arv/asset/resource.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

void write_enabled(const std::filesystem::path& packs_root, std::string_view text) {
  write_text(packs_root / "enabled.yaml", text);
}

void write_pack_manifest(
    const std::filesystem::path& packs_root,
    std::string_view id,
    std::string_view namespaces,
    std::string_view side = "both",
    std::string_view capabilities = "assets: true\n  data: true\n  native_plugin: false\n",
    std::string_view extra = "") {
  write_text(
      packs_root / std::string{id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{id} + "\"\n"
                            "display_name: \"" +
          std::string{id} + "\"\n"
                            "version: \"1.0.0\"\n"
                            "namespaces:\n" +
          std::string{namespaces} +
          "side: \"" + std::string{side} + "\"\n"
                                           "dependencies: []\n"
                                           "load_after: []\n"
                                           "load_before: []\n"
                                           "capabilities:\n  " +
          std::string{capabilities} +
          std::string{extra});
}

std::string native_library_name(std::string_view stem) {
#if defined(_WIN32)
  return std::string{stem} + ".dll";
#elif defined(__APPLE__)
  return "lib" + std::string{stem} + ".dylib";
#else
  return "lib" + std::string{stem} + ".so";
#endif
}

std::string native_manifest_extra(
    std::string_view library_path,
    std::string_view api_version = "1",
    std::string_view entrypoint = "arvane_plugin_main") {
  return "native:\n"
         "  api_version: " +
         std::string{api_version} + "\n"
                                    "  entrypoint: \"" +
         std::string{entrypoint} + "\"\n"
                                   "  libraries:\n"
                                   "    " +
         arv::asset::native_plugin_platform_key() + ": \"" +
         std::string{library_path} + "\"\n";
}

void write_binary(
    const std::filesystem::path& path,
    std::span<const arv::u8> bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool contains_bundle_file(
    std::span<const arv::asset::ClientPackBundleFileInfo> files,
    std::string_view path) {
  return std::find_if(
             files.begin(),
             files.end(),
             [&](const arv::asset::ClientPackBundleFileInfo& file) {
               return file.path == path;
             }) != files.end();
}

} // namespace

TEST(PackResourceIDTests, ParsesValidAndRejectsUnsafeIds) {
  auto dirt = arv::asset::ResourceID::parse("arvane:dirt");
  ASSERT_TRUE(dirt) << dirt.error();
  EXPECT_EQ(dirt.value().name_space, "arvane");
  EXPECT_EQ(dirt.value().path, "dirt");

  auto model = arv::asset::ResourceID::parse("arvane:model/entity/pig");
  ASSERT_TRUE(model) << model.error();

  EXPECT_FALSE(arv::asset::ResourceID::parse("arvane").has_value());
  EXPECT_FALSE(arv::asset::ResourceID::parse(":dirt").has_value());
  EXPECT_FALSE(arv::asset::ResourceID::parse("Arvane:dirt").has_value());
  EXPECT_FALSE(arv::asset::ResourceID::parse("arvane:../dirt").has_value());
  EXPECT_FALSE(arv::asset::ResourceID::parse("arvane:block//dirt").has_value());
}

TEST(PackManifestTests, LoadsValidManifestAndRejectsUnsafeNativePath) {
  ScopedTempDir temp("arvane_pack_manifest_");
  const std::filesystem::path packs = temp.path / "packs";
  write_pack_manifest(packs, "arvane_base", "  - \"arvane\"\n");

  auto manifest = arv::asset::load_pack_manifest(packs / "arvane_base");
  ASSERT_TRUE(manifest) << manifest.error().describe();
  EXPECT_EQ(manifest.value().id, "arvane_base");
  EXPECT_EQ(manifest.value().namespaces.front(), "arvane");
  EXPECT_FALSE(manifest.value().builtin.has_value());

  write_pack_manifest(
      packs,
      "builtin_base",
      "  - \"builtin_base\"\n",
      "both",
      "assets: true\n  data: true\n  native_plugin: false\n",
      "builtin:\n"
      "  module: \"arv_base\"\n");
  auto builtin = arv::asset::load_pack_manifest(packs / "builtin_base");
  ASSERT_TRUE(builtin) << builtin.error().describe();
  ASSERT_TRUE(builtin.value().builtin.has_value());
  EXPECT_EQ(builtin.value().builtin->module, "arv_base");

  write_pack_manifest(
      packs,
      "native_bad",
      "  - \"native_bad\"\n",
      "both",
      "assets: false\n  data: false\n  native_plugin: true\n",
      native_manifest_extra("../outside.so"));
  auto bad = arv::asset::load_pack_manifest(packs / "native_bad");
  EXPECT_FALSE(bad);
  EXPECT_NE(bad.error().describe().find("unsafe native library path"), std::string::npos);
}

TEST(PackManifestTests, LoadsNativeMetadataAndRejectsInvalidNativeDeclarations) {
  ScopedTempDir temp("arvane_pack_native_manifest_");
  const std::filesystem::path packs = temp.path / "packs";
  const std::string library =
      "native/" + arv::asset::native_plugin_platform_key() + "/" +
      native_library_name("example_plugin_pack");
  write_pack_manifest(
      packs,
      "example_plugin_pack",
      "  - \"example_plugin_pack\"\n",
      "server",
      "assets: true\n  data: true\n  native_plugin: true\n",
      native_manifest_extra(library));

  auto manifest = arv::asset::load_pack_manifest(packs / "example_plugin_pack");
  ASSERT_TRUE(manifest) << manifest.error().describe();
  ASSERT_TRUE(manifest.value().native.has_value());
  EXPECT_EQ(manifest.value().native->api_version, 1U);
  EXPECT_EQ(manifest.value().native->entrypoint, "arvane_plugin_main");
  EXPECT_EQ(
      manifest.value().native->libraries.at(arv::asset::native_plugin_platform_key()),
      std::filesystem::path{library});

  write_pack_manifest(
      packs,
      "native_no_capability",
      "  - \"native_no_capability\"\n",
      "server",
      "assets: true\n  data: true\n  native_plugin: false\n",
      native_manifest_extra(library));
  auto no_capability = arv::asset::load_pack_manifest(packs / "native_no_capability");
  EXPECT_FALSE(no_capability);
  EXPECT_NE(no_capability.error().describe().find("capabilities.native_plugin"), std::string::npos);

  write_pack_manifest(
      packs,
      "native_bad_version",
      "  - \"native_bad_version\"\n",
      "server",
      "assets: true\n  data: true\n  native_plugin: true\n",
      native_manifest_extra(library, "999"));
  auto bad_version = arv::asset::load_pack_manifest(packs / "native_bad_version");
  EXPECT_FALSE(bad_version);
  EXPECT_NE(bad_version.error().describe().find("unsupported native.api_version"), std::string::npos);

  write_pack_manifest(
      packs,
      "native_bad_entry",
      "  - \"native_bad_entry\"\n",
      "server",
      "assets: true\n  data: true\n  native_plugin: true\n",
      native_manifest_extra(library, "1", "bad entry"));
  auto bad_entry = arv::asset::load_pack_manifest(packs / "native_bad_entry");
  EXPECT_FALSE(bad_entry);
  EXPECT_NE(bad_entry.error().describe().find("native.entrypoint"), std::string::npos);

  write_pack_manifest(
      packs,
      "native_missing_platform",
      "  - \"native_missing_platform\"\n",
      "server",
      "assets: true\n  data: true\n  native_plugin: true\n",
      "native:\n"
      "  api_version: 1\n"
      "  entrypoint: \"arvane_plugin_main\"\n"
      "  libraries:\n"
      "    unsupported-platform: \"native/unsupported/libexample.so\"\n");
  auto missing_platform = arv::asset::load_pack_manifest(packs / "native_missing_platform");
  EXPECT_FALSE(missing_platform);
  EXPECT_NE(missing_platform.error().describe().find("missing current platform"), std::string::npos);
}

TEST(PackManagerTests, OrdersDependenciesAndResolvesAssetOverrides) {
  ScopedTempDir temp("arvane_pack_order_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"override_pack\"\n  - \"arvane_base\"\n");
  write_pack_manifest(packs, "arvane_base", "  - \"arvane\"\n");
  write_text(
      packs / "override_pack" / "pack.yaml",
      "schema_version: 1\n"
      "id: \"override_pack\"\n"
      "display_name: \"Override Pack\"\n"
      "version: \"1.0.0\"\n"
      "namespaces:\n"
      "  - \"arvane\"\n"
      "side: \"client\"\n"
      "dependencies:\n"
      "  - id: \"arvane_base\"\n"
      "    version: \">=1.0.0\"\n"
      "load_after: []\n"
      "load_before: []\n"
      "capabilities:\n"
      "  assets: true\n"
      "  data: false\n"
      "  native_plugin: false\n");
  write_text(packs / "arvane_base/assets/arvane/textures/block/dirt.png", "base");
  write_text(packs / "override_pack/assets/arvane/textures/block/dirt.png", "override");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  ASSERT_EQ(manager.value().stack().size(), 2U);
  EXPECT_EQ(manager.value().stack()[0].manifest.id, "arvane_base");
  EXPECT_EQ(manager.value().stack()[1].manifest.id, "override_pack");

  auto resolved = manager.value().resolve_asset_path(
      "arvane:texture/block/dirt",
      arv::asset::AssetKind::Texture);
  ASSERT_TRUE(resolved) << resolved.error().describe();
  EXPECT_EQ(resolved.value().pack_id, "override_pack");
}

TEST(PackManagerTests, RejectsDependencyCycles) {
  ScopedTempDir temp("arvane_pack_cycle_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"pack_a\"\n  - \"pack_b\"\n");
  write_text(
      packs / "pack_a/pack.yaml",
      "schema_version: 1\nid: \"pack_a\"\ndisplay_name: \"A\"\nversion: \"1.0.0\"\n"
      "namespaces:\n  - \"pack_a\"\nside: \"both\"\n"
      "dependencies:\n  - id: \"pack_b\"\nload_after: []\nload_before: []\n"
      "capabilities:\n  assets: false\n  data: true\n  native_plugin: false\n");
  write_text(
      packs / "pack_b/pack.yaml",
      "schema_version: 1\nid: \"pack_b\"\ndisplay_name: \"B\"\nversion: \"1.0.0\"\n"
      "namespaces:\n  - \"pack_b\"\nside: \"both\"\n"
      "dependencies:\n  - id: \"pack_a\"\nload_after: []\nload_before: []\n"
      "capabilities:\n  assets: false\n  data: true\n  native_plugin: false\n");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  EXPECT_FALSE(manager);
  EXPECT_NE(manager.error().describe().find("cycle"), std::string::npos);
}

TEST(PackDataTests, RejectsDuplicateDefinitionsWithoutOverride) {
  ScopedTempDir temp("arvane_pack_data_duplicate_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"arvane_base\"\n  - \"override_pack\"\n");
  write_pack_manifest(packs, "arvane_base", "  - \"arvane\"\n");
  write_pack_manifest(packs, "override_pack", "  - \"arvane\"\n");
  write_text(
      packs / "arvane_base/data/arvane/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:stone\"}]}\n");
  write_text(
      packs / "override_pack/data/arvane/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:stone\"}]}\n");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  auto merged = manager.value().load_merged_json_array_text("blocks", "blocks");
  EXPECT_FALSE(merged);
  EXPECT_NE(merged.error().describe().find("duplicate definition"), std::string::npos);
}

TEST(PackDataTests, LoadsCondensedRootDataFiles) {
  ScopedTempDir temp("arvane_pack_data_root_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"arvane_base\"\n");
  write_pack_manifest(packs, "arvane_base", "  - \"arvane\"\n");
  write_text(
      packs / "arvane_base/data/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:stone\"}]}\n");
  write_text(
      packs / "arvane_base/data/commands.yaml",
      "schema_version: 1\naliases: []\n");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  auto merged = manager.value().load_merged_json_array_text("blocks", "blocks");
  ASSERT_TRUE(merged) << merged.error().describe();
  EXPECT_NE(merged.value().find("\"name\": \"arvane:stone\""), std::string::npos);

  auto resolved = manager.value().resolve_data_path("arvane:blocks", ".json");
  ASSERT_TRUE(resolved) << resolved.error().describe();
  EXPECT_EQ(resolved.value().relative_path.generic_string(), "data/blocks.json");

  const auto command_files = manager.value().data_files_at("commands.yaml");
  ASSERT_EQ(command_files.size(), 1U);
  EXPECT_EQ(command_files.front().relative_path.generic_string(), "data/commands.yaml");
}

TEST(PackConfigTests, ResolvesRootConfigFiles) {
  ScopedTempDir temp("arvane_pack_config_root_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"arv_mason\"\n");
  write_pack_manifest(
      packs,
      "arv_mason",
      "  - \"arv_mason\"\n",
      "server",
      "assets: false\n  data: false\n  config: true\n  native_plugin: false\n");
  write_text(
      packs / "arv_mason/config/messages.yaml",
      "prefix: \"<red>Mason<gray>></>\"\n");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();

  auto resolved = manager.value().resolve_config_path("arv_mason:messages", ".yaml");
  ASSERT_TRUE(resolved) << resolved.error().describe();
  EXPECT_EQ(resolved.value().relative_path.generic_string(), "config/messages.yaml");

  auto text = manager.value().load_config_text("arv_mason:messages", ".yaml");
  ASSERT_TRUE(text) << text.error().describe();
  EXPECT_NE(text.value().find("<red>Mason<gray>></>"), std::string::npos);
}

TEST(PackDataTests, AllowsExplicitOverrideAndRejectsNamespaceSquatting) {
  ScopedTempDir temp("arvane_pack_data_override_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"arvane_base\"\n  - \"override_pack\"\n");
  write_pack_manifest(packs, "arvane_base", "  - \"arvane\"\n");
  write_pack_manifest(packs, "override_pack", "  - \"override_pack\"\n");
  write_text(
      packs / "arvane_base/data/arvane/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:stone\",\"hardness\":1}]}\n");
  write_text(
      packs / "override_pack/data/override_pack/blocks.json",
      "{\"blocks\":[{\"operation\":\"override\",\"target\":\"arvane:stone\","
      "\"definition\":{\"name\":\"arvane:stone\",\"hardness\":2}}]}\n");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  auto merged = manager.value().load_merged_json_array_text("blocks", "blocks");
  ASSERT_TRUE(merged) << merged.error().describe();
  EXPECT_NE(merged.value().find("\"hardness\": 2"), std::string::npos);

  write_text(
      packs / "override_pack/data/override_pack/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:dirt\"}]}\n");
  manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  auto bad = manager.value().load_merged_json_array_text("blocks", "blocks");
  EXPECT_FALSE(bad);
  EXPECT_NE(bad.error().describe().find("namespace is not declared"), std::string::npos);
}

TEST(PackHashTests, IsDeterministicAndChangesWithContent) {
  ScopedTempDir temp("arvane_pack_hash_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"arvane_base\"\n");
  write_pack_manifest(packs, "arvane_base", "  - \"arvane\"\n");
  write_text(
      packs / "arvane_base/data/arvane/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:stone\"}]}\n");

  auto first = arv::asset::PackManager::load_from_packs_root(packs);
  auto second = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(first) << first.error().describe();
  ASSERT_TRUE(second) << second.error().describe();
  ASSERT_EQ(first.value().stack().size(), 1U);
  EXPECT_EQ(first.value().stack()[0].content_hash, second.value().stack()[0].content_hash);

  write_text(
      packs / "arvane_base/config/messages.yaml",
      "prefix: \"First\"\n");
  auto config_changed = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(config_changed) << config_changed.error().describe();
  EXPECT_EQ(first.value().stack()[0].content_hash, config_changed.value().stack()[0].content_hash);

  write_text(
      packs / "arvane_base/data/arvane/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:dirt\"}]}\n");
  auto changed = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(changed) << changed.error().describe();
  EXPECT_NE(first.value().stack()[0].content_hash, changed.value().stack()[0].content_hash);
}

TEST(PackBundleTests, BuildsDeterministicClientSafeBundleAndInstallsCache) {
  ScopedTempDir temp("arvane_pack_bundle_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"arvane_base\"\n");
  write_pack_manifest(packs, "arvane_base", "  - \"arvane\"\n");
  write_text(
      packs / "arvane_base/data/arvane/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:stone\"}]}\n");
  write_text(
      packs / "arvane_base/assets/arvane/textures/block/stone.png",
      "fake-png");
  write_text(
      packs / "arvane_base/server/spawn_rules.yaml",
      "secret: server-only\n");
  write_text(
      packs / "arvane_base/assets/arvane/textures/block/tool.exe",
      "not exported\n");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  auto first = arv::asset::build_client_pack_bundle(
      manager.value(),
      "{\"format_version\":1,\"blocks\":[]}");
  auto second = arv::asset::build_client_pack_bundle(
      manager.value(),
      "{\"format_version\":1,\"blocks\":[]}");
  ASSERT_TRUE(first) << first.error().message;
  ASSERT_TRUE(second) << second.error().message;
  EXPECT_EQ(first.value().bundle_hash, second.value().bundle_hash);
  EXPECT_TRUE(arv::asset::is_valid_bundle_hash(first.value().bundle_hash));
  EXPECT_TRUE(contains_bundle_file(first.value().files, "manifest.json"));
  EXPECT_TRUE(contains_bundle_file(first.value().files, "registry-client.json"));
  EXPECT_TRUE(contains_bundle_file(first.value().files, "data/arvane/blocks.json"));
  EXPECT_TRUE(contains_bundle_file(
      first.value().files,
      "assets/arvane/textures/block/stone.png"));
  EXPECT_FALSE(contains_bundle_file(first.value().files, "server/spawn_rules.yaml"));
  EXPECT_FALSE(contains_bundle_file(
      first.value().files,
      "assets/arvane/textures/block/tool.exe"));

  EXPECT_TRUE(arv::asset::is_client_bundle_relative_path_allowed(
      "assets/arvane/textures/block/stone.png"));
  EXPECT_FALSE(arv::asset::is_client_bundle_relative_path_allowed("../escape.txt"));
  EXPECT_FALSE(arv::asset::is_client_bundle_relative_path_allowed(
      "assets/arvane/textures/block/tool.exe"));

  const std::filesystem::path bundle_file = temp.path / "bundle.apack";
  auto write_bundle = arv::asset::write_client_pack_bundle_file(
      bundle_file,
      first.value());
  ASSERT_TRUE(write_bundle) << write_bundle.error().message;

  const std::filesystem::path cache_root = temp.path / "cache";
  auto install = arv::asset::install_client_pack_bundle_from_file(
      bundle_file,
      cache_root,
      first.value().bundle_hash);
  ASSERT_TRUE(install) << install.error().message;
  EXPECT_FALSE(install.value().cache_hit);
  EXPECT_TRUE(arv::asset::client_pack_cache_contains(cache_root, first.value().bundle_hash));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      install.value().cache_path / "registry-client.json"));
  EXPECT_FALSE(std::filesystem::exists(
      install.value().cache_path / "server" / "spawn_rules.yaml"));

  auto cached = arv::asset::install_client_pack_bundle_from_file(
      bundle_file,
      cache_root,
      first.value().bundle_hash);
  ASSERT_TRUE(cached) << cached.error().message;
  EXPECT_TRUE(cached.value().cache_hit);

  std::vector<arv::u8> corrupted = first.value().apack_bytes;
  corrupted.back() ^= 0xFFU;
  const std::filesystem::path corrupt_file = temp.path / "corrupt.apack";
  write_binary(corrupt_file, corrupted);
  auto corrupt = arv::asset::install_client_pack_bundle_from_file(
      corrupt_file,
      temp.path / "corrupt-cache",
      first.value().bundle_hash);
  EXPECT_FALSE(corrupt);
}

TEST(PackBundleTests, LoadsClientBundleRootAsSyntheticPack) {
  ScopedTempDir temp("arvane_client_bundle_root_");
  const std::filesystem::path bundle = temp.path / "cache" / "abc123";
  write_text(bundle / "manifest.json", "{\"format_version\":1}\n");
  write_text(bundle / "registry-client.json", "{\"format_version\":1}\n");
  write_text(
      bundle / "data/custom/blocks.json",
      "{\"blocks\":[{\"name\":\"custom:glowstone\"}]}\n");
  write_text(
      bundle / "assets/custom/textures/block/glowstone.png",
      "fake-png");

  const std::filesystem::path roots[] = {bundle};
  auto manager = arv::asset::PackManager::load_client_bundle_roots(roots);
  ASSERT_TRUE(manager) << manager.error().describe();
  ASSERT_EQ(manager.value().stack().size(), 1U);
  EXPECT_EQ(manager.value().stack().front().manifest.id, "server_bundle_0");
  ASSERT_EQ(manager.value().stack().front().manifest.namespaces.size(), 1U);
  EXPECT_EQ(manager.value().stack().front().manifest.namespaces.front(), "custom");

  auto merged = manager.value().load_merged_json_array_text("blocks", "blocks");
  ASSERT_TRUE(merged) << merged.error().describe();
  EXPECT_NE(merged.value().find("\"name\": \"custom:glowstone\""), std::string::npos);

  auto resolved = manager.value().resolve_asset_path(
      "custom:texture/block/glowstone",
      arv::asset::AssetKind::Texture);
  ASSERT_TRUE(resolved) << resolved.error().describe();
  EXPECT_EQ(
      resolved.value().relative_path.generic_string(),
      "assets/custom/textures/block/glowstone.png");
}

TEST(PackBundleTests, RemoteResourceManagerFallsBackToLocalPackAssets) {
  ScopedTempDir temp("arvane_remote_resource_fallback_");
  const std::filesystem::path bundle = temp.path / "cache" / "abc123";
  write_text(bundle / "manifest.json", "{\"format_version\":1}\n");
  write_text(bundle / "registry-client.json", "{\"format_version\":1}\n");
  write_text(
      bundle / "assets/custom/textures/block/glowstone.png",
      "fake-png");

  const std::filesystem::path local_packs = temp.path / "packs";
  write_enabled(local_packs, "enabled:\n  - \"arvane_base\"\n");
  write_pack_manifest(local_packs, "arvane_base", "  - \"arvane\"\n");
  write_text(
      local_packs / "arvane_base/assets/arvane/fonts/font.ttf",
      "fake-font");

  const std::filesystem::path roots[] = {bundle};
  auto remote = arv::asset::PackManager::load_client_bundle_roots(roots);
  ASSERT_TRUE(remote) << remote.error().describe();
  auto fallback = arv::asset::PackManager::load_from_packs_root(local_packs);
  ASSERT_TRUE(fallback) << fallback.error().describe();

  arv::asset::ResourceManager resources(
      std::move(remote.value()),
      std::move(fallback.value()));
  auto remote_texture = resources.resolve_asset(
      {"custom:texture/block/glowstone"},
      arv::asset::AssetKind::Texture);
  ASSERT_TRUE(remote_texture) << remote_texture.error().describe();
  EXPECT_EQ(
      remote_texture.value().path,
      bundle / "assets/custom/textures/block/glowstone.png");

  auto local_font = resources.resolve_asset(
      {"arvane:font/font"},
      arv::asset::AssetKind::Font);
  ASSERT_TRUE(local_font) << local_font.error().describe();
  EXPECT_EQ(
      local_font.value().path,
      local_packs / "arvane_base/assets/arvane/fonts/font.ttf");
}

TEST(PackLockTests, ValidatesRequiredPacks) {
  ScopedTempDir temp("arvane_pack_lock_");
  const std::filesystem::path packs = temp.path / "packs";
  write_enabled(packs, "enabled:\n  - \"arvane_base\"\n");
  write_pack_manifest(packs, "arvane_base", "  - \"arvane\"\n");
  write_text(
      packs / "arvane_base/data/arvane/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:stone\"}]}\n");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  std::vector<arv::asset::RequiredPack> required = manager.value().required_packs();
  EXPECT_TRUE(manager.value().validate_required_packs(required));
  required.front().content_hash.back() =
      required.front().content_hash.back() == '0' ? '1' : '0';
  EXPECT_FALSE(manager.value().validate_required_packs(required));
}
