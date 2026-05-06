#include "arv/client/client_runtime.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_state.hpp"
#include "arv/server/server_runtime.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <numbers>
#include <string_view>
#include <thread>

namespace {

void complete_integrated_handshake(
    arv::server::ServerRuntime& server,
    arv::client::ClientRuntime& client) {
  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
}

arv::player::PlayerState& connected_player(arv::server::ServerRuntime& server) {
  const std::vector<arv::SessionID> session_ids = server.sessions().session_ids();
  EXPECT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session = server.sessions().find(session_ids.front());
  EXPECT_NE(session, nullptr);
  arv::player::PlayerState* player_state = server.voxel_world().find_player(session->player_id);
  EXPECT_NE(player_state, nullptr);
  return *player_state;
}

void prepare_test_player(
    arv::server::ServerRuntime& server,
    const arv::player::ItemRegistry& item_registry) {
  arv::player::PlayerState& player_state = connected_player(server);
  player_state.position = {0.5F, 0.0F, 0.5F};
  player_state.velocity = {};
  player_state.yaw_radians = std::numbers::pi_v<float> * 0.5F;
  player_state.pitch_radians = 0.0F;
  player_state.grounded = false;
  player_state.selected_hotbar_slot = 0;
  ASSERT_TRUE(player_state.inventory.set_slot(
      0,
      arv::player::ItemStack{
          item_registry.find_item("arvane:stone").value(),
          2}));
}

void set_test_player_held_item(
    arv::server::ServerRuntime& server,
    const arv::player::ItemRegistry& item_registry,
    std::string_view item_name,
    arv::u16 count = 1U) {
  arv::player::PlayerState& player_state = connected_player(server);
  player_state.position = {0.5F, 0.0F, 0.5F};
  player_state.velocity = {};
  player_state.yaw_radians = std::numbers::pi_v<float> * 0.5F;
  player_state.pitch_radians = 0.0F;
  player_state.grounded = false;
  player_state.selected_hotbar_slot = 0;
  ASSERT_TRUE(player_state.inventory.set_slot(
      0,
      arv::player::ItemStack{
          item_registry.find_item(item_name).value(),
          count}));
}

arv::voxel::BlockRegistry make_timed_break_block_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  arv::voxel::BlockDefinition stone =
      arv::voxel::make_full_cube_definition("arvane:stone");
  stone.properties.hardness = 0.2F;
  stone.tags.push_back("arvane:mineable/pickaxe");
  definitions.push_back(std::move(stone));
  arv::voxel::BlockDefinition dirt =
      arv::voxel::make_full_cube_definition("arvane:dirt");
  dirt.properties.hardness = 0.05F;
  dirt.properties.drops_with_wrong_tool = true;
  dirt.tags.push_back("arvane:mineable/shovel");
  definitions.push_back(std::move(dirt));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

} // namespace

TEST(PlayerInteractionTests, SecondaryPlaceFlowsThroughServerAuthorityAndConsumesHeldStack) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.hand_speed_multiplier = 1.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{2, 1, 0}}), stone);
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(player_state.inventory.slot(0)->count, 1U);
}

TEST(PlayerInteractionTests, PrimaryBreakRemovesTargetBlockThroughServerAuthority) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.hand_speed_multiplier = 1.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
}

TEST(PlayerInteractionTests, PrimaryBreakAccumulatesProgressBeforeRemovingHardBlock) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.hand_speed_multiplier = 1.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true, .primary_held = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), dirt);

  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.block_break_progress().active);
  EXPECT_EQ(client.block_break_progress().block, (arv::BlockPos{{3, 1, 0}}));
  EXPECT_GT(client.block_break_progress().progress, 0.0F);
  server.tick();

  for (int tick = 0; tick < 4; ++tick) {
    client.fixed_tick(server.tick_index());
    server.tick();
  }

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
}

TEST(PlayerInteractionTests, MatchingToolBreaksMineableBlocksFasterThanHand) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.base_seconds_per_hardness = 1.0F;
  start_info.block_breaking.hand_speed_multiplier = 0.2F;
  start_info.block_breaking.wrong_tool_speed_multiplier = 0.2F;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, stone));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true, .primary_held = true});
  for (int tick = 0; tick < 4; ++tick) {
    client.fixed_tick(server.tick_index());
    server.tick();
  }
  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), stone);

  set_test_player_held_item(server, item_registry, "arvane:wood_pickaxe");
  for (int tick = 0; tick < 4; ++tick) {
    client.fixed_tick(server.tick_index());
    server.tick();
  }

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
}

