#include "arv/client/client_runtime.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_state.hpp"
#include "arv/server/server_runtime.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <numbers>

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

} // namespace

TEST(PlayerInteractionTests, SecondaryPlaceFlowsThroughServerAuthorityAndConsumesHeldStack) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
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

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
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
