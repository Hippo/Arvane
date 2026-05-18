#include "arv/client/client_runtime.hpp"
#include "arv/base/base_commands.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/command/command_system.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/player/permissions.hpp"
#include "arv/server/server_commands.hpp"
#include "arv/server/server_runtime.hpp"
#include "support/server_harness.hpp"
#include "support/temp_dir.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>

namespace {

using arv::testsupport::complete_integrated_handshake;
using arv::testsupport::connected_player;

struct RunningServerHarness {
  arv::testsupport::ScopedTempDir temp;
  arv::voxel::BlockRegistry block_registry;
  arv::player::ItemRegistry item_registry;
  arv::player::GameModeRegistry game_modes;
  arv::net::InProcessTransportPair transports;
  arv::server::ServerRuntime server;
  arv::client::ClientRuntime client;

  explicit RunningServerHarness(
      arv::io::MovementValidationConfig movement = arv::io::MovementValidationConfig{},
      arv::Location world_spawn = arv::default_spawn_location())
      : temp("arvane_command_"),
        block_registry(arv::testsupport::make_test_block_registry()),
        item_registry(arv::testsupport::make_test_item_registry(block_registry)),
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
    start_info.game_modes = &game_modes;
    start_info.register_commands = arv::base::register_server_commands;
    start_info.movement = movement;
    start_info.world_spawn = world_spawn;
    start_info.world_name = "default";
    start_info.server_save_root = temp.path / "save";
    EXPECT_TRUE(server.start(start_info));

    EXPECT_TRUE(client.connect_integrated());
    complete_integrated_handshake(server, client);
  }
};

std::optional<arv::command::CommandSender> connected_player_sender(
    RunningServerHarness& harness) {
  const std::vector<arv::SessionID> session_ids = harness.server.sessions().session_ids();
  EXPECT_EQ(session_ids.size(), 1U);
  if (session_ids.empty()) {
    return std::nullopt;
  }
  return harness.server.command_sender_for_session(session_ids.front());
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

TEST(CommandTests, SpawnCommandIsDeniedWithoutPermission) {
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

  ASSERT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_EQ(player.position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
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
