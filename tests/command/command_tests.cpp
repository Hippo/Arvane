#include "arv/client/client_runtime.hpp"
#include "arv/base/base_commands.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command_system.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/entity/components.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/persistence/save.hpp"
#include "arv/player/permissions.hpp"
#include "arv/server/server_commands.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/text/language.hpp"
#include "arv/text/rich_text.hpp"
#include "arv/voxel/storage.hpp"
#include "arv/world/space_definition.hpp"
#include "arv/world/world_generation.hpp"
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

[[nodiscard]] arv::entity::VehicleDefinitionRegistry make_command_test_vehicle_registry() {
  arv::entity::VehicleDefinition buggy;
  buggy.name = "arvane:command_buggy";
  buggy.kind = arv::entity::VehicleKind::Vehicle;
  buggy.physics = arv::entity::VehiclePhysicsKind::GroundVehicle;
  buggy.collision_size = {1.8F, 1.2F, 3.2F};
  buggy.seats.push_back({"driver",
                         "driver",
                         arv::entity::SeatRole::Driver,
                         {0.0F, 0.8F, 0.0F},
                         {},
                         true,
                         arv::entity::CameraMode::ThirdPersonFollow});
  buggy.authority.max_speed = 22.0F;
  buggy.authority.max_acceleration = 18.0F;
  buggy.authority.max_deceleration = 50.0F;
  buggy.authority.max_angular_speed = arv::degrees_to_radians(110.0F);
  buggy.authority.max_vertical_speed = 24.0F;
  buggy.authority.max_step_height = 0.6F;
  buggy.authority.requires_ground = false;
  buggy.authority.requires_water = false;
  buggy.cargo = arv::entity::VehicleCargoDefinition{6};

  arv::entity::VehicleDefinition rocket;
  rocket.name = "arvane:command_rocket";
  rocket.kind = arv::entity::VehicleKind::Vehicle;
  rocket.physics = arv::entity::VehiclePhysicsKind::Aircraft;
  rocket.tags = {"arvane:vehicle", "arvane:rocket", "arvane:space_capable"};
  rocket.collision_size = {1.2F, 3.0F, 1.2F};
  rocket.seats.push_back({"pilot",
                          "pilot",
                          arv::entity::SeatRole::Driver,
                          {0.0F, 1.4F, 0.0F},
                          {},
                          true,
                          arv::entity::CameraMode::VehicleChase});
  rocket.aircraft = {
      0.0F,
      80.0F,
      24.0F,
      16.0F,
      0.98F,
      arv::degrees_to_radians(70.0F),
      arv::degrees_to_radians(60.0F),
      arv::degrees_to_radians(90.0F),
      arv::degrees_to_radians(45.0F),
      30.0F,
      8.0F,
      1.2F,
      0.75F,
      80.0F,
      80.0F,
      8.0F,
      0.90F};
  rocket.space_aircraft = arv::entity::AircraftMovementDefinition{
      0.0F,
      120.0F,
      20.0F,
      12.0F,
      0.997F,
      arv::degrees_to_radians(95.0F),
      arv::degrees_to_radians(95.0F),
      arv::degrees_to_radians(120.0F),
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      120.0F,
      120.0F,
      0.0F,
      0.997F};
  rocket.fuel = arv::entity::VehicleFuelDefinition{
      "arvane:rocket_fuel/basic",
      60.0F,
      20.0F,
      1.0F,
      10.0F};
  rocket.cargo = arv::entity::VehicleCargoDefinition{9};
  rocket.authority.max_speed = 120.0F;
  rocket.authority.max_acceleration = 80.0F;
  rocket.authority.max_deceleration = 80.0F;
  rocket.authority.max_angular_speed = arv::degrees_to_radians(180.0F);
  rocket.authority.max_vertical_speed = 120.0F;

  auto result = arv::entity::VehicleDefinitionRegistry::build(
      {std::move(buggy), std::move(rocket)});
  EXPECT_TRUE(result) << result.error().message;
  return std::move(result.value());
}

[[nodiscard]] arv::world::SpaceDefinitionRegistry make_command_test_space_definitions() {
  arv::world::SpaceDefinitionRegistry registry;

  arv::world::EnvironmentDefinition deep_space;
  deep_space.id = "arvane:environment/deep_space";
  deep_space.gravity_scale = 0.0F;
  deep_space.has_atmosphere = false;
  deep_space.has_oxygen = false;
  deep_space.sky = "space";
  deep_space.ambient_light = 0.15F;
  deep_space.fog = "none";
  registry.environments.emplace(deep_space.id, std::move(deep_space));

  arv::world::EnvironmentDefinition overworld_env;
  overworld_env.id = "arvane:environment/overworld";
  overworld_env.gravity_scale = 1.0F;
  overworld_env.has_atmosphere = true;
  overworld_env.has_oxygen = true;
  overworld_env.sky = "overworld";
  overworld_env.ambient_light = 0.15F;
  overworld_env.fog = "none";
  registry.environments.emplace(overworld_env.id, std::move(overworld_env));

  arv::world::EnvironmentDefinition luna_env;
  luna_env.id = "arvane:environment/luna";
  luna_env.gravity_scale = 0.18F;
  luna_env.has_atmosphere = false;
  luna_env.has_oxygen = false;
  luna_env.sky = "space";
  luna_env.ambient_light = 0.15F;
  luna_env.fog = "none";
  registry.environments.emplace(luna_env.id, std::move(luna_env));

  arv::world::WorldDefinition default_world;
  default_world.id = "arvane:world/default";
  default_world.kind = arv::world::WorldDefinitionKind::Planet;
  default_world.display_name = "Default";
  default_world.worldgen = "arvane:worldgen/dimension/overworld";
  default_world.environment = "arvane:environment/overworld";
  default_world.rules.allow_vehicle_launch = true;
  arv::world::SpaceExitDefinition exit;
  exit.enabled = true;
  exit.target_space_world = "arvane:space/solar_system";
  exit.min_y = 20;
  exit.min_vertical_speed = 5.0F;
  exit.require_vehicle_tags = {"arvane:space_capable"};
  exit.consume_fuel = true;
  exit.transition_spawn.position = {11.0F, 22.0F, 33.0F};
  exit.transition_spawn.velocity = {0.0F, 1.0F, 0.0F};
  exit.transition_spawn.rotation_degrees = {0.0F, 90.0F, 0.0F};
  exit.transition_spawn.velocity_scale = 0.25F;
  default_world.space_exit = std::move(exit);
  registry.worlds.emplace(default_world.id, std::move(default_world));

  arv::world::WorldDefinition luna_world;
  luna_world.id = "arvane:world/luna";
  luna_world.kind = arv::world::WorldDefinitionKind::Planet;
  luna_world.display_name = "Luna";
  luna_world.worldgen = "arvane:worldgen/dimension/luna";
  luna_world.environment = "arvane:environment/luna";
  luna_world.rules.allow_vehicle_launch = true;
  registry.worlds.emplace(luna_world.id, std::move(luna_world));

  arv::world::WorldDefinition solar_system;
  solar_system.id = "arvane:space/solar_system";
  solar_system.kind = arv::world::WorldDefinitionKind::Space;
  solar_system.display_name = "Solar System";
  solar_system.worldgen = std::string{arv::world::VoidWorldGenerationID};
  solar_system.environment = "arvane:environment/deep_space";
  solar_system.rules.allow_block_placement = false;
  solar_system.rules.allow_space_entry = true;
  arv::world::SpaceWorldDefinition space;
  space.bodies.push_back("arvane:space_body/luna");
  solar_system.space = std::move(space);
  registry.worlds.emplace(solar_system.id, std::move(solar_system));

  arv::world::SpaceBodyDefinition luna;
  luna.id = "arvane:space_body/luna";
  luna.type = arv::world::SpaceBodyType::Moon;
  luna.display_name = "Luna";
  luna.space_world = "arvane:space/solar_system";
  luna.target_world = "arvane:world/luna";
  luna.position = {2200.0F, 0.0F, 700.0F};
  luna.visual_radius = 90.0F;
  luna.entry_radius = 130.0F;
  luna.entry.enabled = true;
  luna.entry.mode = arv::world::SpaceBodyEntryMode::FixedSpawn;
  luna.entry.require_vehicle_tags = {"arvane:space_capable"};
  luna.entry.spawn_position = {0.0F, 2300.0F, 0.0F};
  luna.entry.spawn_velocity = {0.0F, -10.0F, 0.0F};
  luna.entry.spawn_rotation_degrees = {0.0F, 180.0F, 0.0F};
  luna.entry.effect = "arvane:moon_entry";
  registry.space_bodies.emplace(luna.id, std::move(luna));

  return registry;
}

