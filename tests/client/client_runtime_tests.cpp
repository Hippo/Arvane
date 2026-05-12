#include "arv/client/client_runtime.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/entity_interaction.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/world_join.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

const arv::net::NetworkEvent* find_event(
    const std::vector<arv::net::NetworkEvent>& events,
    arv::net::NetworkEventType type) {
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type == type) {
      return &event;
    }
  }
  return nullptr;
}

arv::u32 player_move_count(const std::vector<arv::net::NetworkEvent>& events) {
  arv::u32 count = 0;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (decoded &&
        decoded.value().envelope.message_type ==
            arv::protocol::MessageType::ServerboundPlayerMove) {
      ++count;
    }
  }
  return count;
}

std::vector<arv::protocol::ServerboundKeepAlive> serverbound_keep_alives(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundKeepAlive> keep_alives;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundKeepAlive) {
      continue;
    }
    const auto keep_alive =
        arv::protocol::deserialize_serverbound_keep_alive(decoded.value().payload);
    if (keep_alive) {
      keep_alives.push_back(keep_alive.value());
    }
  }
  return keep_alives;
}

std::vector<arv::protocol::ServerboundChatMessage> serverbound_chat_messages(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundChatMessage> messages;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundChatMessage) {
      continue;
    }
    const auto chat =
        arv::protocol::deserialize_serverbound_chat_message(decoded.value().payload);
    if (chat) {
      messages.push_back(chat.value());
    }
  }
  return messages;
}

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

  arv::protocol::ClientboundHello hello;
  hello.accepted = true;
  hello.assigned_session_id = arv::SessionID{1};
  hello.assigned_player_id = arv::PlayerID{1};
  hello.initial_world_id = arv::WorldID{1};
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(hello));

  arv::protocol::ClientboundWorldJoinResult join;
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
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));

  client.fixed_tick(1);
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
}

class HangingConnectTransport final : public arv::net::ITransport {
public:
  arv::net::NetworkResult start_listening(const arv::net::ListenAddress&) override {
    return arv::net::NetworkResult::failure({"client test transport cannot listen"});
  }

  arv::net::ConnectResult connect(const arv::net::NetworkAddress&) override {
    state_ = arv::net::ConnectionState::Connecting;
    return arv::net::ConnectResult::success(arv::net::ConnectionID{7});
  }

  arv::net::NetworkResult send(arv::net::TransportMessage) override {
    return arv::net::NetworkResult::success();
  }

  arv::net::NetworkResult close_connection(
      arv::net::ConnectionID,
      std::string_view reason = {}) override {
    close_reason = std::string{reason};
    state_ = arv::net::ConnectionState::Disconnected;
    return arv::net::NetworkResult::success();
  }

  std::vector<arv::net::NetworkEvent> poll_events() override {
    return {};
  }

  arv::net::ConnectionState state(arv::net::ConnectionID) const override {
    return state_;
  }

  void shutdown() override {
    state_ = arv::net::ConnectionState::Disconnected;
  }

  std::string close_reason{};

private:
  arv::net::ConnectionState state_{arv::net::ConnectionState::Disconnected};
};

} // namespace

TEST(ClientRuntimeTests, ChatDraftIgnoresOpeningKeyThenAcceptsTextEditingAndSubmitWithoutLocalEcho) {
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
  EXPECT_TRUE(client.chat_messages().empty());
}

TEST(ClientRuntimeTests, SlashTextOpensChatWithCommandPrefix) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.text_input = "/"});

  EXPECT_TRUE(client.chat_open());
  EXPECT_EQ(client.chat_draft(), "/");
  EXPECT_TRUE(client.chat_messages().empty());
}

TEST(ClientRuntimeTests, ChatSubmitSendsRawServerboundIntentWhenInGame) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = " hello "});
  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});

  const std::vector<arv::protocol::ServerboundChatMessage> messages =
      serverbound_chat_messages(server_transport->poll_events());
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message, "hello");
  EXPECT_FALSE(messages.front().channel.has_value());
  EXPECT_TRUE(client.chat_messages().empty());
}

