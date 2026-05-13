#include "arv/plugin/plugin_manager.hpp"

#include "arv/command/command_dispatcher.hpp"
#include "arv/command/command_registry.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/world/entity_goal_registry.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef ARV_TEST_EXAMPLE_PLUGIN_PATH
#define ARV_TEST_EXAMPLE_PLUGIN_PATH ""
#endif

#ifndef ARV_TEST_FAILING_PLUGIN_PATH
#define ARV_TEST_FAILING_PLUGIN_PATH ""
#endif

#ifndef ARV_TEST_MISSING_SYMBOL_PLUGIN_PATH
#define ARV_TEST_MISSING_SYMBOL_PLUGIN_PATH ""
#endif

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

std::filesystem::path copy_plugin_library(
    const std::filesystem::path& packs_root,
    std::string_view pack_id,
    const std::filesystem::path& source) {
  const std::filesystem::path relative =
      std::filesystem::path{"native"} /
      arv::asset::native_plugin_platform_key() /
      source.filename();
  const std::filesystem::path destination =
      packs_root / std::string{pack_id} / relative;
  std::filesystem::create_directories(destination.parent_path());
  std::filesystem::copy_file(
      source,
      destination,
      std::filesystem::copy_options::overwrite_existing);
  return relative;
}

void write_enabled(const std::filesystem::path& packs_root, std::string_view pack_id) {
  write_text(
      packs_root / "enabled.yaml",
      "enabled:\n  - \"" + std::string{pack_id} + "\"\n");
}

void write_plugin_pack(
    const std::filesystem::path& packs_root,
    std::string_view pack_id,
    const std::filesystem::path& relative_library,
    std::string_view entrypoint = "arvane_plugin_main") {
  write_text(
      packs_root / std::string{pack_id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" + std::string{pack_id} + "\"\n"
      "display_name: \"" + std::string{pack_id} + "\"\n"
      "version: \"1.0.0\"\n"
      "namespaces:\n"
      "  - \"" + std::string{pack_id} + "\"\n"
      "side: \"server\"\n"
      "dependencies: []\n"
      "load_after: []\n"
      "load_before: []\n"
      "capabilities:\n"
      "  assets: false\n"
      "  data: true\n"
      "  native_plugin: true\n"
      "native:\n"
      "  api_version: 1\n"
      "  entrypoint: \"" + std::string{entrypoint} + "\"\n"
      "  libraries:\n"
      "    " + arv::asset::native_plugin_platform_key() + ": \"" +
      relative_library.generic_string() + "\"\n");
  write_text(
      packs_root / std::string{pack_id} / "data" / std::string{pack_id} /
          "plugin_probe.txt",
      "probe=data\n");
}

struct PluginHarness {
  ScopedTempDir temp;
  std::filesystem::path packs;
  arv::voxel::BlockBehaviorRegistry block_behaviors;
  arv::player::ItemBehaviorRegistry item_behaviors;
  arv::world::EntityGoalRegistry entity_goals;
  arv::asset::PackManager pack_manager;

  explicit PluginHarness(std::string_view prefix)
      : temp(prefix),
        packs(temp.path / "packs") {
  }

  void load_packs() {
    auto manager = arv::asset::PackManager::load_from_packs_root(packs);
    ASSERT_TRUE(manager) << manager.error().describe();
    pack_manager = std::move(manager.value());
  }

  arv::plugin::PluginLoadOptions load_options(arv::plugin::NativePluginPolicy policy) {
    return {
        &pack_manager,
        std::move(policy),
        temp.path / "config",
        &block_behaviors,
        &item_behaviors,
        &entity_goals,
    };
  }
};

} // namespace

TEST(PluginPolicyTests, DisabledAndAllowlistPoliciesSkipWithoutLoading) {
  PluginHarness harness("arvane_plugin_policy_");
  write_enabled(harness.packs, "example_plugin_pack");
  const std::filesystem::path relative = copy_plugin_library(
      harness.packs,
      "example_plugin_pack",
      ARV_TEST_EXAMPLE_PLUGIN_PATH);
  write_plugin_pack(harness.packs, "example_plugin_pack", relative);
  harness.load_packs();

  arv::plugin::PluginManager disabled;
  auto disabled_result = disabled.load_plugins(
      harness.load_options({false, {"example_plugin_pack"}}));
  ASSERT_TRUE(disabled_result) << disabled_result.error().describe();
  EXPECT_EQ(disabled.loaded_plugin_count(), 0U);
  auto disabled_infos = disabled.plugins();
  ASSERT_EQ(disabled_infos.size(), 1U);
  EXPECT_EQ(disabled_infos.front().state, arv::plugin::PluginState::SkippedByPolicy);

  arv::plugin::PluginManager not_allowed;
  auto not_allowed_result = not_allowed.load_plugins(
      harness.load_options({true, {"other_pack"}}));
  ASSERT_TRUE(not_allowed_result) << not_allowed_result.error().describe();
  EXPECT_EQ(not_allowed.loaded_plugin_count(), 0U);
  auto not_allowed_infos = not_allowed.plugins();
  ASSERT_EQ(not_allowed_infos.size(), 1U);
  EXPECT_EQ(not_allowed_infos.front().state, arv::plugin::PluginState::SkippedByPolicy);
}

