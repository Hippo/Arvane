#include "arv/plugin/plugin_manager.hpp"

#include "arv/command/command_dispatcher.hpp"
#include "arv/command/command_registry.hpp"
#include "arv/entity/components.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/text/chat_message.hpp"
#include "arv/text/variables.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifndef ARV_TEST_EXAMPLE_PLUGIN_PATH
#define ARV_TEST_EXAMPLE_PLUGIN_PATH ""
#endif

#ifndef ARV_TEST_FAILING_PLUGIN_PATH
#define ARV_TEST_FAILING_PLUGIN_PATH ""
#endif

#ifndef ARV_TEST_MISSING_SYMBOL_PLUGIN_PATH
#define ARV_TEST_MISSING_SYMBOL_PLUGIN_PATH ""
#endif

#ifndef ARV_TEST_MASON_PLUGIN_PATH
#define ARV_TEST_MASON_PLUGIN_PATH ""
#endif

namespace {

using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

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
      "id: \"" +
          std::string{pack_id} + "\"\n"
                                 "display_name: \"" +
          std::string{pack_id} + "\"\n"
                                 "version: \"1.0.0\"\n"
                                 "namespaces:\n"
                                 "  - \"" +
          std::string{pack_id} + "\"\n"
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
                                 "  entrypoint: \"" +
          std::string{entrypoint} + "\"\n"
                                    "  libraries:\n"
                                    "    " +
          arv::asset::native_plugin_platform_key() + ": \"" +
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
  arv::player::GameModeRegistry game_modes;
  arv::asset::PackManager pack_manager;

  explicit PluginHarness(std::string_view prefix) : temp(prefix),
                                                    packs(temp.path / "packs") {
    (void)game_modes.add_builtin_mode(
        std::string{arv::player::SurvivalGameModeID},
        {});
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
        &game_modes,
        &block_behaviors,
        &item_behaviors,
        &entity_goals,
    };
  }
};

} // namespace

TEST(PluginSdkTests, FixturePluginsLinkOnlySdkAndDoNotDependOnEngineSharedLibraries) {
#if defined(__linux__) || defined(__APPLE__)
  const std::vector<std::string> plugin_paths{
      ARV_TEST_EXAMPLE_PLUGIN_PATH,
      ARV_TEST_MASON_PLUGIN_PATH,
  };
  for (const std::string& plugin_path : plugin_paths) {
    ASSERT_FALSE(plugin_path.empty());
    const std::string command =
#if defined(__APPLE__)
        "otool -L \"" + plugin_path + "\"";
#else
        "ldd \"" + plugin_path + "\"";
#endif
    FILE* pipe = popen(command.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string output;
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
      output += buffer.data();
    }
    const int status = pclose(pipe);
    EXPECT_EQ(status, 0) << plugin_path;
    EXPECT_EQ(output.find("libarv_"), std::string::npos) << plugin_path << '\n'
                                                         << output;
    EXPECT_EQ(output.find("libarvane"), std::string::npos) << plugin_path << '\n'
                                                           << output;
  }
#else
  GTEST_SKIP() << "Dependency inspection is platform-specific.";
#endif
}

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
  EXPECT_TRUE(manager.has_registered_extension(
      arv::plugin::PluginExtensionRegistryKind::EntityBehavior,
      "example_plugin_pack:entity_behavior"));
  EXPECT_TRUE(manager.has_registered_extension(
      arv::plugin::PluginExtensionRegistryKind::ComponentCodec,
      "example_plugin_pack:component_codec"));
  EXPECT_TRUE(manager.has_registered_extension(
      arv::plugin::PluginExtensionRegistryKind::LootFunction,
      "example_plugin_pack:loot_function"));
  EXPECT_TRUE(manager.has_registered_extension(
      arv::plugin::PluginExtensionRegistryKind::RecipeType,
      "example_plugin_pack:recipe_type"));
  EXPECT_TRUE(manager.has_registered_extension(
      arv::plugin::PluginExtensionRegistryKind::WorldgenFeature,
      "example_plugin_pack:feature"));
  EXPECT_TRUE(manager.has_registered_extension(
      arv::plugin::PluginExtensionRegistryKind::WorldgenNoise,
      "example_plugin_pack:noise"));
  EXPECT_TRUE(manager.has_registered_extension(
      arv::plugin::PluginExtensionRegistryKind::WorldgenDensityFunction,
      "example_plugin_pack:density"));
  EXPECT_TRUE(manager.has_command_executor("example_plugin_pack:test_command"));
  EXPECT_TRUE(manager.has_service("example_plugin_pack:test_service"));
  EXPECT_TRUE(harness.game_modes.find("example_plugin_pack:builder") != nullptr);
  EXPECT_TRUE(std::filesystem::exists(
      harness.temp.path / "config" / "plugins" / "example_plugin_pack" /
      "settings.yaml"));

  arv::command::CommandRegistry command_registry;
  manager.register_commands(command_registry);
  EXPECT_NE(command_registry.find("testplugin"), nullptr);
}