TEST(ClientRuntimeTests, ClientboundChatAppendsRichHistory) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::TextStylePatch style;
  style.color = arv::protocol::RgbColor{0x20U, 0x80U, 0xFFU};
  arv::protocol::ClientboundChatMessage chat;
  chat.message_id = 9U;
  chat.channel = "arvane:global";
  chat.kind = arv::protocol::ChatMessageKind::Chat;
  chat.message = arv::protocol::plain_rich_text("server says hi", style);
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundChatMessage,
      arv::protocol::serialize_clientbound_chat_message(chat));

  client.fixed_tick(2);

  ASSERT_EQ(client.chat_messages().size(), 1U);
  EXPECT_EQ(client.chat_messages().front(), "server says hi");
  ASSERT_EQ(client.chat_history().size(), 1U);
  EXPECT_EQ(client.chat_history().front().message_id, 9U);
  const std::vector<arv::protocol::RichTextRun> runs =
      arv::protocol::flatten_rich_text(client.chat_history().front().message);
  ASSERT_EQ(runs.size(), 1U);
  EXPECT_EQ(runs.front().style.color, (arv::protocol::RgbColor{0x20U, 0x80U, 0xFFU}));
}

TEST(ClientRuntimeTests, ChatHistoryLimitIsConfigurable) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  client.configure_chat({2U});

  client.add_chat_message("one");
  client.add_chat_message("two");
  client.add_chat_message("three");

  ASSERT_EQ(client.chat_history().size(), 2U);
  EXPECT_EQ(arv::protocol::to_plain_text(client.chat_history()[0].message), "two");
  EXPECT_EQ(arv::protocol::to_plain_text(client.chat_history()[1].message), "three");
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

TEST(ClientRuntimeTests, KickPacketDisconnectsAndStoresReason) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundKickPlayer,
      arv::protocol::serialize_clientbound_kick_player({"Duplicate profile."}),
      arv::protocol::ReliabilityClass::ReliableImmediate);

  client.fixed_tick(2);

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(client.disconnect_reason().has_value());
  EXPECT_EQ(client.disconnect_reason().value(), "Duplicate profile.");
  EXPECT_FALSE(client.local_player_id().has_value());
}

TEST(ClientRuntimeTests, KeepAlivePacketGetsImmediateEcho) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundKeepAlive,
      arv::protocol::serialize_clientbound_keep_alive({42U}),
      arv::protocol::ReliabilityClass::UnreliableEvent);

  client.fixed_tick(2);

  const std::vector<arv::protocol::ServerboundKeepAlive> replies =
      serverbound_keep_alives(server_transport->poll_events());
  ASSERT_EQ(replies.size(), 1U);
  EXPECT_EQ(replies.front().id, 42U);
  EXPECT_FALSE(client.disconnect_reason().has_value());
}

TEST(ClientRuntimeTests, ServerKeepAliveTimeoutDisconnectsClient) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  client.configure_networking({1U, 10U});
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  client.fixed_tick(2);

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(client.disconnect_reason().has_value());
  EXPECT_EQ(client.disconnect_reason().value(), "Lost connection to the server.");
}

TEST(ClientRuntimeTests, DisabledServerKeepAliveTimeoutDoesNotDisconnectClient) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  client.configure_networking({1U, 10U, false});
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  client.fixed_tick(2);

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_FALSE(client.disconnect_reason().has_value());
}

TEST(ClientRuntimeTests, ConnectionAttemptTimeoutDisconnectsClient) {
  auto transport = std::make_unique<HangingConnectTransport>();
  HangingConnectTransport* raw_transport = transport.get();
  arv::client::ClientRuntime client(std::move(transport));
  client.configure_networking({30U, 1U});
  const auto connection = client.connect({"127.0.0.1", 42069});
  ASSERT_TRUE(connection);
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Connecting);

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  client.fixed_tick(1);

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(client.disconnect_reason().has_value());
  EXPECT_EQ(client.disconnect_reason().value(), "Server is offline or unreachable.");
  EXPECT_EQ(raw_transport->close_reason, "Server is offline or unreachable.");
}