[[nodiscard]] arv::world::WorldGenerationConfig make_command_test_luna_worldgen() {
  arv::world::WorldGenerationConfig config;
  config.id = "arvane:worldgen/dimension/luna";
  config.dimension_id = config.id;
  config.version = 1U;
  config.seed_salt = "command_test_luna";
  config.min_y = -64;
  config.max_y = 128;
  config.sea_level = -64;
  config.base_height = 28;
  config.height_scale = 12;
  config.terrain_noise_type = "arvane:noise/simplex_2d";
  config.terrain_frequency = 0.015F;
  config.terrain_octaves = 3U;
  config.terrain_amplitude = 1.0F;
  config.terrain_lacunarity = 2.0F;
  config.terrain_gain = 0.5F;
  config.surface_layers.push_back({"arvane:stone", 4, false});
  config.surface_layers.push_back({"arvane:sand", 1, false});
  config.surface_layers.push_back({"arvane:stone", 0, true});
  return config;
}

struct RunningServerHarness {
  arv::testsupport::ScopedTempDir temp;
  arv::voxel::BlockRegistry block_registry;
  arv::player::ItemRegistry item_registry;
  arv::entity::EntityRegistry entity_registry;
  arv::entity::VehicleDefinitionRegistry vehicle_registry;
  arv::world::SpaceDefinitionRegistry space_definitions;
  arv::world::WorldGenerationConfig luna_worldgen;
  arv::player::GameModeRegistry game_modes;
  arv::net::InProcessTransportPair transports;
  arv::server::ServerRuntime server;
  arv::client::ClientRuntime client;

  explicit RunningServerHarness(
      arv::io::MovementValidationConfig movement = arv::io::MovementValidationConfig{},
      arv::Location world_spawn = arv::default_spawn_location(),
      std::string permission_denied_message = "No permission",
      const arv::text::LanguageCatalog* language = nullptr,
      std::string server_save_display_name = {})
      : temp("arvane_command_"),
        block_registry(arv::testsupport::make_test_block_registry()),
        item_registry(arv::testsupport::make_test_item_registry(block_registry)),
        entity_registry(make_command_test_entity_registry()),
        vehicle_registry(make_command_test_vehicle_registry()),
        space_definitions(make_command_test_space_definitions()),
        luna_worldgen(make_command_test_luna_worldgen()),
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
    start_info.vehicle_registry = &vehicle_registry;
    start_info.space_definitions = &space_definitions;
    start_info.resolve_world_generation =
        [config = luna_worldgen](
            std::string_view generator_id) -> std::optional<arv::world::WorldGenerationConfig> {
      if (generator_id == config.dimension_id || generator_id == config.id) {
        return config;
      }
      return std::nullopt;
    };
    start_info.game_modes = &game_modes;
    start_info.register_commands = arv::base::register_server_commands;
    start_info.movement = movement;
    start_info.world_spawn = world_spawn;
    start_info.world_name = "default";
    start_info.server_save_root = temp.path / "save";
    start_info.server_save_display_name = std::move(server_save_display_name);
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

[[nodiscard]] std::size_t count_region_files(
    const std::filesystem::path& directory) {
  if (!std::filesystem::exists(directory)) {
    return 0U;
  }
  std::size_t count = 0U;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".arvr") {
      ++count;
    }
  }
  return count;
}

void submit_chat_text(arv::client::ClientRuntime& client, std::string_view text) {
  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = std::string{text}});
  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});
}

void pump_client_until_ingame(
    arv::server::ServerRuntime& server,
    arv::client::ClientRuntime& client) {
  (void)server;
  for (int i = 0; i < 20; ++i) {
    client.poll_network();
    if (client.connection_state() == arv::net::ConnectionState::InGame) {
      return;
    }
  }
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

[[nodiscard]] arv::EntityID create_command_test_entity(
    arv::world::WorldRuntime& world,
    arv::Vec3f position) {
  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:cow";
  info.position = position;
  return world.create_entity(std::move(info));
}

[[nodiscard]] arv::EntityID create_command_test_vehicle(
    arv::world::WorldRuntime& world,
    const arv::entity::VehicleDefinitionRegistry& vehicles,
    arv::Vec3f position) {
  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{1.8F, 1.2F, 3.2F}};
  components.health = arv::entity::AuthoredHealthComponent{30.0F};
  components.rideable =
      arv::entity::AuthoredRideableComponent{"arvane:command_buggy"};
  components.vehicle_physics =
      arv::entity::AuthoredVehiclePhysicsComponent{"arvane:command_buggy"};

  arv::world::EntityCreateInfo info;
  info.type_id = arv::EntityTypeID{20};
  info.type_name = "arvane:command_buggy";
  info.position = position;
  info.authored_components = components;
  info.vehicle_definitions = &vehicles;
  return world.create_entity(std::move(info));
}

[[nodiscard]] arv::EntityID create_command_test_rocket(
    arv::world::WorldRuntime& world,
    const arv::entity::VehicleDefinitionRegistry& vehicles,
    arv::Vec3f position) {
  arv::entity::EntityComponentSet components;
  components.collision =
      arv::entity::AuthoredCollisionComponent{{1.2F, 3.0F, 1.2F}};
  components.health = arv::entity::AuthoredHealthComponent{40.0F};
  components.rideable =
      arv::entity::AuthoredRideableComponent{"arvane:command_rocket"};
  components.vehicle_physics =
      arv::entity::AuthoredVehiclePhysicsComponent{"arvane:command_rocket"};

  arv::world::EntityCreateInfo info;
  info.type_id = arv::EntityTypeID{21};
  info.type_name = "arvane:command_rocket";
  info.position = position;
  info.authored_components = components;
  info.vehicle_definitions = &vehicles;
  return world.create_entity(std::move(info));
}

[[nodiscard]] const arv::server::WorldRecord* create_and_load_command_test_space_world(
    RunningServerHarness& harness) {
  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create solar_system --type space --seed 4242");
  EXPECT_EQ(result.status, arv::command::CommandStatus::Success);
  if (result.status != arv::command::CommandStatus::Success) {
    return nullptr;
  }

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load solar_system");
  EXPECT_EQ(result.status, arv::command::CommandStatus::Success);
  if (result.status != arv::command::CommandStatus::Success) {
    return nullptr;
  }

  return harness.server.worlds().find("solar_system");
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

TEST(CommandTests, EffectCommandsUseTargetWorldAfterWorldTeleport) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  const arv::PlayerID player_id = player.player_id;
  player.permission_overrides.custom_overrides["*"] = arv::player::TriState::Allow;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create effect_world --seed 24680");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load effect_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp effect_world local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const arv::server::WorldRecord* runtime_record =
      harness.server.worlds().find("effect_world");
  ASSERT_NE(runtime_record, nullptr);
  arv::world::WorldRuntime* runtime =
      harness.server.world_runtime(runtime_record->id);
  ASSERT_NE(runtime, nullptr);
  const std::optional<arv::EntityID> runtime_entity =
      runtime->entity_for_player(player_id);
  ASSERT_TRUE(runtime_entity.has_value());

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  ASSERT_EQ(sender->world_id, runtime_record->id);

  result = harness.server.execute_command(
      sender.value(),
      "/effect give @s arvane:speed 1 2");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Applied arvane:speed to 1 entities."));
  ASSERT_EQ(runtime->status_effects(runtime_entity.value()).size(), 1U);
  EXPECT_EQ(
      runtime->status_effects(runtime_entity.value()).front().effect_id,
      "arvane:speed");

  result = harness.server.execute_command(sender.value(), "/effect list @s");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "arvane:speed"));

  result = harness.server.execute_command(sender.value(), "/effect clear @s arvane:speed");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Cleared 1 effects."));
  EXPECT_TRUE(runtime->status_effects(runtime_entity.value()).empty());
}