TEST(PluginLoadTests, LoadsAllowlistedPluginAndRegistersBehaviorServicesAndCommands) {
  PluginHarness harness("arvane_plugin_load_");
  write_enabled(harness.packs, "example_plugin_pack");
  const std::filesystem::path relative = copy_plugin_library(
      harness.packs,
      "example_plugin_pack",
      ARV_TEST_EXAMPLE_PLUGIN_PATH);
  write_plugin_pack(harness.packs, "example_plugin_pack", relative);
  harness.load_packs();

  arv::plugin::PluginManager manager;
  auto result = manager.load_plugins(
      harness.load_options({true, {"example_plugin_pack"}}));
  ASSERT_TRUE(result) << result.error().describe();
  EXPECT_EQ(manager.loaded_plugin_count(), 1U);
  EXPECT_TRUE(harness.block_behaviors.contains("example_plugin_pack:glowing_block_behavior"));
  EXPECT_TRUE(harness.item_behaviors.contains("example_plugin_pack:wand_use"));
  EXPECT_TRUE(harness.entity_goals.contains("example_plugin_pack:idle_goal"));
  EXPECT_TRUE(manager.has_command_executor("example_plugin_pack:test_command"));
  EXPECT_TRUE(manager.has_service("example_plugin_pack:test_service"));
  EXPECT_TRUE(std::filesystem::exists(
      harness.temp.path / "config" / "plugins" / "example_plugin_pack" /
      "example_plugin_pack" / "settings.yaml"));

  arv::command::CommandRegistry command_registry;
  manager.register_commands(command_registry);
  EXPECT_NE(command_registry.find("testplugin"), nullptr);
}

TEST(PluginLoadTests, ReportsMissingLibraryMissingSymbolAndFalseEntrypoint) {
  {
    PluginHarness harness("arvane_plugin_missing_library_");
    write_enabled(harness.packs, "example_plugin_pack");
    write_plugin_pack(
        harness.packs,
        "example_plugin_pack",
        std::filesystem::path{"native"} /
            arv::asset::native_plugin_platform_key() /
            std::filesystem::path{std::filesystem::path{ARV_TEST_EXAMPLE_PLUGIN_PATH}.filename()});
    harness.load_packs();

    arv::plugin::PluginManager manager;
    auto result = manager.load_plugins(
        harness.load_options({true, {"example_plugin_pack"}}));
    EXPECT_FALSE(result);
    EXPECT_NE(result.error().describe().find("does not exist"), std::string::npos);
  }

  {
    PluginHarness harness("arvane_plugin_missing_symbol_");
    write_enabled(harness.packs, "example_plugin_pack");
    const std::filesystem::path relative = copy_plugin_library(
        harness.packs,
        "example_plugin_pack",
        ARV_TEST_MISSING_SYMBOL_PLUGIN_PATH);
    write_plugin_pack(harness.packs, "example_plugin_pack", relative);
    harness.load_packs();

    arv::plugin::PluginManager manager;
    auto result = manager.load_plugins(
        harness.load_options({true, {"example_plugin_pack"}}));
    EXPECT_FALSE(result);
    EXPECT_NE(result.error().describe().find("entrypoint"), std::string::npos);
  }

  {
    PluginHarness harness("arvane_plugin_false_entrypoint_");
    write_enabled(harness.packs, "example_plugin_pack");
    const std::filesystem::path relative = copy_plugin_library(
        harness.packs,
        "example_plugin_pack",
        ARV_TEST_FAILING_PLUGIN_PATH);
    write_plugin_pack(harness.packs, "example_plugin_pack", relative);
    harness.load_packs();

    arv::plugin::PluginManager manager;
    auto result = manager.load_plugins(
        harness.load_options({true, {"example_plugin_pack"}}));
    EXPECT_FALSE(result);
    EXPECT_NE(result.error().describe().find("returned false"), std::string::npos);
  }
}

TEST(PluginRuntimeTests, RunsSchedulerEventsAndCommandExecutor) {
  PluginHarness harness("arvane_plugin_runtime_");
  write_enabled(harness.packs, "example_plugin_pack");
  const std::filesystem::path relative = copy_plugin_library(
      harness.packs,
      "example_plugin_pack",
      ARV_TEST_EXAMPLE_PLUGIN_PATH);
  write_plugin_pack(harness.packs, "example_plugin_pack", relative);
  harness.load_packs();

  arv::plugin::PluginManager manager;
  auto result = manager.load_plugins(
      harness.load_options({true, {"example_plugin_pack"}}));
  ASSERT_TRUE(result) << result.error().describe();

  arv::sched::JobSystem jobs{{.background_cpu_workers = 1U}};
  arv::world::WorldRuntime world{arv::WorldID{1}};
  jobs.start();
  manager.attach_runtime({
      .default_world = &world,
      .job_system = &jobs,
      .tick_index = 0U,
  });
  manager.tick(1U);
  jobs.join();
  manager.tick(2U);
  manager.stop_runtime();

  arv::command::CommandRegistry registry;
  manager.register_commands(registry);
  arv::command::CommandSender sender;
  sender.kind = arv::command::CommandSourceKind::ServerConsole;
  arv::command::CommandServices services;
  const arv::command::CommandResult command_result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ServerBound,
      sender,
      services,
      "/testplugin");

  ASSERT_EQ(command_result.status, arv::command::CommandStatus::Success);
  ASSERT_FALSE(command_result.messages.empty());
  const std::string& message = command_result.messages.front().text;
  EXPECT_NE(message.find("sync=2"), std::string::npos);
  EXPECT_NE(message.find("async=1"), std::string::npos);
  EXPECT_NE(message.find("thread_violations=1"), std::string::npos);
  EXPECT_NE(message.find("starts=1"), std::string::npos);
  EXPECT_NE(message.find("stops=1"), std::string::npos);
}
