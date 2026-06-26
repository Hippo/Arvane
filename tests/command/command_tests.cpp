#include "arv/client/client_runtime.hpp"
#include "arv/base/base_commands.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command_system.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/player/permissions.hpp"
#include "arv/server/server_commands.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/text/language.hpp"
#include "arv/text/rich_text.hpp"
#include "arv/world/world_time.hpp"
#include "support/server_harness.hpp"
#include "support/temp_dir.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using arv::testsupport::complete_integrated_handshake;
using arv::testsupport::connected_player;

[[nodiscard]] arv::entity::EntityRegistry make_command_test_entity_registry() {
  arv::entity::EntityDefinition cow;
  cow.name = "arvane:cow";
  cow.display_name = "Cow";
  cow.category = "test";
  cow.spawnable = true;
  cow.persistent_by_default = true;
  cow.replicated_by_default = true;

  auto result = arv::base::build_base_entity_registry({std::move(cow)});
  EXPECT_TRUE(result) << result.error().message;
  return std::move(result.value());
}

struct RunningServerHarness {
  arv::testsupport::ScopedTempDir temp;
  arv::voxel::BlockRegistry block_registry;
  arv::player::ItemRegistry item_registry;
  arv::entity::EntityRegistry entity_registry;
  arv::player::GameModeRegistry game_modes;
  arv::net::InProcessTransportPair transports;
  arv::server::ServerRuntime server;
  arv::client::ClientRuntime client;

  explicit RunningServerHarness(
      arv::io::MovementValidationConfig movement = arv::io::MovementValidationConfig{},
      arv::Location world_spawn = arv::default_spawn_location(),
      std::string permission_denied_message = "No permission",
      const arv::text::LanguageCatalog* language = nullptr)
      : temp("arvane_command_"),
        block_registry(arv::testsupport::make_test_block_registry()),
        item_registry(arv::testsupport::make_test_item_registry(block_registry)),
        entity_registry(make_command_test_entity_registry()),
        transports(arv::net::make_in_process_transport_pair()),
        server(std::move(transports.server)),
        client(std::move(transports.client)) {
    arv::base::register_base_game_modes(game_modes);
    arv::server::ServerStartInfo start_info;
    start_info.mode = arv::server::ServerMode::Integrated;
    start_info.base_tps = arv::DefaultBaseTps;
    start_info.listen_address = {"in-process", 0};
    start_info.block_registry = &block_registry;
    start_info.item_registry = &item_registry;
    start_info.entity_registry = &entity_registry;
    start_info.game_modes = &game_modes;
    start_info.register_commands = arv::base::register_server_commands;
    start_info.movement = movement;
    start_info.world_spawn = world_spawn;
    start_info.world_name = "default";
    start_info.server_save_root = temp.path / "save";
    start_info.command_permission_denied_message = std::move(permission_denied_message);
    start_info.language = language;
    EXPECT_TRUE(server.start(start_info));

    EXPECT_TRUE(client.connect_integrated());
    complete_integrated_handshake(server, client);
  }
};

void grant_custom_permission(
    arv::player::PlayerState& player,
    std::string_view node) {
  player.permission_overrides.custom_overrides[std::string{node}] =
      arv::player::TriState::Allow;
}

std::optional<arv::command::CommandSender> connected_player_sender(
    RunningServerHarness& harness) {
  const std::vector<arv::SessionID> session_ids = harness.server.sessions().session_ids();
  EXPECT_EQ(session_ids.size(), 1U);
  if (session_ids.empty()) {
    return std::nullopt;
  }
  return harness.server.command_sender_for_session(session_ids.front());
}

[[nodiscard]] bool result_contains(
    const arv::command::CommandResult& result,
    std::string_view needle) {
  return std::any_of(
      result.messages.begin(),
      result.messages.end(),
      [&](const arv::command::CommandMessage& message) {
        return message.text.find(needle) != std::string::npos;
      });
}

void submit_chat_text(arv::client::ClientRuntime& client, std::string_view text) {
  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = std::string{text}});
  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});
}

[[nodiscard]] bool client_chat_contains(
    const arv::client::ClientRuntime& client,
    std::string_view needle) {
  const std::span<const std::string> messages = client.chat_messages();
  return std::any_of(
      messages.begin(),
      messages.end(),
      [&](const std::string& message) {
        return message.find(needle) != std::string::npos;
      });
}

[[nodiscard]] std::vector<arv::text::RichTextRun> last_chat_runs(
    const arv::client::ClientRuntime& client) {
  EXPECT_FALSE(client.chat_history().empty());
  if (client.chat_history().empty()) {
    return {};
  }
  return arv::text::flatten_rich_text(client.chat_history().back().message);
}

