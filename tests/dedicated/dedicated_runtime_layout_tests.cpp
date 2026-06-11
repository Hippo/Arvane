#include "dedicated_runtime_layout.hpp"

#include "arv/asset/pack.hpp"
#include "arv/io/config.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

using arv::testsupport::TempDir;
using arv::testsupport::write_file;

[[nodiscard]] arv::io::EngineConfig make_config(const std::filesystem::path& root) {
  arv::io::EngineConfig config;
  config.paths.project_root = root;
  config.paths.arvane_home_root = root / "arvane_home";
  config.paths.pack_root = config.paths.arvane_home_root / "packs";
  config.paths.server_root = root / "arvane_server";
  config.paths.server_pack_root = config.paths.server_root / "packs";
  config.paths.server_worlds_root = config.paths.server_root / "worlds";
  config.paths.server_cache_root = config.paths.server_root / "cache";
  config.paths.server_logs_root = config.paths.server_root / "logs";
  return config;
}

void write_base_pack(const std::filesystem::path& packs_root) {
  write_file(
      packs_root / "arvane_base/pack.yaml",
      R"(schema_version: 1
id: "arvane_base"
display_name: "Arvane Base"
version: "0.1.0"
namespaces:
  - "arvane"
side: "both"
capabilities:
  assets: true
  data: true
  config: true
  native_plugin: false
)");
  write_file(
      packs_root / "arvane_base/data/arvane/blocks.json",
      R"({"blocks":[]})");
}

} // namespace

TEST(DedicatedRuntimeLayoutTests, SeedsBasePackAndCreatesServerDirectoriesOnCleanInstall) {
  const TempDir temp{"arvane_dedicated_layout_"};
  arv::io::EngineConfig config = make_config(temp.path);
  write_base_pack(config.paths.packs_path());

  auto result = arv::dedicated::ensure_dedicated_runtime_layout(config);
  ASSERT_TRUE(result) << result.error().message;

  EXPECT_TRUE(std::filesystem::is_directory(config.paths.server_root_path()));
  EXPECT_TRUE(std::filesystem::is_directory(config.paths.server_packs_path()));
  EXPECT_TRUE(std::filesystem::is_directory(config.paths.server_worlds_path()));
  EXPECT_TRUE(std::filesystem::is_directory(config.paths.server_cache_path()));
  EXPECT_TRUE(std::filesystem::is_directory(config.paths.server_logs_path()));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      config.paths.server_packs_path() / "arvane_base/pack.yaml"));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      config.paths.server_packs_path() / "arvane_base/data/arvane/blocks.json"));
  EXPECT_FALSE(std::filesystem::exists(config.paths.server_packs_path() / "enabled.yaml"));

  auto packs = arv::asset::PackManager::load_from_packs_root(
      config.paths.server_packs_path());
  ASSERT_TRUE(packs) << packs.error().describe();
  ASSERT_EQ(packs.value().stack().size(), 1U);
  EXPECT_EQ(packs.value().stack()[0].manifest.id, "arvane_base");
}

TEST(DedicatedRuntimeLayoutTests, LeavesConfiguredServerPacksUntouched) {
  const TempDir temp{"arvane_dedicated_layout_configured_"};
  arv::io::EngineConfig config = make_config(temp.path);
  write_base_pack(config.paths.packs_path());
  write_file(
      config.paths.server_packs_path() / "enabled.yaml",
      "enabled:\n  - custom_pack\n");

  auto result = arv::dedicated::ensure_dedicated_runtime_layout(config);
  ASSERT_TRUE(result) << result.error().message;

  EXPECT_TRUE(std::filesystem::is_regular_file(config.paths.server_packs_path() / "enabled.yaml"));
  EXPECT_FALSE(std::filesystem::exists(config.paths.server_packs_path() / "arvane_base/pack.yaml"));
}
