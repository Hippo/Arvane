#include "arv/client/client_runtime.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/message_type.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/world/world_generation.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <span>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace {

arv::player::PlayerState& connected_player(arv::server::ServerRuntime& server) {
  const std::vector<arv::SessionID> session_ids = server.sessions().session_ids();
  EXPECT_EQ(session_ids.size(), 1U);
  const arv::server::ServerSession* session = server.sessions().find(session_ids.front());
  EXPECT_NE(session, nullptr);
  arv::player::PlayerState* player_state = server.voxel_world().find_player(session->player_id);
  EXPECT_NE(player_state, nullptr);
  return *player_state;
}

void send_raw_client_hello(
    arv::net::ITransport& client_transport,
    arv::net::ConnectionID connection_id) {
  const arv::protocol::ClientHello hello;
  const std::vector<arv::u8> payload = arv::protocol::serialize_client_hello(hello);
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       arv::protocol::MessageType::ClientHello,
       0,
       0,
       0,
       0},
      payload);
  (void)client_transport.send(
      {connection_id, arv::protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
}

arv::u32 player_correction_count(std::span<const arv::net::NetworkEvent> events) {
  arv::u32 count = 0;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (decoded &&
        decoded.value().envelope.message_type == arv::protocol::MessageType::PlayerCorrection) {
      ++count;
    }
  }
  return count;
}

arv::server::ServerChunkGenerationSettings make_generation_settings(
    const arv::voxel::BlockRegistry& block_registry) {
  auto config = arv::world::parse_world_generation_yaml(R"yaml(
id: arvane:test_overworld
version: 1
seed_salt: "test"
vertical:
  min_y: -16
  max_y: 64
  sea_level: 16
noise:
  terrain:
    frequency: 0.1
density:
  base_height: 8
  height_scale: 12
surface:
  rules:
    - layers:
        - block: arvane:grass_block
          depth: 1
        - block: arvane:dirt
          depth: 2
        - block: arvane:stone
          depth: remaining
)yaml");
  EXPECT_TRUE(config);

  auto palette = arv::world::resolve_world_generation_palette(config.value(), block_registry);
  EXPECT_TRUE(palette);

  arv::server::ServerChunkGenerationSettings settings;
  settings.enabled = true;
  settings.seed = 1234;
  settings.config = std::move(config.value());
  settings.palette = std::move(palette.value());
  return settings;
}

} // namespace

TEST(IntegratedHandshakeTests, ClientAndServerReachInGameThroughProtocolHandshake) {
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
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::ProtocolHandshake);

  server.tick();
  client.fixed_tick(server.tick_index());

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(client.camera().world_id(), arv::server::DefaultWorldID);
  EXPECT_EQ(server.sessions().size(), 1U);
  const arv::server::ServerSession* session = server.sessions().first();
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->connection_state, arv::net::ConnectionState::InGame);
  EXPECT_NE(server.worlds().find(arv::server::DefaultWorldID), nullptr);
  EXPECT_TRUE(client.has_authoritative_inventory());
  EXPECT_GT(client.replicated_entity_count(), 0U);
}

TEST(IntegratedHandshakeTests, InventoryActionsFlowThroughAuthoritativeProtocolUpdates) {
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

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_TRUE(client.has_authoritative_inventory());
  ASSERT_TRUE(client.inventory().slot(0).has_value());

  ASSERT_TRUE(client.request_inventory_slot_click(
      0,
      arv::player::InventoryClickButton::Primary));
  server.tick();
  client.fixed_tick(server.tick_index());

  arv::player::PlayerState& player = connected_player(server);
  ASSERT_TRUE(player.inventory_cursor_stack.has_value());
  EXPECT_FALSE(player.inventory.slot(0).has_value());
  ASSERT_TRUE(client.inventory_cursor_stack().has_value());
  EXPECT_FALSE(client.inventory().slot(0).has_value());
  EXPECT_GE(client.inventory_revision(), 2U);
}