TEST(PluginLoadTests, MasonPluginLoadsAndRegistersCommands) {
  if (std::string_view{ARV_TEST_MASON_PLUGIN_PATH}.empty()) {
    GTEST_SKIP() << "Mason plugin path is unavailable.";
  }

  PluginHarness harness("arvane_mason_plugin_");
  write_enabled(harness.packs, "arv_mason");
  const std::filesystem::path relative = copy_plugin_library(
      harness.packs,
      "arv_mason",
      ARV_TEST_MASON_PLUGIN_PATH);
  write_plugin_pack(harness.packs, "arv_mason", relative);
  write_text(
      harness.packs / "arv_mason" / "data" / "commands.yaml",
      "schema_version: 1\n"
      "aliases:\n"
      "  - alias: mmarker\n"
      "    command: mason.marker\n"
      "    visible_in_help: true\n"
      "  - alias: mm\n"
      "    command: mason.marker\n"
      "    visible_in_help: true\n"
      "  - alias: mcopy\n"
      "    command: mason.copy\n"
      "    visible_in_help: true\n");
  harness.load_packs();

  arv::plugin::PluginManager manager;
  auto loaded = manager.load_plugins(harness.load_options({true, {"arv_mason"}}));
  ASSERT_TRUE(loaded) << loaded.error().describe();
  EXPECT_TRUE(manager.has_command_executor("arv_mason:command/mason"));
  EXPECT_TRUE(manager.has_command_executor("arv_mason:command/marker"));
  EXPECT_TRUE(manager.has_command_executor("arv_mason:command/copy"));
  EXPECT_TRUE(std::filesystem::exists(
      harness.temp.path / "config" / "plugins" / "arv_mason" / "config.yaml"));
  EXPECT_TRUE(std::filesystem::exists(
      harness.temp.path / "config" / "plugins" / "arv_mason" / "messages.yaml"));

  arv::command::CommandRegistry registry;
  manager.register_commands(registry);
  EXPECT_NE(registry.find("mason"), nullptr);
  EXPECT_NE(registry.find("mason.marker"), nullptr);
  EXPECT_NE(registry.find("mason.copy"), nullptr);
  auto marker_alias = registry.resolve("mm");
  ASSERT_TRUE(marker_alias.has_value());
  EXPECT_EQ(marker_alias->canonical_name, "mason.marker");
  auto copy_alias = registry.resolve("mcopy");
  ASSERT_TRUE(copy_alias.has_value());
  EXPECT_EQ(copy_alias->canonical_name, "mason.copy");
  EXPECT_FALSE(manager.has_command_executor("arv_mason:command/blueprint"));
  EXPECT_FALSE(registry.resolve("mbp").has_value());

  arv::command::CommandSender sender;
  sender.kind = arv::command::CommandSourceKind::ServerConsole;
  arv::command::CommandServices services;
  const arv::command::CommandResult result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ServerBound,
      sender,
      services,
      "/mason status");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  ASSERT_FALSE(result.messages.empty());
  EXPECT_NE(result.messages.front().text.find("Mason 0.1.0 ready"), std::string::npos);

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{9});
  player.permission_overrides.custom_overrides["mason.command.marker"] =
      arv::player::TriState::Allow;
  manager.attach_runtime({
      .default_world = &world,
      .item_registry = &item_registry,
  });
  arv::command::CommandSender player_sender;
  player_sender.kind = arv::command::CommandSourceKind::Player;
  player_sender.player_id = player.player_id;
  player_sender.world_id = world.id();
  auto allow_command =
      [](const arv::command::CommandSender&, const arv::command::Command&) {
        return arv::command::CommandPermissionResult::allow();
      };
  arv::command::CommandServices player_services{
      nullptr,
      arv::command::CommandPermissionCheckFn{allow_command},
      nullptr,
  };
  const arv::command::CommandResult marker_result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ServerBound,
      player_sender,
      player_services,
      "/mm");
  ASSERT_EQ(marker_result.status, arv::command::CommandStatus::Success);
  ASSERT_FALSE(marker_result.messages.empty());
  EXPECT_NE(
      marker_result.messages.front().text.find("Marker added to your inventory"),
      std::string::npos);
  const std::optional<arv::player::ItemID> sword =
      item_registry.find_item("arvane:wood_sword");
  ASSERT_TRUE(sword.has_value());
  bool marker_found = false;
  for (const std::optional<arv::player::ItemStack>& slot : player.inventory.slots()) {
    if (!slot.has_value() || slot->item != sword.value() || !slot->data.has_value()) {
      continue;
    }
    const arv::io::DataValue* marker =
        arv::io::find_path(*slot->data, "arv_mason.marker");
    marker_found = marker != nullptr && marker->as_bool_or(false);
    if (marker_found) {
      const arv::io::DataValue* display_name =
          arv::io::find_path(*slot->data, "arv_mason.display_name");
      ASSERT_NE(display_name, nullptr);
      EXPECT_EQ(display_name->as_string_or({}), "Marker");
      const arv::io::DataValue* select_a_message =
          arv::io::find_path(*slot->data, "arv_mason.select_a_message");
      ASSERT_NE(select_a_message, nullptr);
      EXPECT_NE(
          select_a_message->as_string_or({}).find("Position A set to {position}"),
          std::string_view::npos);
      const arv::io::DataValue* select_b_message =
          arv::io::find_path(*slot->data, "arv_mason.select_b_message");
      ASSERT_NE(select_b_message, nullptr);
      EXPECT_NE(
          select_b_message->as_string_or({}).find("Position B set to {position}"),
          std::string_view::npos);
      break;
    }
  }
  EXPECT_TRUE(marker_found);
  manager.stop_runtime();
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
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  player.selected_hotbar_slot = 0;
  ASSERT_TRUE(player.inventory.set_slot(0, arv::player::ItemStack{stone_item, 3}));
  player.selection.world = arv::WorldID{1};
  player.selection.a = arv::BlockPos{{1, 2, 3}};
  player.selection.b = arv::BlockPos{{4, 5, 6}};
  arv::entity::EntityDefinition persistent_probe;
  persistent_probe.name = "example_plugin_pack:persistent_probe";
  persistent_probe.persistent_by_default = true;
  persistent_probe.replicated_by_default = false;
  auto entity_registry_result =
      arv::entity::EntityRegistry::build({std::move(persistent_probe)});
  ASSERT_TRUE(entity_registry_result) << entity_registry_result.error().message;
  arv::entity::EntityRegistry entity_registry = std::move(entity_registry_result.value());
  arv::player::GameModeRegistry game_modes;
  ASSERT_TRUE(game_modes.add_builtin_mode(
                            std::string{arv::player::SurvivalGameModeID},
                            {})
                  .has_value());
  ASSERT_TRUE(game_modes.add_custom_mode(
                            "example_plugin_pack:builder",
                            std::string{arv::player::SurvivalGameModeID},
                            {},
                            {{"example_plugin_pack.command.test", arv::player::TriState::Allow}})
                  .has_value());
  arv::player::PermissionGroupRegistry permission_groups;
  arv::text::TextVariableStore text_variables;
  int network_sends = 0;
  int chat_sends = 0;
  jobs.start();
  manager.attach_runtime({
      .default_world = &world,
      .block_registry = &block_registry,
      .item_registry = &item_registry,
      .entity_registry = &entity_registry,
      .game_modes = &game_modes,
      .permission_groups = &permission_groups,
      .job_system = &jobs,
      .tick_index = 0U,
      .server_save_root = harness.temp.path / "save",
      .world_name = "default",
      .player_stable_id = [](arv::PlayerID player_id) -> std::optional<std::string> {
        return player_id == arv::PlayerID{7} ? std::optional<std::string>{"profile-7"}
                                             : std::nullopt;
      },
      .network_send = [&](const arv::plugin::PluginNetworkSendRequest& request) {
        ++network_sends;
        EXPECT_EQ(request.target_kind, arv::plugin::PluginNetworkTargetKind::Player);
        EXPECT_EQ(request.player, arv::PlayerID{7});
        if (request.packet == nullptr) {
          return arv::plugin::PluginNetworkSendResult::failure(
              {ARV_RESULT_NOT_FOUND, "packet missing"});
        }
        EXPECT_EQ(
            request.packet->message_type,
            arv::protocol::MessageType::ClientboundChatMessage);
        EXPECT_FALSE(request.packet->payload.empty());
        return arv::plugin::PluginNetworkSendResult::success(); },
      .text_variables = &text_variables,
      .chat_message_send = [&](const arv::plugin::PluginChatMessageSendRequest& request) {
        ++chat_sends;
        EXPECT_EQ(request.target_kind, arv::plugin::PluginNetworkTargetKind::Player);
        EXPECT_EQ(request.player, arv::PlayerID{7});
        EXPECT_NE(request.message, nullptr);
        EXPECT_EQ(request.channel, "example_plugin_pack:alerts");
        EXPECT_EQ(request.kind, arv::protocol::ChatMessageKind::PrivateMessage);
        if (request.message == nullptr) {
          return arv::plugin::PluginNetworkSendResult::failure(
              {ARV_RESULT_NOT_FOUND, "message missing"});
        }
        EXPECT_EQ(
            request.message->to_string({&text_variables, request.player}),
            "Hello Builder");
        return arv::plugin::PluginNetworkSendResult::success(); },
  });
  manager.tick(1U);
  jobs.join();
  manager.tick(2U);
  manager.stop_runtime();

  ASSERT_TRUE(player.inventory.slot(0).has_value());
  ASSERT_TRUE(player.inventory.slot(0)->data.has_value());
  const arv::io::DataValue* item_data =
      arv::io::find_path(*player.inventory.slot(0)->data, "example_plugin_pack.value");
  ASSERT_NE(item_data, nullptr);
  EXPECT_EQ(item_data->as_string_or({}), "owned item data");

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
  EXPECT_NE(message.find("api_checks=2"), std::string::npos);
  EXPECT_NE(message.find("network_sends=2"), std::string::npos);
  EXPECT_NE(message.find("chat_sends=2"), std::string::npos);
  EXPECT_NE(message.find("sync_ms=1"), std::string::npos);
  EXPECT_NE(message.find("async_ms=1"), std::string::npos);
  EXPECT_EQ(network_sends, 2);
  EXPECT_EQ(chat_sends, 2);
  EXPECT_TRUE(std::filesystem::exists(
      harness.temp.path / "save" / "plugin_data" / "example_plugin_pack" /
      "regions" / "example_plugin_pack" / "persistent_runtime_spawn.region"));
}