TEST(CommandTests, KillCommandUsesTargetWorldForEntityIdCollisions) {
  RunningServerHarness harness;
  const arv::EntityID default_entity =
      create_command_test_entity(harness.server.voxel_world(), {1.0F, 70.0F, 1.0F});
  ASSERT_NE(default_entity.value, 0U);

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create kill_world --seed 97531");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load kill_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const arv::server::WorldRecord* runtime_record =
      harness.server.worlds().find("kill_world");
  ASSERT_NE(runtime_record, nullptr);
  arv::world::WorldRuntime* runtime =
      harness.server.world_runtime(runtime_record->id);
  ASSERT_NE(runtime, nullptr);

  arv::EntityID runtime_entity{};
  while (runtime_entity.value < default_entity.value) {
    runtime_entity =
        create_command_test_entity(*runtime, {2.0F, 70.0F, 2.0F});
    ASSERT_NE(runtime_entity.value, 0U);
  }
  ASSERT_EQ(runtime_entity, default_entity);

  arv::command::CommandSender sender = arv::server::server_console_command_sender();
  sender.world_id = runtime_record->id;
  sender.entity_id = runtime_entity;
  sender.position = arv::Vec3f{2.0F, 70.0F, 2.0F};
  result = harness.server.execute_command(sender, "/kill @s");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(harness.server.voxel_world().entity_exists(default_entity));
  EXPECT_FALSE(runtime->entity_exists(runtime_entity));
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

TEST(CommandTests, WorldCommandCreatesListsInspectsPersistsAndDeletesWorlds) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world help");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "World commands page 1/"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/wm create test_world --seed 12345");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Created world 'test_world'."));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      harness.temp.path / "save" / "worlds" / "test_world" / "world.json"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/worlds list");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "test_world"));
  EXPECT_TRUE(result_contains(result, "seed=12345"));
  EXPECT_TRUE(result_contains(result, "unloaded"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world info test_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "World: test_world"));
  EXPECT_TRUE(result_contains(result, "Status: unloaded"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp test_world local_player");
  EXPECT_EQ(result.status, arv::command::CommandStatus::Failure);
  EXPECT_TRUE(result_contains(result, "World 'test_world' is not loaded"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world delete test_world");
  EXPECT_EQ(result.status, arv::command::CommandStatus::Failure);
  EXPECT_TRUE(result_contains(result, "Run /world delete test_world --confirm"));
  EXPECT_TRUE(std::filesystem::exists(
      harness.temp.path / "save" / "worlds" / "test_world"));

  const std::filesystem::path save_root = harness.temp.path / "save";
  harness.server.shutdown();

  arv::net::InProcessTransportPair transports =
      arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime restarted(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &harness.block_registry;
  start_info.item_registry = &harness.item_registry;
  start_info.entity_registry = &harness.entity_registry;
  start_info.vehicle_registry = &harness.vehicle_registry;
  start_info.game_modes = &harness.game_modes;
  start_info.register_commands = arv::base::register_server_commands;
  start_info.world_name = "default";
  start_info.server_save_root = save_root;
  ASSERT_TRUE(restarted.start(start_info));

  result = restarted.execute_command(
      arv::server::server_console_command_sender(),
      "/world info test_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "World: test_world"));

  result = restarted.execute_command(
      arv::server::server_console_command_sender(),
      "/world delete default --confirm");
  EXPECT_EQ(result.status, arv::command::CommandStatus::Failure);
  EXPECT_TRUE(result_contains(result, "Cannot delete the default world"));

  result = restarted.execute_command(
      arv::server::server_console_command_sender(),
      "/world delete test_world --confirm");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Deleted world 'test_world'."));
  EXPECT_FALSE(std::filesystem::exists(save_root / "worlds" / "test_world"));
}

TEST(CommandTests, WorldCommandLoadsTeleportsUnloadsAndDeletesLiveWorldRuntime) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create runtime_world --seed 6789");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load runtime_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Loaded world 'runtime_world'."));

  const arv::server::WorldRecord* runtime_record =
      harness.server.worlds().find("runtime_world");
  ASSERT_NE(runtime_record, nullptr);
  const arv::WorldID runtime_world_id = runtime_record->id;
  EXPECT_EQ(runtime_record->state, arv::server::WorldRuntimeState::Running);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp runtime_world local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Teleported local_player to world 'runtime_world'."));

  pump_client_until_ingame(harness.server, harness.client);
  ASSERT_EQ(harness.client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(harness.client.camera().world_id(), runtime_world_id);
  const std::vector<arv::SessionID> session_ids =
      harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session =
      harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->world_id, runtime_world_id);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world unload runtime_world");
  EXPECT_EQ(result.status, arv::command::CommandStatus::Failure);
  EXPECT_TRUE(result_contains(result, "contains players"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world unload runtime_world --force");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Unloaded world 'runtime_world'."));
  pump_client_until_ingame(harness.server, harness.client);
  ASSERT_EQ(harness.client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(harness.client.camera().world_id(), arv::server::DefaultWorldID);
  session = harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->world_id, arv::server::DefaultWorldID);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load runtime_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world unload runtime_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world delete runtime_world");
  EXPECT_EQ(result.status, arv::command::CommandStatus::Failure);
  EXPECT_TRUE(result_contains(result, "Run /world delete runtime_world --confirm"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world delete runtime_world --confirm");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_FALSE(std::filesystem::exists(
      harness.temp.path / "save" / "worlds" / "runtime_world"));
}

TEST(CommandTests, WorldCommandCreatesLoadsAndGeneratesVoidSpaceWorld) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create solar_system --type space --seed 4242");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Created world 'solar_system'."));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world info solar_system");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Generator: arvane:worldgen/void"));
  EXPECT_TRUE(result_contains(result, "Type: space"));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load solar_system");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const arv::server::WorldRecord* record =
      harness.server.worlds().find("solar_system");
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->world_type, "space");
  EXPECT_EQ(record->generator_id, std::string{arv::world::VoidWorldGenerationID});

  arv::world::WorldRuntime* runtime = harness.server.world_runtime(record->id);
  ASSERT_NE(runtime, nullptr);
  const arv::world::WorldGenerationConfig* generation =
      runtime->generation_config();
  ASSERT_NE(generation, nullptr);
  EXPECT_TRUE(generation->void_world);
  EXPECT_EQ(generation->dimension_id, std::string{arv::world::VoidWorldGenerationID});
  const arv::world::GameRuleState* placement_rule =
      runtime->game_rule("allowBlockPlacement");
  ASSERT_NE(placement_rule, nullptr);
  EXPECT_FALSE(placement_rule->bool_value);
  const arv::world::GameRuleState* spawning_rule =
      runtime->game_rule("doMobSpawning");
  ASSERT_NE(spawning_rule, nullptr);
  EXPECT_FALSE(spawning_rule->bool_value);
  const arv::world::WorldEnvironmentState& environment = runtime->environment();
  EXPECT_FLOAT_EQ(environment.gravity_scale, 0.0F);
  EXPECT_FALSE(environment.has_atmosphere);
  EXPECT_FALSE(environment.has_oxygen);
  EXPECT_EQ(environment.sky, "space");
  EXPECT_FLOAT_EQ(environment.ambient_light, 0.15F);

  const std::vector<arv::server::SpaceBodyRuntimeView> bodies =
      harness.server.space_bodies_for_world(record->id);
  ASSERT_EQ(bodies.size(), 1U);
  EXPECT_EQ(bodies.front().body_id, "arvane:space_body/luna");
  EXPECT_EQ(bodies.front().target_world_id, "arvane:world/luna");
  EXPECT_EQ(bodies.front().position.x, 2200.0F);
  EXPECT_EQ(bodies.front().position.z, 700.0F);
  EXPECT_FLOAT_EQ(bodies.front().visual_radius, 90.0F);
  EXPECT_FLOAT_EQ(bodies.front().entry_radius, 130.0F);
  EXPECT_TRUE(bodies.front().entry_enabled);
  EXPECT_EQ(
      harness.server.space_bodies_near(record->id, {2200.0F, 0.0F, 700.0F}).size(),
      1U);
  EXPECT_TRUE(
      harness.server.space_bodies_near(record->id, {2500.0F, 0.0F, 700.0F}).empty());

  auto palette = arv::world::resolve_world_generation_palette(
      *generation,
      harness.block_registry);
  ASSERT_TRUE(palette) << palette.error().message;
  arv::world::ChunkGenerationRequest request;
  request.world_id = record->id;
  request.seed = runtime->generation_seed();
  request.coord = {1, -1};
  request.config = *generation;
  request.palette = std::move(palette.value());
  const arv::world::WorldHeightSettings height = runtime->height_settings();
  request.min_chunk_y = height.min_chunk_y;
  request.max_chunk_y = height.max_chunk_y;
  auto generated = arv::world::generate_chunk(request);
  ASSERT_TRUE(generated) << generated.error().message;
  for (const arv::world::GeneratedChunkSection& section : generated.value().sections) {
    for (const arv::voxel::BlockVariantID block : section.blocks) {
      ASSERT_EQ(block, arv::voxel::AirBlockVariantID);
    }
  }

  const std::vector<arv::SessionID> session_ids =
      harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session =
      harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  const arv::PlayerID player_id = session->player_id;
  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp solar_system local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  pump_client_until_ingame(harness.server, harness.client);
  session = harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->world_id, record->id);
  EXPECT_EQ(harness.client.camera().world_id(), record->id);
  EXPECT_EQ(harness.client.world_environment_sky(), "space");
  EXPECT_TRUE(harness.client.space_environment());
  ASSERT_EQ(harness.client.space_bodies().size(), 1U);
  EXPECT_EQ(harness.client.space_bodies().front().body_id, "arvane:space_body/luna");
  EXPECT_EQ(harness.client.space_bodies().front().display_name, "Luna");
  EXPECT_EQ(harness.client.space_bodies().front().target_world_id, "arvane:world/luna");
  EXPECT_EQ(harness.client.space_bodies().front().position.x, 2200.0F);
  EXPECT_FLOAT_EQ(harness.client.space_bodies().front().visual_radius, 90.0F);
  EXPECT_FLOAT_EQ(harness.client.space_bodies().front().entry_radius, 130.0F);
  const arv::player::PlayerState* space_player = runtime->find_player(player_id);
  ASSERT_NE(space_player, nullptr);
  EXPECT_EQ(space_player->world_id, record->id);
  EXPECT_EQ(harness.server.voxel_world().find_player(player_id), nullptr);
}