[[nodiscard]] arv::u32 inventory_count(
    const arv::player::Inventory& inventory,
    arv::player::ItemID item_id) {
  arv::u32 count = 0U;
  for (arv::usize slot = 0; slot < inventory.slot_count(); ++slot) {
    const std::optional<arv::player::ItemStack> stack = inventory.slot(slot);
    if (stack.has_value() && stack->item == item_id) {
      count += stack->count;
    }
  }
  return count;
}

} // namespace

TEST(CommandTests, ServerStartsWithoutBuiltInCommandRegistrar) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  ASSERT_TRUE(server.start(start_info));

  EXPECT_TRUE(server.command_registry().commands().empty());
  const arv::command::CommandResult result = server.execute_command(
      arv::server::server_console_command_sender(),
      "/tp local_player 1 2 3");
  EXPECT_EQ(result.status, arv::command::CommandStatus::UnknownCommand);
}

TEST(CommandTests, RegistryRejectsDuplicateCommandNames) {
  arv::command::CommandRegistry registry;
  arv::command::Command command;
  command.name = "test.echo";
  command.domain = arv::command::CommandDomain::ClientBound;
  command.execute = [](arv::command::CommandCtx&) {
    return arv::command::CommandResult::success();
  };

  EXPECT_TRUE(registry.register_command(std::move(command)));

  arv::command::Command duplicate;
  duplicate.name = "test.echo";
  duplicate.domain = arv::command::CommandDomain::ClientBound;
  duplicate.execute = [](arv::command::CommandCtx&) {
    return arv::command::CommandResult::success();
  };
  EXPECT_FALSE(registry.register_command(std::move(duplicate)));
}

TEST(CommandTests, DispatcherNormalizesSlashAndResolvesAliases) {
  arv::command::CommandRegistry registry;
  arv::command::Command command;
  command.name = "test.echo";
  command.domain = arv::command::CommandDomain::ClientBound;
  command.execute = [](arv::command::CommandCtx& ctx) {
    auto args = ctx.reader();
    auto value = args.read_token("value");
    if (!value) {
      return value.error();
    }
    return arv::command::CommandResult::success(std::string{value.value()});
  };
  ASSERT_TRUE(registry.register_command(std::move(command)));
  ASSERT_TRUE(registry.register_alias("echo", "test.echo", true));

  arv::command::CommandSender sender;
  sender.kind = arv::command::CommandSourceKind::Player;
  arv::command::CommandServices services;

  const arv::command::CommandResult result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ClientBound,
      sender,
      services,
      " /echo hello ");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  ASSERT_EQ(result.messages.size(), 1U);
  EXPECT_EQ(result.messages.front().text, "hello");
}

TEST(CommandTests, DispatcherSupportsQuotedStringsAndRejectsUnclosedQuotes) {
  arv::command::CommandRegistry registry;
  arv::command::Command command;
  command.name = "test.echo";
  command.domain = arv::command::CommandDomain::ClientBound;
  command.execute = [](arv::command::CommandCtx& ctx) {
    if (ctx.args.size() != 1U) {
      return arv::command::CommandResult::usage_error("expected one argument");
    }
    return arv::command::CommandResult::success(std::string{ctx.args[0]});
  };
  ASSERT_TRUE(registry.register_command(std::move(command)));
  ASSERT_TRUE(registry.register_alias("echo", "test.echo", true));

  arv::command::CommandSender sender;
  sender.kind = arv::command::CommandSourceKind::Player;
  arv::command::CommandServices services;

  arv::command::CommandResult result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ClientBound,
      sender,
      services,
      "/echo \"hello world\"");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  ASSERT_EQ(result.messages.size(), 1U);
  EXPECT_EQ(result.messages.front().text, "hello world");

  result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ClientBound,
      sender,
      services,
      "/echo \"hello");
  EXPECT_EQ(result.status, arv::command::CommandStatus::UsageError);
  EXPECT_TRUE(result_contains(result, "Unclosed quoted string"));
}

TEST(CommandTests, RelativePositionResolutionUsesAnchor) {
  std::string input{"~ ~5 -3.5"};
  arv::command::CommandArgs args{arv::command::tokenize_command_text(input)};
  arv::command::CommandArgReader reader(args);

  auto position = reader.read_position();
  ASSERT_TRUE(position);
  auto resolved = arv::command::resolve_position(position.value(), arv::Vec3f{1.0F, 2.0F, 3.0F});
  ASSERT_TRUE(resolved);

  EXPECT_FLOAT_EQ(resolved.value().x, 1.0F);
  EXPECT_FLOAT_EQ(resolved.value().y, 7.0F);
  EXPECT_FLOAT_EQ(resolved.value().z, -3.5F);
}