TEST(ClientRuntimeTests, ReplicatedEntitiesIgnoreUnknownAndStaleSnapshots) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundEntitySnapshot unknown_snapshot;
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
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(unknown_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(2);
  EXPECT_EQ(client.replicated_entity_count(), 0U);

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {0.0F, 1.0F, 2.0F};
  spawn.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));
  client.fixed_tick(3);
  ASSERT_EQ(client.replicated_entity_count(), 1U);

  arv::protocol::ClientboundEntitySnapshot fresh_snapshot;
  fresh_snapshot.world_id = arv::WorldID{1};
  fresh_snapshot.server_tick = 20;
  fresh_snapshot.entities.push_back({arv::EntityNetID{5},
                                     {10.0F, 11.0F, 12.0F},
                                     {1.0F, 0.0F, 0.0F},
                                     {0.0F, 0.0F, 0.0F, 1.0F},
                                     0,
                                     {{"walking", 0.5F}, {"hit_right", 0.125F}},
                                     {5.0F, 15.0F, 0.0F},
                                     8.0F,
                                     10.0F});
  fresh_snapshot.entities.back().state_flags =
      arv::protocol::EntityStateFlagHasHealth;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(fresh_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(4);
  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{10.0F, 11.0F, 12.0F}));
  EXPECT_EQ(mirror->last_server_tick, 20U);
  ASSERT_EQ(mirror->animations.size(), 2U);
  EXPECT_EQ(mirror->animations[0].name, "walking");
  EXPECT_FLOAT_EQ(mirror->animations[0].time_seconds, 0.5F);
  EXPECT_EQ(mirror->animations[1].name, "hit_right");
  EXPECT_FLOAT_EQ(mirror->animations[1].time_seconds, 0.125F);
  EXPECT_EQ(mirror->head_rotation_degrees, (arv::Vec3f{5.0F, 15.0F, 0.0F}));
  EXPECT_TRUE(mirror->has_health);
  EXPECT_FALSE(mirror->dead);
  EXPECT_FALSE(mirror->invulnerable);
  EXPECT_FLOAT_EQ(mirror->health_current, 8.0F);
  EXPECT_FLOAT_EQ(mirror->health_max, 10.0F);

  fresh_snapshot.server_tick = 19;
  fresh_snapshot.entities[0].position = {30.0F, 31.0F, 32.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(fresh_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(5);
  mirror = client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{10.0F, 11.0F, 12.0F}));

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntityDespawn,
      arv::protocol::serialize_clientbound_entity_despawn(
          {arv::WorldID{1}, arv::EntityNetID{5}, 0}));
  client.fixed_tick(6);
  EXPECT_EQ(client.replicated_entity_count(), 0U);
}

TEST(ClientRuntimeTests, AttackRequestSendsEntityPacketAndQueuesFeedback) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {3.0F, 1.0F, 2.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));
  client.fixed_tick(2);
  ASSERT_NE(client.find_replicated_entity(arv::EntityNetID{5}), nullptr);
  (void)server_transport->poll_events();

  const auto result = client.request_attack_entity(arv::EntityNetID{5});
  ASSERT_TRUE(result);

  const std::vector<arv::client::ClientAttackFeedback> feedback =
      client.drain_attack_feedback();
  ASSERT_EQ(feedback.size(), 1U);
  EXPECT_EQ(feedback.front().target_entity, (arv::EntityNetID{5}));
  EXPECT_EQ(feedback.front().target_position, (arv::Vec3f{3.0F, 1.0F, 2.0F}));
  EXPECT_EQ(feedback.front().sequence, 1U);

  const std::vector<arv::net::NetworkEvent> server_events =
      server_transport->poll_events();
  const arv::net::NetworkEvent* message =
      find_event(server_events, arv::net::NetworkEventType::MessageReceived);
  ASSERT_NE(message, nullptr);
  EXPECT_EQ(message->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(message->lane, arv::protocol::NetworkLane::Gameplay);

  const auto decoded = arv::protocol::deserialize_envelope(message->payload);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(
      decoded.value().envelope.message_type,
      arv::protocol::MessageType::ServerboundAttackEntity);
  const auto attack =
      arv::protocol::deserialize_serverbound_attack_entity(decoded.value().payload);
  ASSERT_TRUE(attack);
  EXPECT_EQ(attack.value().target_entity, (arv::EntityNetID{5}));
  EXPECT_EQ(attack.value().sequence, 1U);
}

TEST(ClientRuntimeTests, MovementHzPacketOverridesDefaultClientSendRate) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  client.fixed_tick(2);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 1U);
  client.fixed_tick(3);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 0U);

  const arv::protocol::ClientboundSetMovementHz settings{60.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundSetMovementHz,
      arv::protocol::serialize_clientbound_set_movement_hz(settings));

  client.fixed_tick(4);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 1U);
  client.fixed_tick(5);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 1U);
}