TEST(CommandTests, WorldCommandTransitionMovesMountedVehicleAndPassenger) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create vehicle_world --seed 24681");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load vehicle_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const arv::server::WorldRecord* vehicle_record =
      harness.server.worlds().find("vehicle_world");
  ASSERT_NE(vehicle_record, nullptr);
  arv::world::WorldRuntime* target_world =
      harness.server.world_runtime(vehicle_record->id);
  ASSERT_NE(target_world, nullptr);

  arv::world::WorldRuntime& source_world = harness.server.voxel_world();
  arv::player::PlayerState& player_state = connected_player(harness.server);
  const arv::PlayerID player_id = player_state.player_id;
  const arv::player::ItemID stone_item =
      harness.item_registry.find_item("arvane:stone").value();
  ASSERT_TRUE(player_state.inventory.set_slot(
      0,
      arv::player::ItemStack{stone_item, 7}));
  const std::optional<arv::EntityID> source_player_entity =
      source_world.entity_for_player(player_state.player_id);
  ASSERT_TRUE(source_player_entity.has_value());

  const arv::EntityID source_vehicle = create_command_test_vehicle(
      source_world,
      harness.vehicle_registry,
      {2.0F, 20.0F, 0.0F});
  ASSERT_NE(source_vehicle, (arv::EntityID{}));
  arv::entity::VehiclePhysicsComponent* source_physics =
      source_world.find_entity_vehicle_physics(source_vehicle);
  ASSERT_NE(source_physics, nullptr);
  source_physics->fuel_item = "arvane:rocket_fuel/basic";
  source_physics->fuel_seconds = 12.5F;
  source_physics->fuel_capacity_seconds = 40.0F;
  source_physics->fuel_burn_per_second = 1.25F;
  arv::entity::EntityHealth* source_health =
      source_world.find_entity_health(source_vehicle);
  ASSERT_NE(source_health, nullptr);
  source_health->current = 12.0F;
  arv::entity::VehicleCargoComponent* source_cargo =
      source_world.find_entity_vehicle_cargo(source_vehicle);
  ASSERT_NE(source_cargo, nullptr);
  ASSERT_EQ(source_cargo->inventory.slot_count(), 6U);
  ASSERT_TRUE(source_cargo->inventory.set_slot(
      2,
      arv::player::ItemStack{stone_item, 13}));
  ASSERT_TRUE(source_world.mount_entity(
      source_vehicle,
      source_player_entity.value(),
      "driver"));
  EXPECT_EQ(
      source_world.mounted_rideable(source_player_entity.value()),
      std::optional<arv::EntityID>{source_vehicle});
  EXPECT_EQ(source_world.mounted_seat(source_player_entity.value()), "driver");

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp vehicle_world local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Teleported local_player to world 'vehicle_world'."));

  EXPECT_FALSE(source_world.entity_exists(source_vehicle));
  EXPECT_FALSE(source_world.entity_exists(source_player_entity.value()));
  EXPECT_FALSE(source_world.entity_for_player(player_id).has_value());

  const arv::player::PlayerState* target_player =
      target_world->find_player(player_id);
  ASSERT_NE(target_player, nullptr);
  EXPECT_EQ(target_player->world_id, vehicle_record->id);
  ASSERT_TRUE(target_player->inventory.slot(0).has_value());
  EXPECT_EQ(target_player->inventory.slot(0)->item, stone_item);
  EXPECT_EQ(target_player->inventory.slot(0)->count, 7U);

  const std::optional<arv::EntityID> target_player_entity =
      target_world->entity_for_player(player_id);
  ASSERT_TRUE(target_player_entity.has_value());
  const std::optional<arv::EntityID> target_vehicle =
      target_world->mounted_rideable(target_player_entity.value());
  ASSERT_TRUE(target_vehicle.has_value());
  EXPECT_EQ(target_world->mounted_seat(target_player_entity.value()), "driver");
  const arv::entity::EntityType* target_vehicle_type =
      target_world->find_entity_type(target_vehicle.value());
  ASSERT_NE(target_vehicle_type, nullptr);
  EXPECT_EQ(target_vehicle_type->type_name, "arvane:command_buggy");
  const arv::entity::VehiclePhysicsComponent* target_physics =
      target_world->find_entity_vehicle_physics(target_vehicle.value());
  ASSERT_NE(target_physics, nullptr);
  EXPECT_EQ(target_physics->definition, "arvane:command_buggy");
  EXPECT_EQ(target_physics->fuel_item, "arvane:rocket_fuel/basic");
  EXPECT_FLOAT_EQ(target_physics->fuel_seconds, 12.5F);
  EXPECT_FLOAT_EQ(target_physics->fuel_capacity_seconds, 40.0F);
  EXPECT_FLOAT_EQ(target_physics->fuel_burn_per_second, 1.25F);
  const arv::entity::EntityHealth* target_health =
      target_world->find_entity_health(target_vehicle.value());
  ASSERT_NE(target_health, nullptr);
  EXPECT_FLOAT_EQ(target_health->current, 12.0F);
  const arv::entity::VehicleCargoComponent* target_cargo =
      target_world->find_entity_vehicle_cargo(target_vehicle.value());
  ASSERT_NE(target_cargo, nullptr);
  ASSERT_EQ(target_cargo->inventory.slot_count(), 6U);
  ASSERT_TRUE(target_cargo->inventory.slot(2).has_value());
  EXPECT_EQ(target_cargo->inventory.slot(2)->item, stone_item);
  EXPECT_EQ(target_cargo->inventory.slot(2)->count, 13U);

  pump_client_until_ingame(harness.server, harness.client);
  ASSERT_EQ(harness.client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(harness.client.camera().world_id(), vehicle_record->id);
}