TEST(PlayerInteractionTests, BlockDropsRequireCorrectToolWhenConfiguredByMineableTags) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.base_seconds_per_hardness = 0.0F;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, stone));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
  EXPECT_TRUE(server.dropped_item_stacks().empty());
}

TEST(PlayerInteractionTests, BlockDropsSpawnWhenBrokenWithMatchingToolOrNoRequiredTool) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.base_seconds_per_hardness = 0.0F;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, stone));
  set_test_player_held_item(server, item_registry, "arvane:wood_pickaxe");
  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);
  EXPECT_EQ(
      server.dropped_item_stacks().front().stack.item,
      item_registry.find_item("arvane:stone").value());

  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);
  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  const std::vector<arv::server::DroppedItemStack> drops = server.dropped_item_stacks();
  ASSERT_EQ(drops.size(), 2U);
  const arv::player::ItemID dirt_item = item_registry.find_item("arvane:dirt").value();
  EXPECT_NE(
      std::find_if(
          drops.begin(),
          drops.end(),
          [dirt_item](const arv::server::DroppedItemStack& drop) {
            return drop.stack.item == dirt_item;
          }),
      drops.end());
}

TEST(PlayerInteractionTests, BucketPicksUpSourceWaterAndBecomesWaterBucket) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.simulation_distance = 4;
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry,
      arv::player::default_player_tuning(),
      water,
      streaming_config}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().place_fluid_source({{3, 1, 0}}, water));
  set_test_player_held_item(server, item_registry, "arvane:bucket");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_TRUE(server.voxel_world().get_fluid({{3, 1, 0}}).empty());
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(
      player_state.inventory.slot(0)->item,
      item_registry.find_item("arvane:water_bucket").value());
}

TEST(PlayerInteractionTests, WaterBucketPlacesSourceWaterAndBecomesBucket) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  set_test_player_held_item(server, item_registry, "arvane:water_bucket");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_TRUE(server.voxel_world().get_fluid({{2, 1, 0}}).source());
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(
      player_state.inventory.slot(0)->item,
      item_registry.find_item("arvane:bucket").value());
}

TEST(PlayerInteractionTests, WaterBucketReplacesPartialWaterWithSource) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  ASSERT_TRUE(server.voxel_world().set_fluid(
      {{2, 1, 0}},
      arv::voxel::make_flowing_fluid(water, 4U),
      water));
  set_test_player_held_item(server, item_registry, "arvane:water_bucket");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_TRUE(server.voxel_world().get_fluid({{2, 1, 0}}).source());
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(
      player_state.inventory.slot(0)->item,
      item_registry.find_item("arvane:bucket").value());
}

TEST(PlayerInteractionTests, PlacedWaterFlowsThroughActiveServerSimulation) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  set_test_player_held_item(server, item_registry, "arvane:water_bucket");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();
  ASSERT_TRUE(server.voxel_world().get_fluid({{2, 1, 0}}).source());

  bool flowed = false;
  for (arv::u32 tick = 0; tick < 300U; ++tick) {
    client.fixed_tick(server.tick_index());
    server.tick();
    const arv::voxel::FluidCell below = server.voxel_world().get_fluid({{2, 0, 0}});
    if (below.type == water.type && below.falling()) {
      flowed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(flowed);
}

TEST(PlayerInteractionTests, RegionDenyPreventsWorldModification) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  arv::world::Region spawn;
  spawn.name = "spawn";
  spawn.world = arv::server::DefaultWorldID;
  spawn.priority = 100;
  spawn.shape.min = {-16.0F, -4.0F, -16.0F};
  spawn.shape.max = {16.0F, 16.0F, 16.0F};
  spawn.core.can_modify_world = arv::player::TriState::Deny;
  ASSERT_NE(server.voxel_world().regions().add_region(std::move(spawn)).value, 0U);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), dirt);
}

TEST(PlayerInteractionTests, PlacementOutsideConfiguredReachIsRejected) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{10, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{9, 1, 0}}), arv::voxel::AirBlockVariantID);
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(player_state.inventory.slot(0)->count, 2U);
}

