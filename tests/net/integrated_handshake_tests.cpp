#include "arv/client/client_runtime.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/message_type.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/world_join.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/world/world_generation.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
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

std::vector<arv::u8> raw_client_hello_packet(
    const arv::client::LocalPlayerIdentity& identity) {
  arv::protocol::ServerboundHello hello;
  hello.requested_username_or_profile_name = identity.display_name;
  hello.auth_begin = arv::protocol::ServerboundAuthBegin{
      identity.key_pair.public_key,
      identity.display_name,
      identity.profile_id};
  const std::vector<arv::u8> payload = arv::protocol::serialize_serverbound_hello(hello);
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       arv::protocol::MessageType::ServerboundHello,
       0,
       0,
       0,
       0},
      payload);
  return packet;
}

void send_raw_client_hello(
    arv::net::ITransport& client_transport,
    arv::net::ConnectionID connection_id,
    const arv::client::LocalPlayerIdentity& identity) {
  std::vector<arv::u8> packet = raw_client_hello_packet(identity);
  (void)client_transport.send(
      {connection_id, arv::protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
}

void send_raw_keep_alive(
    arv::net::ITransport& client_transport,
    arv::net::ConnectionID connection_id,
    arv::u64 id) {
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       arv::protocol::MessageType::ServerboundKeepAlive,
       0,
       0,
       0,
       0},
      arv::protocol::serialize_serverbound_keep_alive({id}));
  (void)client_transport.send(
      {connection_id,
       arv::protocol::ReliabilityClass::UnreliableEvent,
       std::move(packet),
       arv::protocol::MessageType::ServerboundKeepAlive,
       arv::protocol::NetworkLane::Session});
}

std::vector<std::string> kick_reasons(std::span<const arv::net::NetworkEvent> events) {
  std::vector<std::string> reasons;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ClientboundKickPlayer) {
      continue;
    }
    const auto kick =
        arv::protocol::deserialize_clientbound_kick_player(decoded.value().payload);
    if (kick) {
      reasons.push_back(kick.value().reason);
    }
  }
  return reasons;
}

std::vector<arv::u64> keep_alive_ids(std::span<const arv::net::NetworkEvent> events) {
  std::vector<arv::u64> ids;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ClientboundKeepAlive) {
      continue;
    }
    const auto keep_alive =
        arv::protocol::deserialize_clientbound_keep_alive(decoded.value().payload);
    if (keep_alive) {
      ids.push_back(keep_alive.value().id);
    }
  }
  return ids;
}

std::vector<std::pair<arv::net::ConnectionID, std::string>> sent_kick_reasons(
    std::span<const arv::net::TransportMessage> messages) {
  std::vector<std::pair<arv::net::ConnectionID, std::string>> reasons;
  for (const arv::net::TransportMessage& message : messages) {
    if (message.message_type != arv::protocol::MessageType::ClientboundKickPlayer) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(message.payload);
    if (!decoded) {
      continue;
    }
    const auto kick =
        arv::protocol::deserialize_clientbound_kick_player(decoded.value().payload);
    if (kick) {
      reasons.push_back({message.connection_id, kick.value().reason});
    }
  }
  return reasons;
}

class ScriptedServerTransport final : public arv::net::ITransport {
public:
  arv::net::NetworkResult start_listening(const arv::net::ListenAddress&) override {
    listening = true;
    return arv::net::NetworkResult::success();
  }

  arv::net::ConnectResult connect(const arv::net::NetworkAddress&) override {
    return arv::net::ConnectResult::failure({"server test transport cannot connect"});
  }

  arv::net::NetworkResult send(arv::net::TransportMessage message) override {
    sent.push_back(std::move(message));
    return arv::net::NetworkResult::success();
  }

  arv::net::NetworkResult close_connection(
      arv::net::ConnectionID connection_id,
      std::string_view reason = {}) override {
    states[connection_id] = arv::net::ConnectionState::Disconnected;
    closed.push_back({connection_id, std::string{reason}});
    return arv::net::NetworkResult::success();
  }