TEST(ClientRuntimeTests, ReplicatedEntityRenderPositionSmoothsBetweenTargets) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {0.0F, 0.0F, 0.0F};
  spawn.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));
  client.fixed_tick(2);

  arv::protocol::ClientboundEntitySnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 10;
  snapshot.entities.push_back({arv::EntityNetID{5},
                               {0.0F, 0.0F, 0.0F},
                               {},
                               {0.0F, 0.0F, 0.0F, 1.0F},
                               0});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(3);

  snapshot.server_tick = 13;
  snapshot.entities[0].position = {3.0F, 0.0F, 0.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(4);

  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{3.0F, 0.0F, 0.0F}));
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{0.0F, 0.0F, 0.0F}));

  client.advance_entity_interpolation(1.0F / 60.0F);
  mirror = client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_NEAR(mirror->render_position.x, 1.0F, 0.001F);

  client.advance_entity_interpolation(2.0F / 60.0F);
  mirror = client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{3.0F, 0.0F, 0.0F}));
}

TEST(ClientRuntimeTests, DroppedItemSnapshotsCreateUpdateAndRemoveMirrors) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundDroppedItemSnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 10;
  snapshot.drops.push_back({arv::EntityNetID{7},
                            arv::player::ItemStack{arv::player::ItemID{8}, 2},
                            {1.0F, 2.0F, 3.0F},
                            {}});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(snapshot),
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
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(3);
  EXPECT_EQ(client.dropped_item_count(), 0U);
}

TEST(ClientRuntimeTests, DroppedItemRenderPositionSmoothsBetweenTargets) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundDroppedItemSnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 10;
  snapshot.drops.push_back({arv::EntityNetID{7},
                            arv::player::ItemStack{arv::player::ItemID{8}, 2},
                            {0.0F, 0.0F, 0.0F},
                            {}});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(2);

  snapshot.server_tick = 13;
  snapshot.drops[0].position = {0.0F, 0.0F, 3.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(3);

  const arv::client::DroppedItemMirror* mirror =
      client.find_dropped_item(arv::EntityNetID{7});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{0.0F, 0.0F, 3.0F}));
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{0.0F, 0.0F, 0.0F}));

  client.advance_entity_interpolation(1.0F / 60.0F);
  mirror = client.find_dropped_item(arv::EntityNetID{7});
  ASSERT_NE(mirror, nullptr);
  EXPECT_NEAR(mirror->render_position.z, 1.0F, 0.001F);

  client.advance_entity_interpolation(2.0F / 60.0F);
  mirror = client.find_dropped_item(arv::EntityNetID{7});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{0.0F, 0.0F, 3.0F}));
}

TEST(ClientRuntimeTests, InventoryAndBlockUpdatesRejectInactiveOrStaleState) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundBlockUpdateBatch wrong_world;
  wrong_world.world_id = arv::WorldID{2};
  wrong_world.server_tick = 2;
  wrong_world.updates.push_back(
      {arv::BlockPos{{0, 0, 0}}, arv::voxel::BlockVariantID{1}, 0, 2});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundBlockUpdateBatch,
      arv::protocol::serialize_clientbound_block_update_batch(wrong_world));
  client.fixed_tick(2);
  EXPECT_TRUE(client.drain_received_block_updates().empty());

  wrong_world.world_id = arv::WorldID{1};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundBlockUpdateBatch,
      arv::protocol::serialize_clientbound_block_update_batch(wrong_world));
  client.fixed_tick(3);
  EXPECT_EQ(client.drain_received_block_updates().size(), 1U);

  arv::protocol::ClientboundInventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = 2;
  update.selected_hotbar_slot = 0;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 4}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(update));
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
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(update));
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

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));

  arv::protocol::ClientboundInventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = 1;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 1}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(update));
  client.fixed_tick(2);
  ASSERT_EQ(client.replicated_entity_count(), 1U);
  ASSERT_TRUE(client.has_authoritative_inventory());

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundWorldLeave,
      arv::protocol::serialize_clientbound_world_leave({arv::WorldID{1}, 0}));
  client.fixed_tick(3);
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::WorldJoin);
  EXPECT_EQ(client.replicated_entity_count(), 0U);
  EXPECT_FALSE(client.has_authoritative_inventory());
  EXPECT_FALSE(client.local_player_id().has_value());
  EXPECT_FALSE(client.local_entity_id().has_value());
  EXPECT_TRUE(client.drain_received_block_updates().empty());
}