TEST(IntegratedHandshakeTests, BlockUpdatesReplicateAfterChunkStreaming) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID dirt =
      block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry,
      arv::player::default_player_tuning(),
      arv::voxel::make_water_fluid_definition(),
      streaming_config}));

  const arv::ChunkCoord expected_chunk{{0, 1, -2}};
  (void)server.voxel_world().ensure_chunk(expected_chunk);
  const arv::BlockPos changed_block =
      arv::chunk_local_to_block(expected_chunk, {{1, 2, 3}});
  ASSERT_TRUE(server.voxel_world().set_block(changed_block, stone));
  (void)server.voxel_world().drain_replicated_block_updates();

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.drain_received_chunk_data().size(), 1U);

  ASSERT_TRUE(server.voxel_world().set_block(changed_block, dirt));
  server.tick();
  client.fixed_tick(server.tick_index());
  std::vector<arv::protocol::BlockUpdateBatch> updates =
      client.drain_received_block_updates();
  ASSERT_EQ(updates.size(), 1U);
  ASSERT_EQ(updates.front().updates.size(), 1U);
  EXPECT_EQ(updates.front().world_id, arv::server::DefaultWorldID);
  EXPECT_EQ(updates.front().updates.front().pos, changed_block);
  EXPECT_EQ(updates.front().updates.front().new_state, dirt);
}

TEST(IntegratedHandshakeTests, DisconnectCleansSessionAndWorldLocalPlayerEntity) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(*client_transport, connect_result.value());

  server.tick();
  (void)client_transport->poll_events();
  ASSERT_EQ(server.sessions().size(), 1U);
  EXPECT_EQ(server.voxel_world().player_count(), 1U);
  EXPECT_EQ(server.voxel_world().entity_count(), 1U);

  client_transport->shutdown();
  server.tick();

  EXPECT_EQ(server.sessions().size(), 0U);
  EXPECT_EQ(server.voxel_world().player_count(), 0U);
  EXPECT_EQ(server.voxel_world().entity_count(), 0U);
}

TEST(IntegratedHandshakeTests, ClientDoesNotOverwriteLoadedHotbarSlotBeforeCorrection) {
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

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);

  connected_player(server).selected_hotbar_slot = 4;

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_EQ(client.selected_hotbar_slot(), 4U);

  server.tick();
  EXPECT_EQ(connected_player(server).selected_hotbar_slot, 4U);
}

TEST(IntegratedHandshakeTests, ServerSkipsUnchangedPlayerCorrections) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(*client_transport, connect_result.value());

  server.tick();
  (void)client_transport->poll_events();
  arv::player::PlayerState& player = connected_player(server);
  ASSERT_TRUE(server.set_player_game_mode(player.player_id, arv::player::NoClipGameModeID));

  server.tick();
  const std::vector<arv::net::NetworkEvent> changed_events = client_transport->poll_events();
  EXPECT_GT(player_correction_count(changed_events), 0U);

  server.tick();
  const std::vector<arv::net::NetworkEvent> unchanged_events = client_transport->poll_events();
  EXPECT_EQ(player_correction_count(unchanged_events), 0U);
}

TEST(IntegratedHandshakeTests, ClientReceivesChunkDataOnlyAfterInterestUpdate) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry,
      arv::player::default_player_tuning(),
      arv::voxel::make_water_fluid_definition(),
      streaming_config}));

  const arv::ChunkCoord expected_chunk{{0, 1, -2}};
  (void)server.voxel_world().ensure_chunk(expected_chunk);
  ASSERT_TRUE(server.voxel_world().set_block(
      arv::chunk_local_to_block(expected_chunk, {{1, 2, 3}}),
      stone));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  std::vector<arv::protocol::ChunkData> chunks = client.drain_received_chunk_data();
  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
  EXPECT_EQ(chunks.front().coord, expected_chunk);
  EXPECT_EQ(
      chunks.front().blocks[arv::voxel::local_index({{1, 2, 3}})],
      stone);

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_data().empty());
}

TEST(IntegratedHandshakeTests, ClientReceivesGeneratedChunkAfterAsyncGeneration) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry,
      arv::player::default_player_tuning(),
      arv::voxel::make_water_fluid_definition(),
      streaming_config,
      make_generation_settings(block_registry)}));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_data().empty());

  std::vector<arv::protocol::ChunkData> chunks;
  for (int attempt = 0; attempt < 100 && chunks.empty(); ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    chunks = client.drain_received_chunk_data();
    if (chunks.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
}