TEST(CommandTests, SpaceExitTransitionsMountedRocketToConfiguredSpaceWorld) {
  RunningServerHarness harness;

  arv::world::WorldRuntime& source_world = harness.server.voxel_world();
  arv::player::PlayerState& player = connected_player(harness.server);
  const arv::PlayerID player_id = player.player_id;
  player.position = {0.0F, 21.0F, 0.0F};
  const std::optional<arv::EntityID> player_entity =
      source_world.entity_for_player(player_id);
  ASSERT_TRUE(player_entity.has_value());
  ASSERT_TRUE(source_world.set_entity_position(
      player_entity.value(),
      player.position));

  const arv::EntityID rocket = create_command_test_rocket(
      source_world,
      harness.vehicle_registry,
      {2.0F, 21.0F, 0.0F});
  ASSERT_TRUE(source_world.mount_entity(
      rocket,
      player_entity.value(),
      "pilot"));
  arv::entity::VehiclePhysicsComponent* physics =
      source_world.find_entity_vehicle_physics(rocket);
  ASSERT_NE(physics, nullptr);
  physics->fuel_seconds = 20.0F;
  physics->fuel_capacity_seconds = 60.0F;
  physics->fuel_burn_per_second = 1.0F;
  physics->linear_velocity = {0.0F, 6.0F, 0.0F};
  arv::entity::EntityVelocity* velocity =
      source_world.find_entity_velocity(rocket);
  ASSERT_NE(velocity, nullptr);
  velocity->linear = physics->linear_velocity;

  harness.server.tick();

  const arv::server::WorldRecord* space_record =
      harness.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);
  EXPECT_EQ(space_record->world_type, "space");
  arv::world::WorldRuntime* space_world =
      harness.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  EXPECT_EQ(space_world->environment().sky, "space");

  EXPECT_FALSE(source_world.entity_exists(rocket));
  EXPECT_FALSE(source_world.entity_for_player(player_id).has_value());
  const arv::player::PlayerState* target_player =
      space_world->find_player(player_id);
  ASSERT_NE(target_player, nullptr);
  EXPECT_EQ(target_player->world_id, space_record->id);
  EXPECT_NEAR(target_player->velocity.y, 2.5F, 0.0001F);

  const std::optional<arv::EntityID> target_player_entity =
      space_world->entity_for_player(player_id);
  ASSERT_TRUE(target_player_entity.has_value());
  const std::optional<arv::EntityID> target_rocket =
      space_world->mounted_rideable(target_player_entity.value());
  ASSERT_TRUE(target_rocket.has_value());
  const arv::entity::EntityTransform* transform =
      space_world->find_entity_transform(target_rocket.value());
  ASSERT_NE(transform, nullptr);
  EXPECT_EQ(transform->position, (arv::Vec3f{11.0F, 22.0F, 33.0F}));
  const arv::entity::VehiclePhysicsComponent* target_physics =
      space_world->find_entity_vehicle_physics(target_rocket.value());
  ASSERT_NE(target_physics, nullptr);
  EXPECT_EQ(target_physics->definition, "arvane:command_rocket");
  EXPECT_FLOAT_EQ(target_physics->fuel_seconds, 20.0F);
  EXPECT_FLOAT_EQ(target_physics->fuel_capacity_seconds, 60.0F);
  EXPECT_FLOAT_EQ(target_physics->fuel_burn_per_second, 1.0F);
  EXPECT_NEAR(target_physics->linear_velocity.y, 2.5F, 0.0001F);

  pump_client_until_ingame(harness.server, harness.client);
  EXPECT_EQ(harness.client.camera().world_id(), space_record->id);
}

TEST(CommandTests, SpaceExitRequiresLaunchFuel) {
  RunningServerHarness harness;

  arv::world::WorldRuntime& source_world = harness.server.voxel_world();
  arv::player::PlayerState& player = connected_player(harness.server);
  const arv::PlayerID player_id = player.player_id;
  player.position = {0.0F, 21.0F, 0.0F};
  const std::optional<arv::EntityID> player_entity =
      source_world.entity_for_player(player_id);
  ASSERT_TRUE(player_entity.has_value());
  ASSERT_TRUE(source_world.set_entity_position(
      player_entity.value(),
      player.position));

  const arv::EntityID rocket = create_command_test_rocket(
      source_world,
      harness.vehicle_registry,
      {2.0F, 21.0F, 0.0F});
  ASSERT_TRUE(source_world.mount_entity(
      rocket,
      player_entity.value(),
      "pilot"));
  arv::entity::VehiclePhysicsComponent* physics =
      source_world.find_entity_vehicle_physics(rocket);
  ASSERT_NE(physics, nullptr);
  physics->fuel_seconds = 1.0F;
  physics->fuel_capacity_seconds = 60.0F;
  physics->fuel_burn_per_second = 1.0F;
  physics->linear_velocity = {0.0F, 6.0F, 0.0F};
  arv::entity::EntityVelocity* velocity =
      source_world.find_entity_velocity(rocket);
  ASSERT_NE(velocity, nullptr);
  velocity->linear = physics->linear_velocity;

  harness.server.tick();
  harness.client.poll_network();

  EXPECT_EQ(harness.server.worlds().find("solar_system"), nullptr);
  EXPECT_TRUE(source_world.entity_exists(rocket));
  ASSERT_TRUE(source_world.entity_for_player(player_id).has_value());
  EXPECT_EQ(
      source_world.mounted_rideable(player_entity.value()),
      std::optional<arv::EntityID>{rocket});
  EXPECT_TRUE(client_chat_contains(harness.client, "needs more fuel"));
}

TEST(CommandTests, SpaceExitRequiresSpaceCapableVehicleTag) {
  RunningServerHarness harness;

  arv::world::WorldRuntime& source_world = harness.server.voxel_world();
  arv::player::PlayerState& player = connected_player(harness.server);
  const arv::PlayerID player_id = player.player_id;
  player.position = {0.0F, 21.0F, 0.0F};
  const std::optional<arv::EntityID> player_entity =
      source_world.entity_for_player(player_id);
  ASSERT_TRUE(player_entity.has_value());
  ASSERT_TRUE(source_world.set_entity_position(
      player_entity.value(),
      player.position));

  const arv::EntityID buggy = create_command_test_vehicle(
      source_world,
      harness.vehicle_registry,
      {2.0F, 21.0F, 0.0F});
  ASSERT_TRUE(source_world.mount_entity(
      buggy,
      player_entity.value(),
      "driver"));
  arv::entity::VehiclePhysicsComponent* physics =
      source_world.find_entity_vehicle_physics(buggy);
  ASSERT_NE(physics, nullptr);
  physics->linear_velocity = {0.0F, 6.0F, 0.0F};
  arv::entity::EntityVelocity* velocity =
      source_world.find_entity_velocity(buggy);
  ASSERT_NE(velocity, nullptr);
  velocity->linear = physics->linear_velocity;

  harness.server.tick();
  harness.client.poll_network();

  EXPECT_EQ(harness.server.worlds().find("solar_system"), nullptr);
  EXPECT_TRUE(source_world.entity_exists(buggy));
  ASSERT_TRUE(source_world.entity_for_player(player_id).has_value());
  EXPECT_EQ(
      source_world.mounted_rideable(player_entity.value()),
      std::optional<arv::EntityID>{buggy});
  EXPECT_TRUE(client_chat_contains(harness.client, "not space-capable"));
}