TEST(CommandTests, ServerBoundPlayerCommandRequiresPermission) {
  arv::command::CommandRegistry registry;
  bool executed = false;
  arv::command::Command command;
  command.name = "test.secure";
  command.domain = arv::command::CommandDomain::ServerBound;
  command.permission = "arvane.command.secure";
  command.execute = [&executed](arv::command::CommandCtx&) {
    executed = true;
    return arv::command::CommandResult::success();
  };
  ASSERT_TRUE(registry.register_command(std::move(command)));

  arv::command::CommandSender sender;
  sender.kind = arv::command::CommandSourceKind::Player;
  sender.player_id = arv::PlayerID{1};
  auto permission_check =
      [](const arv::command::CommandSender&, const arv::command::Command& checked_command) {
        return arv::command::CommandPermissionResult::deny(
            "Permission denied: " + checked_command.permission + ".");
      };
  arv::command::CommandServices services{
      nullptr,
      arv::command::CommandPermissionCheckFn{permission_check},
  };

  const arv::command::CommandResult result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ServerBound,
      sender,
      services,
      "/test.secure");

  EXPECT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_TRUE(result_contains(result, "Permission denied: arvane.command.secure."));
  EXPECT_FALSE(executed);
}

TEST(CommandTests, PermissionDeniedCanCarryIntentionalBlankMessage) {
  arv::command::CommandRegistry registry;
  bool executed = false;
  arv::command::Command command;
  command.name = "test.secure";
  command.domain = arv::command::CommandDomain::ServerBound;
  command.permission = "arvane.command.secure";
  command.execute = [&executed](arv::command::CommandCtx&) {
    executed = true;
    return arv::command::CommandResult::success();
  };
  ASSERT_TRUE(registry.register_command(std::move(command)));

  arv::command::CommandSender sender;
  sender.kind = arv::command::CommandSourceKind::Player;
  sender.player_id = arv::PlayerID{1};
  auto permission_check =
      [](const arv::command::CommandSender&, const arv::command::Command&) {
        return arv::command::CommandPermissionResult::deny("");
      };
  arv::command::CommandServices services{
      nullptr,
      arv::command::CommandPermissionCheckFn{permission_check},
  };

  const arv::command::CommandResult result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ServerBound,
      sender,
      services,
      "/test.secure");

  EXPECT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_TRUE(result.messages.empty());
  EXPECT_FALSE(executed);
}

TEST(CommandTests, ServerBoundPlayerCommandWithEmptyPermissionRunsWithoutPermissionCheck) {
  arv::command::CommandRegistry registry;
  bool executed = false;
  bool checked = false;
  arv::command::Command command;
  command.name = "test.open";
  command.domain = arv::command::CommandDomain::ServerBound;
  command.permission = "";
  command.execute = [&executed](arv::command::CommandCtx&) {
    executed = true;
    return arv::command::CommandResult::success("opened");
  };
  ASSERT_TRUE(registry.register_command(std::move(command)));

  arv::command::CommandSender sender;
  sender.kind = arv::command::CommandSourceKind::Player;
  sender.player_id = arv::PlayerID{1};
  auto permission_check =
      [&checked](const arv::command::CommandSender&, const arv::command::Command&) {
        checked = true;
        return arv::command::CommandPermissionResult::deny("should not be checked");
      };
  arv::command::CommandServices services{
      nullptr,
      arv::command::CommandPermissionCheckFn{permission_check},
  };

  const arv::command::CommandResult result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ServerBound,
      sender,
      services,
      "/test.open");

  EXPECT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(executed);
  EXPECT_FALSE(checked);
}

TEST(CommandTests, HelpCommandUsesRegisteredMetadataAndAliases) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/help");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Commands page 1/"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/help 2");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Commands page 2/"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/help summon");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Name: summon"));
  EXPECT_TRUE(result_contains(result, "Usage: /summon <entity_id>"));
  EXPECT_TRUE(result_contains(result, "Aliases: /spawnentity, /entityspawn"));
  EXPECT_TRUE(result_contains(result, "Permission: arvane.command.summon"));
  EXPECT_TRUE(result_contains(result, "arvane:cow"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/help /tp");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Name: player.teleport"));
  EXPECT_TRUE(result_contains(result, "Matched alias: /tp"));
  EXPECT_TRUE(result_contains(result, "Aliases: /tp"));
  EXPECT_TRUE(result_contains(result, "/tp [target] <x> <y> <z>"));
  EXPECT_TRUE(result_contains(result, "/tp <target_player> <destination_player>"));
}

TEST(CommandTests, ConsoleCommandsDoNotNeedSlashAndBypassPlayerPermissions) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "help");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Commands page 1/"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "tp local_player 10 20 -30");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(player.position, (arv::Vec3f{10.0F, 20.0F, -30.0F}));
}