  std::vector<arv::net::NetworkEvent> poll_events() override {
    std::vector<arv::net::NetworkEvent> output;
    while (!events.empty()) {
      output.push_back(std::move(events.front()));
      events.pop_front();
    }
    return output;
  }

  arv::net::ConnectionState state(arv::net::ConnectionID connection_id) const override {
    const auto found = states.find(connection_id);
    return found == states.end() ? arv::net::ConnectionState::TransportConnected
                                 : found->second;
  }

  void shutdown() override {
    listening = false;
    for (auto& [connection_id, state] : states) {
      (void)connection_id;
      state = arv::net::ConnectionState::Disconnected;
    }
  }

  void push_message(arv::net::ConnectionID connection_id, std::vector<arv::u8> payload) {
    states[connection_id] = arv::net::ConnectionState::TransportConnected;
    events.push_back({arv::net::NetworkEventType::MessageReceived,
                      connection_id,
                      arv::protocol::ReliabilityClass::ReliableOrdered,
                      std::move(payload),
                      {},
                      std::chrono::steady_clock::now(),
                      arv::protocol::MessageType::ServerboundHello,
                      arv::protocol::NetworkLane::Session});
  }

  bool listening{};
  std::deque<arv::net::NetworkEvent> events{};
  std::vector<arv::net::TransportMessage> sent{};
  std::vector<std::pair<arv::net::ConnectionID, std::string>> closed{};
  arv::HashMap<arv::net::ConnectionID, arv::net::ConnectionState> states{};
};

arv::u32 player_correction_count(std::span<const arv::net::NetworkEvent> events) {
  arv::u32 count = 0;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (decoded &&
        decoded.value().envelope.message_type == arv::protocol::MessageType::ClientboundPlayerMoveCorrection) {
      ++count;
    }
  }
  return count;
}

std::vector<arv::protocol::ClientboundSetMovementHz> movement_hz_packets(
    std::span<const arv::net::NetworkEvent> events) {
  std::vector<arv::protocol::ClientboundSetMovementHz> packets;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ClientboundSetMovementHz) {
      continue;
    }
    const auto settings =
        arv::protocol::deserialize_clientbound_set_movement_hz(decoded.value().payload);
    if (settings) {
      packets.push_back(settings.value());
    }
  }
  return packets;
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

std::optional<arv::u16> start_gns_server_runtime(
    arv::server::ServerRuntime& server,
    arv::server::ServerStartInfo start_info) {
  const auto seed = static_cast<arv::u16>(
      std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
  for (arv::u16 attempt = 0; attempt < 100; ++attempt) {
    const arv::u16 port = static_cast<arv::u16>(45000U + ((seed + attempt) % 1000U));
    start_info.listen_address = {"0.0.0.0", port};
    if (server.start(start_info)) {
      return port;
    }
  }
  return std::nullopt;
}

} // namespace

TEST(IntegratedHandshakeTests, ClientAndServerReachInGameThroughProtocolHandshake) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
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

TEST(IntegratedHandshakeTests, ClientChatIsFormattedAndBroadcastByServer) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_local_identity(arv::client::make_ephemeral_local_identity("Hippo"));
  ASSERT_TRUE(client.connect_integrated());
  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);

  client.submit_input({.open_chat_pressed = true});
  client.submit_input({.text_input = "{\"text\":\"fake admin\"}"});
  client.submit_input({.text_submit_pressed = true});
  server.tick();
  client.fixed_tick(server.tick_index());

  ASSERT_EQ(client.chat_history().size(), 1U);
  EXPECT_EQ(client.chat_history().front().kind, arv::protocol::ChatMessageKind::Chat);
  EXPECT_EQ(client.chat_history().front().channel, "arvane:global");
  EXPECT_EQ(
      arv::protocol::to_plain_text(client.chat_history().front().message),
      "Hippo: {\"text\":\"fake admin\"}");
  const std::vector<arv::protocol::RichTextRun> runs =
      arv::protocol::flatten_rich_text(client.chat_history().front().message);
  ASSERT_GE(runs.size(), 3U);
  EXPECT_TRUE(runs.front().click_event.has_value());
}