TEST(CommandTests, WorldLoadRegeneratesMissingTargetWorldFromDefinition) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create luna --seed 424242");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  const arv::server::WorldRecord* created =
      harness.server.worlds().find("luna");
  ASSERT_NE(created, nullptr);
  const arv::WorldID luna_id = created->id;
  EXPECT_EQ(created->generator_id, "arvane:worldgen/dimension/overworld");

  const std::filesystem::path save_root = harness.temp.path / "save";
  const std::filesystem::path luna_root = save_root / "worlds" / "luna";
  std::error_code remove_error;
  const auto removed = std::filesystem::remove_all(luna_root, remove_error);
  ASSERT_FALSE(remove_error) << remove_error.message();
  EXPECT_GT(removed, 0U);
  EXPECT_FALSE(std::filesystem::exists(luna_root));

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load luna");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const arv::server::WorldRecord* regenerated =
      harness.server.worlds().find("luna");
  ASSERT_NE(regenerated, nullptr);
  EXPECT_EQ(regenerated->id, luna_id);
  EXPECT_EQ(regenerated->generator_id, "arvane:worldgen/dimension/luna");
  ASSERT_TRUE(regenerated->spawn.has_value());
  EXPECT_EQ(regenerated->spawn->world, luna_id);

  arv::world::WorldRuntime* luna_world =
      harness.server.world_runtime(luna_id);
  ASSERT_NE(luna_world, nullptr);
  ASSERT_NE(luna_world->generation_config(), nullptr);
  EXPECT_EQ(luna_world->generation_config()->dimension_id, "arvane:worldgen/dimension/luna");

  auto loaded_luna = arv::persistence::load_server_root(
      save_root,
      "luna",
      harness.block_registry,
      harness.item_registry);
  ASSERT_TRUE(loaded_luna) << loaded_luna.error().message;
  ASSERT_TRUE(loaded_luna.value().has_world_metadata);
  EXPECT_EQ(
      loaded_luna.value().world.generation_config.dimension_id,
      "arvane:worldgen/dimension/luna");
}

TEST(CommandTests, PlanetEntryTransitionsMountedRocketToTargetWorld) {
  RunningServerHarness harness;

  const arv::server::WorldRecord* space_record =
      create_and_load_command_test_space_world(harness);
  ASSERT_NE(space_record, nullptr);
  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp solar_system local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  pump_client_until_ingame(harness.server, harness.client);

  const std::vector<arv::SessionID> session_ids =
      harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session =
      harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  const arv::PlayerID player_id = session->player_id;
  const arv::player::ItemID stone_item =
      harness.item_registry.find_item("arvane:stone").value();
  EXPECT_EQ(session->world_id, space_record->id);

  arv::world::WorldRuntime* space_world =
      harness.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> player_entity =
      space_world->entity_for_player(player_id);
  ASSERT_TRUE(player_entity.has_value());

  const arv::EntityID rocket = create_command_test_rocket(
      *space_world,
      harness.vehicle_registry,
      {2200.0F, 0.0F, 700.0F});
  ASSERT_TRUE(space_world->mount_entity(
      rocket,
      player_entity.value(),
      "pilot"));
  arv::entity::VehiclePhysicsComponent* physics =
      space_world->find_entity_vehicle_physics(rocket);
  ASSERT_NE(physics, nullptr);
  physics->fuel_seconds = 17.5F;
  physics->fuel_capacity_seconds = 60.0F;
  physics->fuel_burn_per_second = 1.0F;
  arv::entity::EntityHealth* health =
      space_world->find_entity_health(rocket);
  ASSERT_NE(health, nullptr);
  health->current = 33.0F;
  arv::entity::VehicleCargoComponent* cargo =
      space_world->find_entity_vehicle_cargo(rocket);
  ASSERT_NE(cargo, nullptr);
  ASSERT_TRUE(cargo->inventory.set_slot(
      3,
      arv::player::ItemStack{stone_item, 5}));

  harness.server.tick();

  const arv::server::WorldRecord* luna_record =
      harness.server.worlds().find("luna");
  ASSERT_NE(luna_record, nullptr);
  EXPECT_EQ(luna_record->world_type, "default");
  EXPECT_EQ(luna_record->generator_id, "arvane:worldgen/dimension/luna");
  arv::world::WorldRuntime* luna_world =
      harness.server.world_runtime(luna_record->id);
  ASSERT_NE(luna_world, nullptr);
  ASSERT_NE(luna_world->generation_config(), nullptr);
  EXPECT_EQ(luna_world->generation_config()->dimension_id, "arvane:worldgen/dimension/luna");
  EXPECT_FLOAT_EQ(luna_world->environment().gravity_scale, 0.18F);
  EXPECT_FALSE(luna_world->environment().has_oxygen);

  EXPECT_FALSE(space_world->entity_exists(rocket));
  EXPECT_FALSE(space_world->entity_for_player(player_id).has_value());
  const arv::player::PlayerState* target_player =
      luna_world->find_player(player_id);
  ASSERT_NE(target_player, nullptr);
  EXPECT_EQ(target_player->world_id, luna_record->id);
  EXPECT_NEAR(target_player->velocity.y, -10.0F, 0.0001F);
  EXPECT_NEAR(
      target_player->yaw_radians,
      arv::degrees_to_radians(180.0F),
      0.0001F);

  const std::optional<arv::EntityID> target_player_entity =
      luna_world->entity_for_player(player_id);
  ASSERT_TRUE(target_player_entity.has_value());
  const std::optional<arv::EntityID> target_rocket =
      luna_world->mounted_rideable(target_player_entity.value());
  ASSERT_TRUE(target_rocket.has_value());
  EXPECT_EQ(luna_world->mounted_seat(target_player_entity.value()), "pilot");
  const arv::entity::EntityTransform* transform =
      luna_world->find_entity_transform(target_rocket.value());
  ASSERT_NE(transform, nullptr);
  EXPECT_EQ(transform->position, (arv::Vec3f{0.0F, 124.0F, 0.0F}));
  const arv::entity::VehiclePhysicsComponent* target_physics =
      luna_world->find_entity_vehicle_physics(target_rocket.value());
  ASSERT_NE(target_physics, nullptr);
  EXPECT_EQ(target_physics->definition, "arvane:command_rocket");
  EXPECT_FLOAT_EQ(target_physics->fuel_seconds, 17.5F);
  EXPECT_FLOAT_EQ(target_physics->fuel_capacity_seconds, 60.0F);
  EXPECT_FLOAT_EQ(target_physics->fuel_burn_per_second, 1.0F);
  const arv::entity::EntityHealth* target_health =
      luna_world->find_entity_health(target_rocket.value());
  ASSERT_NE(target_health, nullptr);
  EXPECT_FLOAT_EQ(target_health->current, 33.0F);
  const arv::entity::VehicleCargoComponent* target_cargo =
      luna_world->find_entity_vehicle_cargo(target_rocket.value());
  ASSERT_NE(target_cargo, nullptr);
  ASSERT_TRUE(target_cargo->inventory.slot(3).has_value());
  EXPECT_EQ(target_cargo->inventory.slot(3)->item, stone_item);
  EXPECT_EQ(target_cargo->inventory.slot(3)->count, 5U);

  pump_client_until_ingame(harness.server, harness.client);
  session = harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->world_id, luna_record->id);
  EXPECT_EQ(harness.client.camera().world_id(), luna_record->id);
  const std::optional<arv::EntityNetID> target_rocket_net =
      luna_world->network_id_for_entity(target_rocket.value());
  ASSERT_TRUE(target_rocket_net.has_value());
  EXPECT_EQ(harness.client.mounted_vehicle_id(), target_rocket_net);
  EXPECT_EQ(std::string{harness.client.mounted_seat_id()}, "pilot");
  EXPECT_TRUE(harness.client.mounted_controls_vehicle());
}