TEST(CommandTests, CoreWorldCommandsMutateStateAndValidateArguments) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/time query");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "World time is"));
  (void)harness.client.take_world_time_update();

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/time set day");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  harness.client.fixed_tick(harness.server.tick_index());
  const std::optional<arv::protocol::ClientboundWorldTime> client_time =
      harness.client.take_world_time_update();
  ASSERT_TRUE(client_time.has_value());
  EXPECT_EQ(
      harness.server.voxel_world().clock().day_time_tick,
      arv::world::day_tick_from_hour_minute(
          8U,
          0U,
          harness.server.voxel_world().clock().day_length_ticks));
  EXPECT_EQ(
      client_time->day_time_tick,
      harness.server.voxel_world().clock().day_time_tick);
  EXPECT_EQ(
      client_time->day_length_ticks,
      harness.server.voxel_world().clock().day_length_ticks);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/time set -1");
  EXPECT_EQ(result.status, arv::command::CommandStatus::UsageError);
  EXPECT_EQ(
      harness.server.voxel_world().clock().day_time_tick,
      arv::world::day_tick_from_hour_minute(
          8U,
          0U,
          harness.server.voxel_world().clock().day_length_ticks));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/weather rain");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(harness.server.voxel_world().weather_id(), "rain");

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/difficulty hard");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(
      harness.server.voxel_world().difficulty(),
      arv::world::WorldDifficulty::Hard);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/gamerule doDaylightCycle false");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  ASSERT_NE(harness.server.voxel_world().game_rule("doDaylightCycle"), nullptr);
  EXPECT_EQ(harness.server.voxel_world().game_rule("doDaylightCycle")->bool_value, false);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/gamerule doDaylightCycle maybe");
  EXPECT_EQ(result.status, arv::command::CommandStatus::UsageError);
}

TEST(CommandTests, CorePlayerInventoryCommandsGiveAndClearItems) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides["*"] = arv::player::TriState::Allow;
  const arv::player::ItemID stone =
      harness.item_registry.find_item("arvane:stone").value();

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/give @s arvane:stone 64");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(inventory_count(player.inventory, stone), 64U);
  EXPECT_TRUE(result_contains(result, "Gave 64 arvane:stone"));

  result = harness.server.execute_command(sender.value(), "/clear @s arvane:stone");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(inventory_count(player.inventory, stone), 0U);
  EXPECT_TRUE(result_contains(result, "Removed 64 items"));
}

TEST(CommandTests, CoreEntityAndEffectCommandsUseRegistriesAndSelectors) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides["*"] = arv::player::TriState::Allow;
  player.position = {2.0F, 70.0F, 3.0F};
  const arv::usize initial_entities = harness.server.voxel_world().entity_count();

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/summon arvane:cow");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(harness.server.voxel_world().entity_count(), initial_entities + 1U);
  EXPECT_TRUE(result_contains(result, "Spawned 1 arvane:cow"));

  result = harness.server.execute_command(
      sender.value(),
      "/summon not_namespaced");
  EXPECT_EQ(result.status, arv::command::CommandStatus::UsageError);
  EXPECT_EQ(harness.server.voxel_world().entity_count(), initial_entities + 1U);

  result = harness.server.execute_command(
      sender.value(),
      "/effect give @e arvane:speed 1 2");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Applied arvane:speed"));

  result = harness.server.execute_command(sender.value(), "/effect list @s");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "arvane:speed"));

  result = harness.server.execute_command(sender.value(), "/effect clear @s arvane:speed");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Cleared"));

  result = harness.server.execute_command(sender.value(), "/kill @e");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Affected"));
}

TEST(CommandTests, CoreAdminCommandsProduceFeedback) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides["*"] = arv::player::TriState::Allow;
  player.position = {1.25F, 65.5F, -4.75F};

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());

  arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/list");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "local_player"));

  result = harness.server.execute_command(sender.value(), "/say hello world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Broadcasted: hello world"));

  result = harness.server.execute_command(sender.value(), "/me waves");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Broadcasted action: waves"));

  result = harness.server.execute_command(sender.value(), "/debug position");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Position 1.25"));

  result = harness.server.execute_command(sender.value(), "/debug tick");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "TPS"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/save-all flush");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Saved and flushed"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/kick local_player Test complete");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Kicked local_player"));
}

TEST(CommandTests, TimeSubcommandsRequireSubactionPermission) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides["arvane.command.time"] =
      arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::u32 before = harness.server.voxel_world().clock().day_time_tick;
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/time set day");

  EXPECT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_EQ(harness.server.voxel_world().clock().day_time_tick, before);
  EXPECT_TRUE(result_contains(result, "No permission"));
  EXPECT_FALSE(result_contains(result, "arvane.command.time.set"));
}

TEST(CommandTests, ConsoleTeleportMovesNamedOnlinePlayer) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.position = {0.5F, 4.0F, -2.0F};

  const arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/tp local_player 10 20 -30");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(player.position, (arv::Vec3f{10.0F, 20.0F, -30.0F}));
  EXPECT_EQ(player.velocity, (arv::Vec3f{}));
  EXPECT_FALSE(player.grounded);
}

