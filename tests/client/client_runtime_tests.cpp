#include "arv/client/client_runtime.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/world_join.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

void send_to_client(
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::protocol::MessageType message_type,
    std::vector<arv::u8> payload,
    arv::protocol::ReliabilityClass reliability =
        arv::protocol::ReliabilityClass::ReliableOrdered) {
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  (void)server_transport.send({connection_id, reliability, std::move(packet)});
}

void enter_in_game(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::Vec3f spawn_position = {8.0F, 20.0F, -20.0F}) {
  (void)server_transport.poll_events();

  arv::protocol::ServerHello hello;
  hello.accepted = true;
  hello.assigned_session_id = arv::SessionID{1};
  hello.assigned_player_id = arv::PlayerID{1};
  hello.initial_world_id = arv::WorldID{1};
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ServerHello,
      arv::protocol::serialize_server_hello(hello));

  arv::protocol::WorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{1};
  join.player_id = arv::PlayerID{1};
  join.local_entity_id = arv::EntityNetID{1};
  join.spawn_position = spawn_position;
  join.initial_tick = 1;
  join.tick_rate = arv::DefaultBaseTps;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::WorldJoinResult,
      arv::protocol::serialize_world_join_result(join));

  client.fixed_tick(1);
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
}

} // namespace

TEST(ClientRuntimeTests, ChatDraftIgnoresOpeningKeyThenAcceptsTextEditingAndSubmit) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{
      .open_chat_pressed = true,
      .text_input = "t"});
  EXPECT_TRUE(client.chat_open());
  EXPECT_TRUE(client.chat_draft().empty());

  client.submit_input(arv::client::InputFrame{.text_input = "hello"});
  EXPECT_EQ(client.chat_draft(), "hello");

  client.submit_input(arv::client::InputFrame{.text_backspace_pressed = true});
  EXPECT_EQ(client.chat_draft(), "hell");

  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});
  EXPECT_FALSE(client.chat_open());
  ASSERT_EQ(client.chat_messages().size(), 1U);
  EXPECT_EQ(client.chat_messages().front(), "hell");
}

TEST(ClientRuntimeTests, SlashTextOpensChatWithCommandPrefix) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.text_input = "/"});

  EXPECT_TRUE(client.chat_open());
  EXPECT_EQ(client.chat_draft(), "/");
  EXPECT_TRUE(client.chat_messages().empty());
}

TEST(ClientRuntimeTests, WorldJoinKeepsLocalPlayerBodyAtServerSpawn) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);

  enter_in_game(client, *server_transport, connection_id.value(), {80.0F, 40.0F, -96.0F});
  client.fixed_tick(2);

  EXPECT_FLOAT_EQ(client.camera().position().x, 80.0F);
  EXPECT_FLOAT_EQ(
      client.camera().position().y,
      40.0F + arv::player::DefaultStandingEyeHeight);
  EXPECT_FLOAT_EQ(client.camera().position().z, -96.0F);
  ASSERT_TRUE(client.local_player_id().has_value());
  EXPECT_EQ(client.local_player_id().value(), (arv::PlayerID{1}));
  ASSERT_TRUE(client.local_entity_id().has_value());
  EXPECT_EQ(client.local_entity_id().value(), (arv::EntityNetID{1}));
}

TEST(ClientRuntimeTests, ReplicatedEntitiesIgnoreUnknownAndStaleSnapshots) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::EntitySnapshot unknown_snapshot;
  unknown_snapshot.world_id = arv::WorldID{1};
  unknown_snapshot.server_tick = 10;
  unknown_snapshot.entities.push_back({arv::EntityNetID{99},
                                       {1.0F, 2.0F, 3.0F},
                                       {},
                                       {},
                                       0});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::EntitySnapshot,
      arv::protocol::serialize_entity_snapshot(unknown_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(2);
  EXPECT_EQ(client.replicated_entity_count(), 0U);

  arv::protocol::EntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {0.0F, 1.0F, 2.0F};
  spawn.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::EntitySpawn,
      arv::protocol::serialize_entity_spawn(spawn));
  client.fixed_tick(3);
  ASSERT_EQ(client.replicated_entity_count(), 1U);

  arv::protocol::EntitySnapshot fresh_snapshot;
  fresh_snapshot.world_id = arv::WorldID{1};
  fresh_snapshot.server_tick = 20;
  fresh_snapshot.entities.push_back({arv::EntityNetID{5},
                                     {10.0F, 11.0F, 12.0F},
                                     {1.0F, 0.0F, 0.0F},
                                     {0.0F, 0.0F, 0.0F, 1.0F},
                                     0,
                                     "walking",
                                     0.5F,
                                     {5.0F, 15.0F, 0.0F}});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::EntitySnapshot,
      arv::protocol::serialize_entity_snapshot(fresh_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(4);
  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{10.0F, 11.0F, 12.0F}));
  EXPECT_EQ(mirror->last_server_tick, 20U);
  EXPECT_EQ(mirror->animation_name, "walking");
  EXPECT_FLOAT_EQ(mirror->animation_time_seconds, 0.5F);
  EXPECT_EQ(mirror->head_rotation_degrees, (arv::Vec3f{5.0F, 15.0F, 0.0F}));

  fresh_snapshot.server_tick = 19;
  fresh_snapshot.entities[0].position = {30.0F, 31.0F, 32.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::EntitySnapshot,
      arv::protocol::serialize_entity_snapshot(fresh_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(5);
  mirror = client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{10.0F, 11.0F, 12.0F}));

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::EntityDespawn,
      arv::protocol::serialize_entity_despawn(
          {arv::WorldID{1}, arv::EntityNetID{5}, 0}));
  client.fixed_tick(6);
  EXPECT_EQ(client.replicated_entity_count(), 0U);
}