TEST(CommandTests, PlanetEntryRequiresEnabledBody) {
  RunningServerHarness harness;
  auto body =
      harness.space_definitions.space_bodies.find("arvane:space_body/luna");
  ASSERT_NE(body, harness.space_definitions.space_bodies.end());
  body->second.entry.enabled = false;

  const arv::server::WorldRecord* space_record =
      create_and_load_command_test_space_world(harness);
  ASSERT_NE(space_record, nullptr);
  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp solar_system local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const std::vector<arv::SessionID> session_ids =
      harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session =
      harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  const arv::PlayerID player_id = session->player_id;
  arv::world::WorldRuntime* space_world =
      harness.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> player_entity =
      space_world->entity_for_player(player_id);
  ASSERT_TRUE(player_entity.has_value());

  const arv::EntityID rocket = create_command_test_rocket(
      *space_world,
      harness.vehicle_registry,
      {2200.0F, 0.0F, 700.0F});
  ASSERT_TRUE(space_world->mount_entity(
      rocket,
      player_entity.value(),
      "pilot"));

  harness.server.tick();

  EXPECT_EQ(harness.server.worlds().find("luna"), nullptr);
  EXPECT_TRUE(space_world->entity_exists(rocket));
  ASSERT_TRUE(space_world->entity_for_player(player_id).has_value());
  EXPECT_EQ(
      space_world->mounted_rideable(player_entity.value()),
      std::optional<arv::EntityID>{rocket});
}

TEST(CommandTests, PlanetEntryRequiresSpaceCapableVehicleTag) {
  RunningServerHarness harness;

  const arv::server::WorldRecord* space_record =
      create_and_load_command_test_space_world(harness);
  ASSERT_NE(space_record, nullptr);
  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp solar_system local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const std::vector<arv::SessionID> session_ids =
      harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session =
      harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  const arv::PlayerID player_id = session->player_id;
  arv::world::WorldRuntime* space_world =
      harness.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> player_entity =
      space_world->entity_for_player(player_id);
  ASSERT_TRUE(player_entity.has_value());

  const arv::EntityID buggy = create_command_test_vehicle(
      *space_world,
      harness.vehicle_registry,
      {2200.0F, 0.0F, 700.0F});
  ASSERT_TRUE(space_world->mount_entity(
      buggy,
      player_entity.value(),
      "driver"));

  harness.server.tick();

  EXPECT_EQ(harness.server.worlds().find("luna"), nullptr);
  EXPECT_TRUE(space_world->entity_exists(buggy));
  ASSERT_TRUE(space_world->entity_for_player(player_id).has_value());
  EXPECT_EQ(
      space_world->mounted_rideable(player_entity.value()),
      std::optional<arv::EntityID>{buggy});
}

TEST(CommandTests, PlanetEntryReconnectsInTargetWorldAfterRestart) {
  RunningServerHarness harness;
  const auto body =
      harness.space_definitions.space_bodies.find("arvane:space_body/luna");
  ASSERT_NE(body, harness.space_definitions.space_bodies.end());
  EXPECT_EQ(body->second.target_world, "arvane:world/luna");
  const arv::world::WorldDefinition* target_definition =
      harness.space_definitions.find_world(body->second.target_world);
  ASSERT_NE(target_definition, nullptr);
  EXPECT_EQ(target_definition->kind, arv::world::WorldDefinitionKind::Planet);

  const arv::server::WorldRecord* space_record =
      create_and_load_command_test_space_world(harness);
  ASSERT_NE(space_record, nullptr);
  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp solar_system local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  pump_client_until_ingame(harness.server, harness.client);

  const std::vector<arv::SessionID> session_ids =
      harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session =
      harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  const arv::PlayerID player_id = session->player_id;
  arv::world::WorldRuntime* space_world =
      harness.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> player_entity =
      space_world->entity_for_player(player_id);
  ASSERT_TRUE(player_entity.has_value());

  const arv::EntityID rocket = create_command_test_rocket(
      *space_world,
      harness.vehicle_registry,
      {2200.0F, 0.0F, 700.0F});
  ASSERT_TRUE(space_world->mount_entity(
      rocket,
      player_entity.value(),
      "pilot"));
  harness.server.tick();

  const arv::server::WorldRecord* luna_record =
      harness.server.worlds().find("luna");
  ASSERT_NE(luna_record, nullptr);
  const arv::WorldID luna_world_id = luna_record->id;
  pump_client_until_ingame(harness.server, harness.client);
  ASSERT_EQ(harness.client.camera().world_id(), luna_world_id);

  const arv::client::LocalPlayerIdentity identity = harness.client.local_identity();
  const std::filesystem::path save_root = harness.temp.path / "save";
  const arv::command::SaveAllCommandResult saved =
      harness.server.save_all_worlds(true);
  ASSERT_TRUE(saved.success) << saved.message;
  EXPECT_GT(saved.saved_chunks, 0U);
  const std::filesystem::path luna_regions =
      save_root / "worlds" / "luna" / "regions";
  EXPECT_GT(count_region_files(luna_regions), 0U);
  auto loaded_luna = arv::persistence::load_server_root(
      save_root,
      "luna",
      harness.block_registry,
      harness.item_registry);
  ASSERT_TRUE(loaded_luna) << loaded_luna.error().message;
  EXPECT_FALSE(loaded_luna.value().chunks.empty());
  harness.server.shutdown();

  arv::net::InProcessTransportPair transports =
      arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime restarted(std::move(transports.server));
  arv::client::ClientRuntime reconnected(std::move(transports.client));
  reconnected.set_local_identity(identity);
  reconnected.configure_vehicles(&harness.vehicle_registry);

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &harness.block_registry;
  start_info.item_registry = &harness.item_registry;
  start_info.entity_registry = &harness.entity_registry;
  start_info.vehicle_registry = &harness.vehicle_registry;
  start_info.space_definitions = &harness.space_definitions;
  start_info.game_modes = &harness.game_modes;
  start_info.register_commands = arv::base::register_server_commands;
  start_info.world_name = "default";
  start_info.server_save_root = save_root;
  ASSERT_TRUE(restarted.start(start_info));

  const arv::server::WorldRecord* restarted_luna =
      restarted.worlds().find("luna");
  ASSERT_NE(restarted_luna, nullptr);
  ASSERT_EQ(restarted_luna->state, arv::server::WorldRuntimeState::Running);

  ASSERT_TRUE(reconnected.connect_integrated());
  complete_integrated_handshake(restarted, reconnected);
  ASSERT_EQ(reconnected.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(reconnected.camera().world_id(), luna_world_id);

  const std::vector<arv::SessionID> restarted_session_ids =
      restarted.sessions().session_ids();
  ASSERT_EQ(restarted_session_ids.size(), 1U);
  const arv::server::ServerSession* restarted_session =
      restarted.sessions().find(restarted_session_ids.front());
  ASSERT_NE(restarted_session, nullptr);
  EXPECT_EQ(restarted_session->world_id, luna_world_id);
}

TEST(CommandTests, WorldCommandTransitionReconnectsInTargetWorld) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create reconnect_world --seed 112233");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load reconnect_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  const arv::server::WorldRecord* record =
      harness.server.worlds().find("reconnect_world");
  ASSERT_NE(record, nullptr);
  const arv::WorldID reconnect_world_id = record->id;

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp reconnect_world local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  pump_client_until_ingame(harness.server, harness.client);
  ASSERT_EQ(harness.client.connection_state(), arv::net::ConnectionState::InGame);
  ASSERT_EQ(harness.client.camera().world_id(), reconnect_world_id);

  const arv::client::LocalPlayerIdentity identity = harness.client.local_identity();
  const std::filesystem::path save_root = harness.temp.path / "save";
  const arv::command::SaveAllCommandResult saved =
      harness.server.save_all_worlds(true);
  ASSERT_TRUE(saved.success) << saved.message;
  harness.server.shutdown();

  arv::net::InProcessTransportPair transports =
      arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime restarted(std::move(transports.server));
  arv::client::ClientRuntime reconnected(std::move(transports.client));
  reconnected.set_local_identity(identity);

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &harness.block_registry;
  start_info.item_registry = &harness.item_registry;
  start_info.entity_registry = &harness.entity_registry;
  start_info.vehicle_registry = &harness.vehicle_registry;
  start_info.game_modes = &harness.game_modes;
  start_info.register_commands = arv::base::register_server_commands;
  start_info.world_name = "default";
  start_info.server_save_root = save_root;
  ASSERT_TRUE(restarted.start(start_info));

  const arv::server::WorldRecord* restarted_record =
      restarted.worlds().find("reconnect_world");
  ASSERT_NE(restarted_record, nullptr);
  ASSERT_EQ(restarted_record->state, arv::server::WorldRuntimeState::Running);

  ASSERT_TRUE(reconnected.connect_integrated());
  complete_integrated_handshake(restarted, reconnected);
  ASSERT_EQ(reconnected.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(reconnected.camera().world_id(), reconnect_world_id);

  const std::vector<arv::SessionID> session_ids =
      restarted.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session =
      restarted.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->world_id, reconnect_world_id);
}