TEST(PlayerInteractionTests, InventorySlotClicksMutateAuthoritativeServerInventory) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  ASSERT_TRUE(player_state.inventory.set_slot(0, arv::player::ItemStack{stone_item, 8}));
  ASSERT_TRUE(player_state.inventory.clear_slot(1));

  const auto pick = server.apply_player_inventory_click(
      player_state.player_id,
      0,
      arv::player::InventoryClickButton::Primary);
  EXPECT_EQ(pick.kind, arv::player::InventoryOperationResultKind::Success);
  EXPECT_FALSE(player_state.inventory.slot(0).has_value());
  ASSERT_TRUE(player_state.inventory_cursor_stack.has_value());
  EXPECT_EQ(player_state.inventory_cursor_stack->count, 8U);

  const auto place = server.apply_player_inventory_click(
      player_state.player_id,
      1,
      arv::player::InventoryClickButton::Primary);
  EXPECT_EQ(place.kind, arv::player::InventoryOperationResultKind::Success);
  EXPECT_FALSE(player_state.inventory_cursor_stack.has_value());
  ASSERT_TRUE(player_state.inventory.slot(1).has_value());
  EXPECT_EQ(player_state.inventory.slot(1)->item, stone_item);
  EXPECT_EQ(player_state.inventory.slot(1)->count, 8U);
}

TEST(PlayerInteractionTests, DroppingInventoryCursorRecordsServerDroppedStack) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  player_state.position = {4.0F, 5.0F, 6.0F};
  player_state.inventory_cursor_stack = arv::player::ItemStack{stone_item, 3};

  const std::optional<arv::player::ItemStack> dropped =
      server.drop_player_inventory_cursor(player_state.player_id);

  ASSERT_TRUE(dropped.has_value());
  EXPECT_EQ(dropped->item, stone_item);
  EXPECT_EQ(dropped->count, 3U);
  EXPECT_FALSE(player_state.inventory_cursor_stack.has_value());
  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);
  EXPECT_EQ(server.dropped_item_stacks().front().world_id, player_state.world_id);
  EXPECT_EQ(server.dropped_item_stacks().front().position, player_state.position);
  EXPECT_EQ(server.dropped_item_stacks().front().stack, dropped.value());
}

TEST(PlayerInteractionTests, DroppingOneFromInventoryCursorLeavesRemainderCarried) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  player_state.position = {4.0F, 5.0F, 6.0F};
  player_state.inventory_cursor_stack = arv::player::ItemStack{stone_item, 3};

  const std::optional<arv::player::ItemStack> dropped =
      server.drop_one_from_player_inventory_cursor(player_state.player_id);

  ASSERT_TRUE(dropped.has_value());
  EXPECT_EQ(dropped->item, stone_item);
  EXPECT_EQ(dropped->count, 1U);
  ASSERT_TRUE(player_state.inventory_cursor_stack.has_value());
  EXPECT_EQ(player_state.inventory_cursor_stack->item, stone_item);
  EXPECT_EQ(player_state.inventory_cursor_stack->count, 2U);
  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);
  EXPECT_EQ(server.dropped_item_stacks().front().world_id, player_state.world_id);
  EXPECT_EQ(server.dropped_item_stacks().front().position, player_state.position);
  EXPECT_EQ(server.dropped_item_stacks().front().stack, dropped.value());
}

TEST(PlayerInteractionTests, NearbyDroppedItemsArePickedUpAndRemoved) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.dropped_items.pickup_delay_seconds = 0.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  player_state.position = {4.0F, 5.0F, 6.0F};
  for (arv::usize slot = 0; slot < player_state.inventory.slot_count(); ++slot) {
    ASSERT_TRUE(player_state.inventory.clear_slot(slot));
  }

  (void)server.voxel_world().create_dropped_item(
      arv::player::ItemStack{stone_item, 3},
      player_state.position,
      {},
      0.0F);
  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);

  server.tick();

  EXPECT_TRUE(server.dropped_item_stacks().empty());
  const arv::player::PlayerState& updated_player_state = connected_player(server);
  ASSERT_TRUE(updated_player_state.inventory.slot(0).has_value());
  EXPECT_EQ(updated_player_state.inventory.slot(0)->item, stone_item);
  EXPECT_EQ(updated_player_state.inventory.slot(0)->count, 3U);
}