TEST(CommandTests, PlayerTeleportUsesPermissionAndRelativeCoordinates) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.position = {1.0F, 2.0F, 3.0F};
  player.permission_overrides.custom_overrides["arvane.command.teleport"] =
      arv::player::TriState::Allow;

  const std::vector<arv::SessionID> session_ids = harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  std::optional<arv::command::CommandSender> sender =
      harness.server.command_sender_for_session(session_ids.front());
  ASSERT_TRUE(sender.has_value());

  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/tp ~ ~5 -4");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(player.position, (arv::Vec3f{1.0F, 7.0F, -4.0F}));
}

TEST(CommandTests, TeleportCanMoveTargetPlayerToDestinationPlayer) {
  arv::command::CommandRegistry registry;
  arv::base::register_server_commands(registry);

  const std::vector<arv::command::OnlinePlayerCommandTarget> players{
      {arv::SessionID{1},
       arv::PlayerID{11},
       arv::WorldID{1},
       "Alice",
       {1.0F, 2.0F, 3.0F},
       0.0F,
       0.0F},
      {arv::SessionID{2},
       arv::PlayerID{12},
       arv::WorldID{1},
       "Bob",
       {8.0F, 9.0F, 10.0F},
       1.25F,
       -0.5F},
  };
  auto by_id = [&players](arv::PlayerID player_id) {
    for (const auto& player : players) {
      if (player.player_id == player_id) {
        return std::optional<arv::command::OnlinePlayerCommandTarget>{player};
      }
    }
    return std::optional<arv::command::OnlinePlayerCommandTarget>{};
  };
  auto by_name = [&players](std::string_view name) {
    for (const auto& player : players) {
      if (player.display_name == name) {
        return std::optional<arv::command::OnlinePlayerCommandTarget>{player};
      }
    }
    return std::optional<arv::command::OnlinePlayerCommandTarget>{};
  };
  bool coordinate_teleport_called = false;
  auto teleport_player = [&coordinate_teleport_called](arv::PlayerID, arv::Vec3f) {
    coordinate_teleport_called = true;
    return false;
  };
  std::optional<arv::PlayerID> teleported_player;
  std::optional<arv::Location> teleported_location;
  auto teleport_player_location =
      [&teleported_player, &teleported_location](
          arv::PlayerID player_id,
          arv::Location location) {
        teleported_player = player_id;
        teleported_location = location;
        return true;
      };

  arv::command::ServerCommandServices domain_services;
  domain_services.online_player_by_id =
      arv::command::OnlinePlayerByIDFn{by_id};
  domain_services.online_player_by_name =
      arv::command::OnlinePlayerByNameFn{by_name};
  domain_services.teleport_player =
      arv::command::TeleportPlayerFn{teleport_player};
  domain_services.teleport_player_location =
      arv::command::TeleportPlayerLocationFn{teleport_player_location};

  arv::command::CommandServices services;
  services.domain_services = &domain_services;
  arv::command::CommandSender sender = arv::server::server_console_command_sender();
  const arv::command::CommandResult result = arv::command::dispatch_command(
      registry,
      arv::command::CommandDomain::ServerBound,
      sender,
      services,
      "/tp Alice Bob");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_FALSE(coordinate_teleport_called);
  ASSERT_TRUE(teleported_player.has_value());
  EXPECT_EQ(teleported_player.value(), arv::PlayerID{11});
  ASSERT_TRUE(teleported_location.has_value());
  EXPECT_EQ(arv::location_position(teleported_location.value()), (arv::Vec3f{8.0F, 9.0F, 10.0F}));
  EXPECT_FLOAT_EQ(teleported_location->yaw_radians, 1.25F);
  EXPECT_FLOAT_EQ(teleported_location->pitch_radians, -0.5F);
}

TEST(CommandTests, PlayerTeleportIsDeniedWithoutPermission) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.position = {1.0F, 2.0F, 3.0F};

  const std::vector<arv::SessionID> session_ids = harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  std::optional<arv::command::CommandSender> sender =
      harness.server.command_sender_for_session(session_ids.front());
  ASSERT_TRUE(sender.has_value());

  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/tp 10 20 30");

  ASSERT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_EQ(player.position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
}

TEST(CommandTests, NewPlayersUseConfiguredWorldSpawn) {
  RunningServerHarness harness(
      arv::io::MovementValidationConfig{},
      arv::Location{14.5F, 70.0F, -2.5F, 1.25F, -0.1F});

  const arv::player::PlayerState& player = connected_player(harness.server);
  EXPECT_EQ(player.position, (arv::Vec3f{14.5F, 70.0F, -2.5F}));
  EXPECT_FLOAT_EQ(player.yaw_radians, 1.25F);
  EXPECT_FLOAT_EQ(player.pitch_radians, -0.1F);
}