TEST(IntegratedHandshakeTests, DuplicateProfileRejectsNewConnectionOnly) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto scripted_transport = std::make_unique<ScriptedServerTransport>();
  ScriptedServerTransport* transport = scripted_transport.get();
  arv::server::ServerRuntime server(std::move(scripted_transport));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity();
  transport->push_message(arv::net::ConnectionID{1}, raw_client_hello_packet(identity));
  transport->push_message(arv::net::ConnectionID{2}, raw_client_hello_packet(identity));

  server.tick();

  EXPECT_EQ(server.sessions().size(), 1U);
  const std::vector<std::pair<arv::net::ConnectionID, std::string>> kicks =
      sent_kick_reasons(transport->sent);
  ASSERT_EQ(kicks.size(), 1U);
  EXPECT_EQ(kicks.front().first, (arv::net::ConnectionID{2}));
  EXPECT_EQ(
      kicks.front().second,
      "This Arvane profile is already connected to this server.");
  ASSERT_EQ(transport->closed.size(), 1U);
  EXPECT_EQ(transport->closed.front().first, (arv::net::ConnectionID{2}));
}

TEST(IntegratedHandshakeTests, ServerShutdownSendsKickBeforeClosingSession) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  (void)client_transport->poll_events();

  server.shutdown();

  const std::vector<std::string> kicks = kick_reasons(client_transport->poll_events());
  ASSERT_EQ(kicks.size(), 1U);
  EXPECT_EQ(kicks.front(), "Server is shutting down.");
}

TEST(IntegratedHandshakeTests, ServerKeepAliveTimeoutKicksClient) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.keep_alive.interval_seconds = 1U;
  start_info.keep_alive.client_timeout_seconds = 1U;
  ASSERT_TRUE(server.start(start_info));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  const std::vector<arv::u64> ids = keep_alive_ids(client_transport->poll_events());
  ASSERT_FALSE(ids.empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  server.tick();

  const std::vector<std::string> kicks = kick_reasons(client_transport->poll_events());
  ASSERT_EQ(kicks.size(), 1U);
  EXPECT_EQ(kicks.front(), "Timed out.");
  EXPECT_EQ(server.sessions().size(), 0U);
}

TEST(IntegratedHandshakeTests, DisabledServerKeepAliveDoesNotKickClient) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.keep_alive.interval_seconds = 1U;
  start_info.keep_alive.client_timeout_seconds = 1U;
  start_info.keep_alive_enabled = false;
  ASSERT_TRUE(server.start(start_info));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  EXPECT_TRUE(keep_alive_ids(client_transport->poll_events()).empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  server.tick();

  EXPECT_EQ(server.sessions().size(), 1U);
  EXPECT_TRUE(kick_reasons(client_transport->poll_events()).empty());
}

TEST(IntegratedHandshakeTests, KeepAliveReplyPreventsServerTimeout) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.keep_alive.interval_seconds = 1U;
  start_info.keep_alive.client_timeout_seconds = 1U;
  ASSERT_TRUE(server.start(start_info));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  const std::vector<arv::u64> ids = keep_alive_ids(client_transport->poll_events());
  ASSERT_FALSE(ids.empty());
  send_raw_keep_alive(*client_transport, connect_result.value(), ids.back());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  server.tick();

  EXPECT_EQ(server.sessions().size(), 1U);
  EXPECT_TRUE(kick_reasons(client_transport->poll_events()).empty());
}

TEST(IntegratedHandshakeTests, ServerSendsMovementHzOverrideWhenConfigured) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.movement.send_rate_hz = 30.0F;
  ASSERT_TRUE(server.start(start_info));

  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity();
  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(*client_transport, connect_result.value(), identity);
  server.tick();

  const std::vector<arv::protocol::ClientboundSetMovementHz> packets =
      movement_hz_packets(client_transport->poll_events());
  ASSERT_EQ(packets.size(), 1U);
  EXPECT_FLOAT_EQ(packets.front().movement_hz, 30.0F);
}

