#include "dedicated_runtime_layout.hpp"
#include "dedicated_launch_options.hpp"

#include "arv/asset/pack.hpp"
#include "arv/io/config.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

void write_server_pack(const std::filesystem::path& packs_root, std::string_view id) {
  const std::string pack_id{id};
  write_file(
      packs_root / pack_id / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" + pack_id + "\"\n"
      "display_name: \"" + pack_id + "\"\n"
      "version: \"0.1.0\"\n"
      "namespaces:\n"
      "  - \"" + pack_id + "\"\n"
      "side: \"server\"\n"
      "capabilities:\n"
      "  assets: false\n"
      "  data: true\n"
      "  config: true\n"
      "  native_plugin: false\n");
}

[[nodiscard]] arv::dedicated::DedicatedLaunchOptions parse_args(
    std::vector<std::string> args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (std::string& arg : args) {
    argv.push_back(arg.data());
  }
  return arv::dedicated::parse_launch_options(
      static_cast<int>(argv.size()),
      argv.data());
}

} // namespace

TEST(DedicatedLaunchOptionsTests, DefaultsServerRootToExecutableDirectory) {
  const TempDir temp{"arvane_dedicated_launch_"};
  const std::filesystem::path executable = temp.path / "arvane_server" / "arv_dedicated";

  const auto options = parse_args({executable.string()});

  EXPECT_EQ(options.config.profile, arv::io::ConfigProfile::Server);
  EXPECT_EQ(
      options.config.explicit_server_root_path,
      std::filesystem::weakly_canonical(executable).parent_path());
}

TEST(DedicatedLaunchOptionsTests, ProjectRootKeepsDefaultServerRootResolution) {
  const TempDir temp{"arvane_dedicated_launch_project_"};

  const auto options = parse_args({
      "arv_dedicated",
      "--project-root",
      temp.path.string(),
  });

  EXPECT_TRUE(options.config.explicit_server_root_path.empty());
  EXPECT_EQ(options.config.project_root, temp.path);
}

TEST(DedicatedLaunchOptionsTests, ExplicitServerRootWins) {
  const TempDir temp{"arvane_dedicated_launch_root_"};

  const auto options = parse_args({
      "arv_dedicated",
      "--server-root",
      (temp.path / "server").string(),
  });

  EXPECT_EQ(options.config.explicit_server_root_path, temp.path / "server");
}

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

TEST(DedicatedRuntimeLayoutTests, LeavesInstalledServerPacksAsLoadableStack) {
  const TempDir temp{"arvane_dedicated_layout_server_packs_"};
  arv::io::EngineConfig config = make_config(temp.path);
  write_base_pack(config.paths.packs_path());
  write_base_pack(config.paths.server_packs_path());
  write_server_pack(config.paths.server_packs_path(), "arv_perms");

  auto result = arv::dedicated::ensure_dedicated_runtime_layout(config);
  ASSERT_TRUE(result) << result.error().message;

  EXPECT_FALSE(std::filesystem::exists(config.paths.server_packs_path() / "enabled.yaml"));

  auto packs = arv::asset::PackManager::load_from_packs_root(
      config.paths.server_packs_path());
  ASSERT_TRUE(packs) << packs.error().describe();
  ASSERT_EQ(packs.value().stack().size(), 2U);
  const auto has_pack = [&](std::string_view id) {
    return std::find_if(
               packs.value().stack().begin(),
               packs.value().stack().end(),
               [&](const arv::asset::PackSource& pack) {
                 return pack.manifest.id == id;
               }) != packs.value().stack().end();
  };
  EXPECT_TRUE(has_pack("arvane_base"));
  EXPECT_TRUE(has_pack("arv_perms"));
}

TEST(DedicatedRuntimeLayoutTests, LeavesConfiguredServerPacksUntouched) {
  const TempDir temp{"arvane_dedicated_layout_configured_"};
  arv::io::EngineConfig config = make_config(temp.path);
  write_base_pack(config.paths.packs_path());
  write_server_pack(config.paths.server_packs_path(), "custom_pack");

  auto result = arv::dedicated::ensure_dedicated_runtime_layout(config);
  ASSERT_TRUE(result) << result.error().message;

  EXPECT_TRUE(std::filesystem::is_regular_file(
      config.paths.server_packs_path() / "custom_pack/pack.yaml"));
  EXPECT_FALSE(std::filesystem::exists(config.paths.server_packs_path() / "enabled.yaml"));
  EXPECT_FALSE(std::filesystem::exists(config.paths.server_packs_path() / "arvane_base/pack.yaml"));
}