TEST(CommandTests, PlayerSpawnTeleportsToWorldSpawnWithRotation) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  harness.server.voxel_world().set_spawn_point(
      arv::Location{4.5F, 9.0F, -12.5F, 0.75F, -0.2F});
  player.position = {1.0F, 2.0F, 3.0F};
  player.yaw_radians = 0.0F;
  player.pitch_radians = 0.0F;
  player.permission_overrides.custom_overrides[
      std::string{arv::base::ids::SpawnPermissionNode}] =
      arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/spawn");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(player.position, (arv::Vec3f{4.5F, 9.0F, -12.5F}));
  EXPECT_FLOAT_EQ(player.yaw_radians, 0.75F);
  EXPECT_FLOAT_EQ(player.pitch_radians, -0.2F);
}

TEST(CommandTests, SetspawnStoresCurrentPlayerLocation) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.position = {-3.5F, 64.0F, 8.5F};
  player.yaw_radians = 1.5F;
  player.pitch_radians = 0.25F;
  player.permission_overrides.custom_overrides[
      std::string{arv::base::ids::SetSpawnPermissionNode}] =
      arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/setspawn");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  const arv::Location spawn = harness.server.voxel_world().spawn_point();
  EXPECT_EQ(arv::location_position(spawn), (arv::Vec3f{-3.5F, 64.0F, 8.5F}));
  EXPECT_FLOAT_EQ(spawn.yaw_radians, 1.5F);
  EXPECT_FLOAT_EQ(spawn.pitch_radians, 0.25F);
  ASSERT_TRUE(spawn.world.has_value());
  EXPECT_EQ(spawn.world.value(), harness.server.voxel_world().id());
}

TEST(CommandTests, SpawnCommandUsesBaselinePermissionWithoutArvPerms) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.position = {1.0F, 2.0F, 3.0F};
  harness.server.voxel_world().set_spawn_point(
      arv::Location{4.5F, 9.0F, -12.5F, 0.0F, 0.0F});

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/spawn");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(player.position, (arv::Vec3f{4.5F, 9.0F, -12.5F}));
}

TEST(CommandTests, PlayerGamemodeCommandUsesPerModePermission) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides["arvane.command.gamemode.creative"] =
      arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/gamemode creative");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(player.game_mode_id, arv::base::ids::CreativeGameModeID);
  EXPECT_TRUE(player.resolved_permissions.core.can_fly);
  EXPECT_TRUE(player.resolved_permissions.core.has_creative_inventory);
}

TEST(CommandTests, PlayerGamemodeAliasUsesSamePermission) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides["arvane.command.gamemode.creative"] =
      arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/gmc");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(player.game_mode_id, arv::base::ids::CreativeGameModeID);
  EXPECT_TRUE(player.resolved_permissions.core.can_fly);
}

TEST(CommandTests, ServerboundChatMessageUsesBaselineSendPermissionWithoutArvPerms) {
  RunningServerHarness harness;

  submit_chat_text(harness.client, "hello there");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_FALSE(client_chat_contains(harness.client, "No permission"));
  EXPECT_TRUE(client_chat_contains(harness.client, "local_player: hello there"));
}

TEST(CommandTests, ServerboundChatColorMarkupRequiresPermission) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  grant_custom_permission(player, "arvane.chat.send");

  submit_chat_text(harness.client, "<red>Hi");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_TRUE(client_chat_contains(harness.client, "local_player: <red>Hi"));
  const std::vector<arv::text::RichTextRun> runs = last_chat_runs(harness.client);
  ASSERT_GE(runs.size(), 3U);
  EXPECT_EQ(runs.back().text, "<red>Hi");
  EXPECT_EQ(runs.back().style.color, (arv::text::RgbColor{0xDDU, 0xDDU, 0xDDU}));
}

TEST(CommandTests, ServerboundChatColorMarkupFormatsWhenPermitted) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  grant_custom_permission(player, "arvane.chat.send");
  grant_custom_permission(player, "arvane.chat.color");

  submit_chat_text(harness.client, "<red>Hi");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_TRUE(client_chat_contains(harness.client, "local_player: Hi"));
  const std::vector<arv::text::RichTextRun> runs = last_chat_runs(harness.client);
  ASSERT_GE(runs.size(), 3U);
  EXPECT_EQ(runs.back().text, "Hi");
  EXPECT_EQ(runs.back().style.color, (arv::text::RgbColor{0xFFU, 0x55U, 0x55U}));
}

TEST(CommandTests, ServerboundChatCommandExecutesThroughPermissions) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides["arvane.command.gamemode.creative"] =
      arv::player::TriState::Allow;

  submit_chat_text(harness.client, "/gmc");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_EQ(player.game_mode_id, arv::base::ids::CreativeGameModeID);
  EXPECT_TRUE(player.resolved_permissions.core.can_fly);
  EXPECT_TRUE(client_chat_contains(harness.client, "Set game mode to creative."));
}

TEST(CommandTests, ServerboundMsgCommandSendsPrivateMessage) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides[
      std::string{arv::base::ids::MsgPermissionNode}] =
      arv::player::TriState::Allow;

  submit_chat_text(harness.client, "/msg @s hello there");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_TRUE(client_chat_contains(harness.client, "[local_player -> you] hello there"));
  EXPECT_TRUE(client_chat_contains(harness.client, "Message sent to local_player."));
}