TEST(CommandTests, SaveAllWorldsPersistsLoadedNonDefaultWorlds) {
  RunningServerHarness harness(
      arv::io::MovementValidationConfig{},
      arv::default_spawn_location(),
      "No permission",
      nullptr,
      "Pretty Command Save");

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create runtime_save --seed 13579");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load runtime_save");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const arv::server::WorldRecord* runtime_record =
      harness.server.worlds().find("runtime_save");
  ASSERT_NE(runtime_record, nullptr);
  arv::world::WorldRuntime* runtime =
      harness.server.world_runtime(runtime_record->id);
  ASSERT_NE(runtime, nullptr);
  const std::optional<arv::voxel::BlockVariantID> stone =
      harness.block_registry.find_variant("arvane:stone");
  ASSERT_TRUE(stone.has_value());
  const arv::BlockPos target{{4, 5, 6}};
  (void)runtime->ensure_chunk(arv::block_to_chunk(target));
  ASSERT_TRUE(runtime->set_block(target, stone.value()));

  const arv::command::SaveAllCommandResult saved =
      harness.server.save_all_worlds(false);
  ASSERT_TRUE(saved.success) << saved.message;
  EXPECT_GE(saved.saved_chunks, 1U);

  auto loaded = arv::persistence::load_server_root(
      harness.temp.path / "save",
      "runtime_save",
      harness.block_registry,
      harness.item_registry);
  ASSERT_TRUE(loaded) << loaded.error().message;
  EXPECT_EQ(loaded.value().save.name, "Pretty Command Save");

  const arv::ChunkSectionCoord target_chunk = arv::block_to_chunk(target);
  const auto chunk = std::find_if(
      loaded.value().chunks.begin(),
      loaded.value().chunks.end(),
      [&](const arv::persistence::SavedChunkSection& saved_chunk) {
        return saved_chunk.coord == target_chunk;
      });
  ASSERT_NE(chunk, loaded.value().chunks.end());
  const arv::usize block_index =
      arv::voxel::local_index(arv::block_to_local(target));
  EXPECT_EQ(chunk->blocks[block_index], stone.value());
}

TEST(CommandTests, WorldCommandValidatesNamesAndPermissions) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());

  arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/world list");
  EXPECT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);
  EXPECT_TRUE(result_contains(
      result,
      "You do not have permission to use this world command."));

  player.permission_overrides.custom_overrides["arv.command.world"] =
      arv::player::TriState::Allow;
  result = harness.server.execute_command(sender.value(), "/world list");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Worlds"));

  result = harness.server.execute_command(
      sender.value(),
      "/world create player_world");
  EXPECT_EQ(result.status, arv::command::CommandStatus::PermissionDenied);

  player.permission_overrides.custom_overrides["arv.command.world.create"] =
      arv::player::TriState::Allow;
  result = harness.server.execute_command(
      sender.value(),
      "/world create BadName");
  EXPECT_EQ(result.status, arv::command::CommandStatus::UsageError);
  EXPECT_TRUE(result_contains(result, "lowercase"));

  result = harness.server.execute_command(
      sender.value(),
      "/world create ../escape");
  EXPECT_EQ(result.status, arv::command::CommandStatus::UsageError);
  EXPECT_TRUE(result_contains(result, "path traversal"));

  result = harness.server.execute_command(
      sender.value(),
      "/world create player_world");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Created world 'player_world'."));
}

TEST(CommandTests, WorldSetspawnUpdatesDefaultWorldSpawn) {
  RunningServerHarness harness;
  arv::player::PlayerState& player = connected_player(harness.server);
  player.position = {-6.5F, 72.0F, 11.25F};
  player.yaw_radians = 0.5F;
  player.pitch_radians = -0.25F;
  player.permission_overrides.custom_overrides["arv.command.world.setspawn"] =
      arv::player::TriState::Allow;

  std::optional<arv::command::CommandSender> sender =
      connected_player_sender(harness);
  ASSERT_TRUE(sender.has_value());
  const arv::command::CommandResult result =
      harness.server.execute_command(sender.value(), "/world setspawn");

  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  const arv::Location spawn = harness.server.voxel_world().spawn_point();
  EXPECT_EQ(arv::location_position(spawn), (arv::Vec3f{-6.5F, 72.0F, 11.25F}));
  EXPECT_FLOAT_EQ(spawn.yaw_radians, 0.5F);
  EXPECT_FLOAT_EQ(spawn.pitch_radians, -0.25F);
  EXPECT_TRUE(std::filesystem::is_regular_file(
      harness.temp.path / "save" / "worlds" / "default" / "world.json"));
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

TEST(CommandTests, ExplodeCommandUsesSenderWorldAfterWorldTeleport) {
  RunningServerHarness harness;

  arv::command::CommandResult result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world create orbit_explode --seed 6790");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world load orbit_explode");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);

  const arv::server::WorldRecord* runtime_record =
      harness.server.worlds().find("orbit_explode");
  ASSERT_NE(runtime_record, nullptr);
  const arv::WorldID runtime_world_id = runtime_record->id;

  result = harness.server.execute_command(
      arv::server::server_console_command_sender(),
      "/world tp orbit_explode local_player");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  for (int i = 0; i < 3; ++i) {
    harness.client.fixed_tick(harness.server.tick_index());
  }

  const std::vector<arv::SessionID> session_ids =
      harness.server.sessions().session_ids();
  ASSERT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session =
      harness.server.sessions().find(session_ids.front());
  ASSERT_NE(session, nullptr);
  ASSERT_EQ(session->world_id, runtime_world_id);

  arv::world::WorldRuntime* runtime_world =
      harness.server.world_runtime(runtime_world_id);
  ASSERT_NE(runtime_world, nullptr);
  arv::player::PlayerState* runtime_player =
      runtime_world->find_player(session->player_id);
  ASSERT_NE(runtime_player, nullptr);
  runtime_player->position = {1.5F, 2.0F, 3.5F};
  runtime_player->permission_overrides.custom_overrides["command.explode"] =
      arv::player::TriState::Allow;

  const std::optional<arv::voxel::BlockVariantID> stone =
      harness.block_registry.find_variant("arvane:stone");
  ASSERT_TRUE(stone.has_value());
  const arv::BlockPos target{{1, 2, 3}};
  (void)harness.server.voxel_world().ensure_chunk_section({{0, 0, 0}});
  (void)runtime_world->ensure_chunk_section({{0, 0, 0}});
  ASSERT_TRUE(harness.server.voxel_world().set_block(target, stone.value()));
  ASSERT_TRUE(runtime_world->set_block(target, stone.value()));

  std::optional<arv::command::CommandSender> sender =
      harness.server.command_sender_for_session(session_ids.front());
  ASSERT_TRUE(sender.has_value());
  result = harness.server.execute_command(
      sender.value(),
      "/explode ~ ~ ~ arvane:debug --no-drops --seed 125");
  ASSERT_EQ(result.status, arv::command::CommandStatus::Success);
  EXPECT_TRUE(result_contains(result, "Created explosion arvane:debug"));
  EXPECT_EQ(runtime_world->get_block(target), arv::voxel::AirBlockVariantID);
  EXPECT_EQ(harness.server.voxel_world().get_block(target), stone.value());
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