TEST(ClientRuntimeTests, DroppedItemSnapshotsCreateUpdateAndRemoveMirrors) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::DroppedItemSnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 10;
  snapshot.drops.push_back({arv::EntityNetID{7},
                            arv::player::ItemStack{arv::player::ItemID{8}, 2},
                            {1.0F, 2.0F, 3.0F},
                            {}});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::DroppedItemSnapshot,
      arv::protocol::serialize_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(2);

  ASSERT_EQ(client.dropped_item_count(), 1U);
  const arv::client::DroppedItemMirror* mirror =
      client.find_dropped_item(arv::EntityNetID{7});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->stack, (arv::player::ItemStack{arv::player::ItemID{8}, 2}));
  EXPECT_EQ(mirror->position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));

  snapshot.server_tick = 11;
  snapshot.drops.clear();
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::DroppedItemSnapshot,
      arv::protocol::serialize_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(3);
  EXPECT_EQ(client.dropped_item_count(), 0U);
}

TEST(ClientRuntimeTests, InventoryAndBlockUpdatesRejectInactiveOrStaleState) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::BlockUpdateBatch wrong_world;
  wrong_world.world_id = arv::WorldID{2};
  wrong_world.server_tick = 2;
  wrong_world.updates.push_back(
      {arv::BlockPos{{0, 0, 0}}, arv::voxel::BlockVariantID{1}, 0, 2});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::BlockUpdateBatch,
      arv::protocol::serialize_block_update_batch(wrong_world));
  client.fixed_tick(2);
  EXPECT_TRUE(client.drain_received_block_updates().empty());

  wrong_world.world_id = arv::WorldID{1};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::BlockUpdateBatch,
      arv::protocol::serialize_block_update_batch(wrong_world));
  client.fixed_tick(3);
  EXPECT_EQ(client.drain_received_block_updates().size(), 1U);

  arv::protocol::InventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = 2;
  update.selected_hotbar_slot = 0;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 4}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::InventoryUpdate,
      arv::protocol::serialize_inventory_update(update));
  client.fixed_tick(4);
  ASSERT_TRUE(client.has_authoritative_inventory());
  ASSERT_TRUE(client.inventory().slot(0).has_value());
  EXPECT_EQ(client.inventory().slot(0)->count, 4U);
  EXPECT_EQ(client.inventory_revision(), 2U);

  update.revision = 1;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 9}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::InventoryUpdate,
      arv::protocol::serialize_inventory_update(update));
  client.fixed_tick(5);
  ASSERT_TRUE(client.inventory().slot(0).has_value());
  EXPECT_EQ(client.inventory().slot(0)->count, 4U);
  EXPECT_EQ(client.inventory_revision(), 2U);
}

TEST(ClientRuntimeTests, WorldLeaveClearsActiveWorldMirrors) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::EntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::EntitySpawn,
      arv::protocol::serialize_entity_spawn(spawn));

  arv::protocol::InventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = 1;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 1}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::InventoryUpdate,
      arv::protocol::serialize_inventory_update(update));
  client.fixed_tick(2);
  ASSERT_EQ(client.replicated_entity_count(), 1U);
  ASSERT_TRUE(client.has_authoritative_inventory());

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::WorldLeave,
      arv::protocol::serialize_world_leave({arv::WorldID{1}, 0}));
  client.fixed_tick(3);
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::WorldJoin);
  EXPECT_EQ(client.replicated_entity_count(), 0U);
  EXPECT_FALSE(client.has_authoritative_inventory());
  EXPECT_FALSE(client.local_player_id().has_value());
  EXPECT_FALSE(client.local_entity_id().has_value());
  EXPECT_TRUE(client.drain_received_block_updates().empty());
}