TEST(CommandTests, PlayerGamemodeCommandIsDeniedWithoutModePermission) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/gamemode creative");

  ASSERT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_EQ(player.game_mode_id, arv::base::ids::SurvivalGameModeID);
  EXPECT_TRUE(result_contains(result, "No permission"));
  EXPECT_FALSE(result_contains(result, "arvane.command.gamemode.creative"));
}

TEST(CommandTests, ServerboundChatCommandDeniesWithoutPermission) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);

  submit_chat_text(harness.client, "/gmc");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_EQ(player.game_mode_id, arv::base::ids::SurvivalGameModeID);
  EXPECT_TRUE(client_chat_contains(harness.client, "No permission"));
  EXPECT_FALSE(client_chat_contains(harness.client, "arvane.command.gamemode.creative"));
}

TEST(CommandTests, ServerboundChatCommandDenialUsesAuthoredTemplateColor) {
  arv::text::LanguageCatalog language;
  language.set("command.dispatcher.permission_denied", "<red>No permission");
  RunningServerHarness harness{
      arv::io::MovementValidationConfig{},
      arv::default_spawn_location(),
      "No permission",
      &language};

  submit_chat_text(harness.client, "/gmc");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_TRUE(client_chat_contains(harness.client, "No permission"));
  const std::vector<arv::text::RichTextRun> runs = last_chat_runs(harness.client);
  ASSERT_FALSE(runs.empty());
  EXPECT_EQ(runs.back().text, "No permission");
  EXPECT_EQ(runs.back().style.color, (arv::text::RgbColor{0xFFU, 0x55U, 0x55U}));
}

TEST(CommandTests, ServerboundMsgCommandUsesBaselinePermissionWithoutArvPerms) {
  RunningServerHarness harness;

  submit_chat_text(harness.client, "/msg @s hello there");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_FALSE(client_chat_contains(harness.client, "No permission"));
  EXPECT_TRUE(client_chat_contains(harness.client, "[local_player -> you] hello there"));
  EXPECT_TRUE(client_chat_contains(harness.client, "Message sent to local_player."));
}

TEST(CommandTests, ServerboundChatCommandOmitsBlankPermissionDeniedMessage) {
  RunningServerHarness harness{
      arv::io::MovementValidationConfig{},
      arv::default_spawn_location(),
      ""};
  arv::player::PlayerState& player = connected_player(harness.server);

  submit_chat_text(harness.client, "/gmc");
  harness.server.tick();
  harness.client.fixed_tick(harness.server.tick_index());

  EXPECT_EQ(player.game_mode_id, arv::base::ids::SurvivalGameModeID);
  EXPECT_FALSE(client_chat_contains(harness.client, "No permission"));
  EXPECT_FALSE(client_chat_contains(harness.client, "Permission denied"));
  EXPECT_TRUE(harness.client.chat_messages().empty());
}

TEST(CommandTests, PermissionDeniedMessageUsesServerConfig) {
  RunningServerHarness harness{
      arv::io::MovementValidationConfig{},
      arv::default_spawn_location(),
      "You cannot do that."};

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/gamemode creative");

  EXPECT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_TRUE(result_contains(result, "You cannot do that."));
  EXPECT_FALSE(result_contains(result, "arvane.command.gamemode.creative"));
}

TEST(CommandTests, GlobalWildcardPermissionAllowsGamemodeCommand) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides["*"] = arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/gamemode creative");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_EQ(player.game_mode_id, arv::base::ids::CreativeGameModeID);
}

TEST(CommandTests, PlayerFlyspeedCommandUpdatesPlayerSpeed) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides[std::string{arv::base::ids::FlySpeedPermissionNode}] =
      arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/flyspeed 10");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_FLOAT_EQ(player.fly_speed, 80.0F);
}

TEST(CommandTests, PlayerFlyspeedOneUsesDefaultFlySpeed) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.fly_speed = 80.0F;
  player.permission_overrides.custom_overrides[std::string{arv::base::ids::FlySpeedPermissionNode}] =
      arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/flyspeed 1");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_FLOAT_EQ(player.fly_speed, 8.0F);
}

TEST(CommandTests, PlayerFlyspeedCommandIsDeniedWithoutPermission) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.fly_speed = 8.0F;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/flyspeed 10");

  ASSERT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_FLOAT_EQ(player.fly_speed, 8.0F);
}

TEST(CommandTests, PlayerFlyspeedCommandRejectsServerMovementLimitMismatch) {
  arv::io::MovementValidationConfig movement;
  movement.max_fly_speed_blocks_per_second = 32.0F;
  RunningServerHarness harness{movement};
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides[std::string{arv::base::ids::FlySpeedPermissionNode}] =
      arv::player::TriState::Allow;
  player.fly_speed = 3.0F;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/flyspeed 5");

  ASSERT_EQ(result.status, arv::command::CommandStatus::UsageError);
  EXPECT_FLOAT_EQ(player.fly_speed, 3.0F);
}

