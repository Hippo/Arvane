#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

struct ScopedTempDir {
  std::filesystem::path path;

  explicit ScopedTempDir(std::string_view prefix) {
    path = std::filesystem::temp_directory_path() /
           (std::string{prefix} +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
  }

  ~ScopedTempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

void write_text(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::trunc);
  output << text;
}

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
      "id: \"" + std::string{id} + "\"\n"
      "display_name: \"" + std::string{id} + "\"\n"
      "version: \"1.0.0\"\n"
      "namespaces:\n" +
          std::string{namespaces} +
          "side: \"" + std::string{side} + "\"\n"
          "dependencies: []\n"
          "load_after: []\n"
          "load_before: []\n"
          "capabilities:\n  " + std::string{capabilities} +
          std::string{extra});
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

  write_pack_manifest(
      packs,
      "native_bad",
      "  - \"native_bad\"\n",
      "both",
      "assets: false\n  data: false\n  native_plugin: true\n",
      "native:\n"
      "  api_version: 1\n"
      "  entrypoint: \"arvane_plugin_main\"\n"
      "  libraries:\n"
      "    linux-x86_64: \"../outside.so\"\n");
  auto bad = arv::asset::load_pack_manifest(packs / "native_bad");
  EXPECT_FALSE(bad);
  EXPECT_NE(bad.error().describe().find("unsafe native library path"), std::string::npos);
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
      packs / "arvane_base/data/arvane/blocks.json",
      "{\"blocks\":[{\"name\":\"arvane:dirt\"}]}\n");
  auto changed = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(changed) << changed.error().describe();
  EXPECT_NE(first.value().stack()[0].content_hash, changed.value().stack()[0].content_hash);
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
  required.front().content_hash += 1U;
  EXPECT_FALSE(manager.value().validate_required_packs(required));
}