TEST(IntegratedHandshakeTests, SavedPlayerStateIsAppliedBeforeWorldJoinResult) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));

  arv::server::ServerPlayerSaveState saved_player;
  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity();
  saved_player.profile_id = identity.profile_id;
  saved_player.profile_name = "local_player";
  saved_player.world_name = "default";
  saved_player.position = {12.0F, 34.0F, -56.0F};
  saved_player.velocity = {1.0F, 2.0F, 3.0F};
  saved_player.selected_hotbar_slot = 3;
  std::array<arv::server::ServerPlayerSaveState, 1> saved_players{saved_player};

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.initial_tick_index = 77;
  start_info.world_name = "default";
  start_info.saved_players = saved_players;
  ASSERT_TRUE(server.start(start_info));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(*client_transport, connect_result.value(), identity);
  server.tick();

  const std::vector<arv::net::NetworkEvent> events = client_transport->poll_events();
  std::optional<arv::protocol::ClientboundWorldJoinResult> world_join;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    auto envelope = arv::protocol::deserialize_envelope(event.payload);
    if (!envelope ||
        envelope.value().envelope.message_type != arv::protocol::MessageType::ClientboundWorldJoinResult) {
      continue;
    }
    auto decoded = arv::protocol::deserialize_clientbound_world_join_result(envelope.value().payload);
    ASSERT_TRUE(decoded);
    world_join = decoded.value();
  }

  ASSERT_TRUE(world_join.has_value());
  EXPECT_TRUE(world_join->accepted);
  EXPECT_EQ(world_join->initial_tick, 77U);
  EXPECT_EQ(world_join->spawn_position.x, 12.0F);
  EXPECT_EQ(world_join->spawn_position.y, 34.0F);
  EXPECT_EQ(world_join->spawn_position.z, -56.0F);
  const arv::player::PlayerState& player = connected_player(server);
  EXPECT_EQ(player.selected_hotbar_slot, 3U);
}

TEST(IntegratedHandshakeTests, InventoryActionsFlowThroughAuthoritativeProtocolUpdates) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
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
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry,
                            arv::player::default_player_tuning(),
                            arv::voxel::make_water_fluid_definition(),
                            streaming_config}));
  server.voxel_world().configure_height({1, 1});

  const arv::ChunkSectionCoord expected_chunk{{0, 1, -2}};
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
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.drain_received_chunk_section_data().size(), 1U);

  ASSERT_TRUE(server.voxel_world().set_block(changed_block, dirt));
  server.tick();
  client.fixed_tick(server.tick_index());
  std::vector<arv::protocol::ClientboundBlockUpdateBatch> updates =
      client.drain_received_block_updates();
  ASSERT_EQ(updates.size(), 1U);
  ASSERT_EQ(updates.front().updates.size(), 1U);
  EXPECT_EQ(updates.front().world_id, arv::server::DefaultWorldID);
  EXPECT_EQ(updates.front().updates.front().pos, changed_block);
  EXPECT_EQ(updates.front().updates.front().new_state, dirt);
}

TEST(IntegratedHandshakeTests, DroppedItemsReplicateAfterRelevantChunkStreaming) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item = item_registry.find_item("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry,
                            arv::player::default_player_tuning(),
                            arv::voxel::make_water_fluid_definition(),
                            streaming_config}));
  server.voxel_world().configure_height({1, 1});

  const arv::ChunkSectionCoord spawn_chunk{{0, 1, -2}};
  (void)server.voxel_world().ensure_chunk(spawn_chunk);

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  for (int tick = 0; tick < 4 && client.dropped_item_count() == 0U; ++tick) {
    server.tick();
    client.fixed_tick(server.tick_index());
    (void)client.drain_received_chunk_section_data();
  }

  arv::player::PlayerState& player = connected_player(server);
  player.inventory_cursor_stack = arv::player::ItemStack{stone_item, 2};
  ASSERT_TRUE(server.drop_player_inventory_cursor(player.player_id).has_value());

  server.tick();
  client.fixed_tick(server.tick_index());

  ASSERT_EQ(client.dropped_item_count(), 1U);
  const std::vector<arv::client::DroppedItemMirror> drops = client.dropped_item_mirrors();
  ASSERT_EQ(drops.size(), 1U);
  EXPECT_EQ(drops.front().stack, (arv::player::ItemStack{stone_item, 2}));

  const std::vector<arv::entity::DroppedItemEntitySnapshot> server_drops =
      server.voxel_world().collect_dropped_item_snapshots();
  ASSERT_EQ(server_drops.size(), 1U);
  ASSERT_TRUE(server.voxel_world().destroy_entity(server_drops.front().entity_id));
  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(server.dropped_item_stacks().empty());
  EXPECT_EQ(client.dropped_item_count(), 0U);
}