TEST(CommandTests, SelectionCommandStoresCurrentPlayerSelection) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides[std::string{arv::base::ids::SelectionPermissionNode}] =
      arv::player::TriState::Allow;

  player.position = {1.25F, 2.8F, -3.1F};
  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/select a");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  ASSERT_TRUE(player.selection.world.has_value());
  ASSERT_TRUE(player.selection.a.has_value());
  EXPECT_EQ(player.selection.world.value(), harness.server.voxel_world().id());
  EXPECT_EQ(player.selection.a.value(), (arv::BlockPos{{1, 2, -4}}));

  player.position = {-5.0F, 8.2F, 9.7F};
  sender = connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  result = harness.server.execute_command(sender.value(), "/sel b");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  ASSERT_TRUE(player.selection.b.has_value());
  EXPECT_EQ(player.selection.b.value(), (arv::BlockPos{{-5, 8, 9}}));

  result = harness.server.execute_command(sender.value(), "/select info");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  ASSERT_FALSE(result.messages.empty());
  EXPECT_NE(result.messages.front().text.find("7 x 7 x 14"), std::string::npos);

  result = harness.server.execute_command(sender.value(), "/sel clear");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_FALSE(player.selection.world.has_value());
  EXPECT_FALSE(player.selection.a.has_value());
  EXPECT_FALSE(player.selection.b.has_value());
}

TEST(CommandTests, ExplodeCommandRequiresPermissionAndUsesServerExplosion) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.position = {1.5F, 2.0F, 3.5F};

  const std::optional<arv::voxel::BlockVariantID> stone =
      harness.block_registry.find_variant("arvane:stone");
  ASSERT_TRUE(stone.has_value());
  (void)harness.server.voxel_world().ensure_chunk_section({{0, 0, 0}});
  const arv::BlockPos target{{1, 2, 3}};
  ASSERT_TRUE(harness.server.voxel_world().set_block(target, stone.value()));

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  arv::command::CommandResult result = harness.server.execute_command(
      sender.value(),
      "/explode ~ ~ ~ arvane:small --no-block-damage --seed 123");
  EXPECT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_EQ(harness.server.voxel_world().get_block(target), stone.value());

  player.permission_overrides.custom_overrides["command.explode"] =
      arv::player::TriState::Allow;
  sender = connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  result = harness.server.execute_command(
      sender.value(),
      "/explode ~ ~ ~ arvane:small --no-block-damage --seed 123");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Created explosion arvane:small"));
  EXPECT_EQ(harness.server.voxel_world().get_block(target), stone.value());

  result = harness.server.execute_command(
      sender.value(),
      "/boom 1.5 2 3.5 arvane:shockwave --seed 124");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Created explosion arvane:shockwave"));
}

TEST(CommandTests, BlueprintCommandSavesListsAndPastesWorldBlueprints) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.permission_overrides.custom_overrides[std::string{arv::base::ids::BlueprintPermissionNode}] =
      arv::player::TriState::Allow;

  const std::optional<arv::voxel::BlockVariantID> stone =
      harness.block_registry.find_variant("arvane:stone");
  const std::optional<arv::voxel::BlockVariantID> dirt =
      harness.block_registry.find_variant("arvane:dirt");
  ASSERT_TRUE(stone.has_value());
  ASSERT_TRUE(dirt.has_value());

  (void)harness.server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(harness.server.voxel_world().set_block(
      arv::BlockPos{{1, 2, 3}},
      stone.value()));
  ASSERT_TRUE(harness.server.voxel_world().set_block(
      arv::BlockPos{{2, 2, 3}},
      dirt.value()));

  player.selection.world = harness.server.voxel_world().id();
  player.selection.a = arv::BlockPos{{1, 2, 3}};
  player.selection.b = arv::BlockPos{{2, 2, 3}};
  player.position = {1.0F, 2.0F, 3.0F};

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/blueprint save line");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(std::filesystem::exists(
      harness.temp.path / "save" / "worlds" / "default" / "blueprints" / "line.abp"));

  result = harness.server.execute_command(sender.value(), "/bp list");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  ASSERT_FALSE(result.messages.empty());
  EXPECT_NE(result.messages.front().text.find("line"), std::string::npos);

  player.position = {10.0F, 2.0F, 3.0F};
  sender = connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  result = harness.server.execute_command(sender.value(), "/bp paste line");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  EXPECT_EQ(
      harness.server.voxel_world().get_block(arv::BlockPos{{10, 2, 3}}),
      stone.value());
  EXPECT_EQ(
      harness.server.voxel_world().get_block(arv::BlockPos{{11, 2, 3}}),
      dirt.value());
}