TEST(IntegratedHandshakeTests, DisconnectCleansSessionAndWorldLocalPlayerEntity) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());

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

TEST(IntegratedHandshakeTests, ClientMovementPacketUpdatesServerHotbarSlot) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);

  client.submit_input(arv::client::InputFrame{.hotbar_select = 4});
  client.fixed_tick(server.tick_index());

  server.tick();
  EXPECT_EQ(connected_player(server).selected_hotbar_slot, 4U);
}

TEST(IntegratedHandshakeTests, RejectedPlayerMoveSendsReliableCorrection) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  (void)client_transport->poll_events();

  arv::protocol::ServerboundPlayerMove move;
  move.sequence = 1;
  move.client_tick = 1;
  move.position = {1000.0F, 20.0F, 1000.0F};
  move.flags = arv::protocol::PlayerMoveFlags::OnGround;
  const std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       arv::protocol::MessageType::ServerboundPlayerMove,
       0,
       0,
       0,
       0},
      arv::protocol::serialize_serverbound_player_move(move));
  ASSERT_TRUE(client_transport->send(
      {connect_result.value(),
       arv::protocol::ReliabilityClass::UnreliableLatestWins,
       packet,
       arv::protocol::MessageType::ServerboundPlayerMove,
       arv::protocol::NetworkLane::Movement}));

  server.tick();
  const std::vector<arv::net::NetworkEvent> events = client_transport->poll_events();
  EXPECT_EQ(player_correction_count(events), 1U);
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
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry,
                            arv::player::default_player_tuning(),
                            arv::voxel::make_water_fluid_definition(),
                            streaming_config}));
  server.voxel_world().configure_height({1, 1});

  const arv::ChunkSectionCoord expected_chunk{{0, 1, -2}};
  (void)server.voxel_world().ensure_chunk(expected_chunk);
  ASSERT_TRUE(server.voxel_world().set_block(
      arv::chunk_local_to_block(expected_chunk, {{1, 2, 3}}),
      stone));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  std::vector<arv::protocol::ClientboundChunkSectionData> chunks =
      client.drain_received_chunk_section_data();
  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
  EXPECT_EQ(chunks.front().coord, expected_chunk);
  EXPECT_EQ(
      chunks.front().blocks[arv::voxel::local_index({{1, 2, 3}})],
      stone);

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());
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
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
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
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  std::vector<arv::protocol::ClientboundChunkSectionData> chunks;
  for (int attempt = 0; attempt < 100 && chunks.empty(); ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    chunks = client.drain_received_chunk_section_data();
    if (chunks.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
}

TEST(IntegratedHandshakeTests, RemoteConnectPathReceivesGeneratedChunks) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Dedicated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();
  start_info.chunk_streaming = streaming_config;
  start_info.chunk_generation = make_generation_settings(block_registry);
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect({"localhost", 42069}));

  std::vector<arv::protocol::ClientboundChunkSectionData> chunks;
  for (int attempt = 0; attempt < 100 && chunks.empty(); ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    chunks = client.drain_received_chunk_section_data();
    if (chunks.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
}

TEST(IntegratedHandshakeTests, GameNetworkingSocketsLoopbackReceivesGeneratedChunks) {
  arv::net::GameNetworkingSocketsEnvironment environment;
  if (!environment.initialized()) {
    GTEST_SKIP() << environment.error_message();
  }

  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto server_transport = std::make_unique<arv::net::GameNetworkingSocketsTransport>();
  arv::net::GameNetworkingSocketsTransport* server_transport_view = server_transport.get();
  arv::server::ServerRuntime server(std::move(server_transport));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Dedicated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();
  start_info.chunk_streaming = streaming_config;
  start_info.chunk_generation = make_generation_settings(block_registry);
  const std::optional<arv::u16> port = start_gns_server_runtime(server, start_info);
  if (!port.has_value()) {
    GTEST_SKIP() << "failed to create GameNetworkingSockets listen socket";
  }
  ASSERT_EQ(server_transport_view->bound_port(), port.value());

  arv::client::ClientRuntime client(
      std::make_unique<arv::net::GameNetworkingSocketsTransport>());
  client.set_chunk_view_distance(0);
  const auto connect_result = client.connect({"localhost", port.value()});
  if (!connect_result) {
    GTEST_SKIP() << connect_result.error().message;
  }

  std::vector<arv::protocol::ClientboundChunkSectionData> chunks;
  for (int attempt = 0; attempt < 300 && chunks.empty(); ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    chunks = client.drain_received_chunk_section_data();
    if (chunks.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(server.sessions().size(), 1U);
  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
  EXPECT_GT(server.voxel_world().loaded_chunk_count(), 0U);

  arv::player::PlayerState& player = connected_player(server);
  const arv::player::ItemID stone_item = item_registry.find_item("arvane:stone").value();
  player.inventory_cursor_stack = arv::player::ItemStack{stone_item, 1};
  ASSERT_TRUE(server.drop_player_inventory_cursor(player.player_id).has_value());

  for (int attempt = 0; attempt < 300 && client.dropped_item_count() == 0U; ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    if (client.dropped_item_count() == 0U) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ASSERT_EQ(client.dropped_item_count(), 1U);
  EXPECT_EQ(client.dropped_item_mirrors().front().stack.item, stone_item);
}

TEST(IntegratedHandshakeTests, GameNetworkingSocketsLoopbackStreamsViewDistanceOne) {
  arv::net::GameNetworkingSocketsEnvironment environment;
  if (!environment.initialized()) {
    GTEST_SKIP() << environment.error_message();
  }

  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto server_transport = std::make_unique<arv::net::GameNetworkingSocketsTransport>();
  arv::server::ServerRuntime server(std::move(server_transport));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 1;
  streaming_config.max_chunks_sent_per_player_tick = 2;

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Dedicated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();
  start_info.chunk_streaming = streaming_config;
  start_info.chunk_generation = make_generation_settings(block_registry);
  start_info.max_generation_results_per_tick = 2;
  start_info.max_pending_generation_jobs = 4;
  const std::optional<arv::u16> port = start_gns_server_runtime(server, start_info);
  if (!port.has_value()) {
    GTEST_SKIP() << "failed to create GameNetworkingSockets listen socket";
  }

  arv::client::ClientRuntime client(
      std::make_unique<arv::net::GameNetworkingSocketsTransport>());
  client.set_chunk_view_distance(1);
  const auto connect_result = client.connect({"localhost", port.value()});
  if (!connect_result) {
    GTEST_SKIP() << connect_result.error().message;
  }

  arv::HashSet<arv::ChunkSectionCoord> received_chunks;
  for (int attempt = 0; attempt < 1500 && received_chunks.size() < 27U; ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    for (const arv::protocol::ClientboundChunkSectionData& chunk :
         client.drain_received_chunk_section_data()) {
      if (chunk.world_id == arv::server::DefaultWorldID) {
        received_chunks.insert(chunk.coord);
      }
    }
    if (received_chunks.size() < 27U) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(server.sessions().size(), 1U);
  EXPECT_EQ(received_chunks.size(), 27U);
  EXPECT_GE(server.voxel_world().loaded_chunk_count(), 27U);
}
