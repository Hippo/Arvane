#include "arv/net/gns_transport.hpp"
#include "arv/net/in_process_transport.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <utility>
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

void connect_and_drain_accept(arv::net::ITransport& client, arv::net::ITransport& server) {
  ASSERT_TRUE(server.start_listening({}));
  const auto connect_result = client.connect({"in-process", 0});
  ASSERT_TRUE(connect_result);

  const std::vector<arv::net::NetworkEvent> server_events = server.poll_events();
  ASSERT_NE(find_event(server_events, arv::net::NetworkEventType::ConnectionAccepted), nullptr);

  const std::vector<arv::net::NetworkEvent> client_events = client.poll_events();
  ASSERT_NE(find_event(client_events, arv::net::NetworkEventType::ConnectionAccepted), nullptr);
}

std::optional<arv::u16> start_gns_test_listener(
    arv::net::GameNetworkingSocketsTransport& server) {
  const auto seed = static_cast<arv::u16>(
      std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
  for (arv::u16 attempt = 0; attempt < 100; ++attempt) {
    const arv::u16 port = static_cast<arv::u16>(44000U + ((seed + attempt) % 1000U));
    if (server.start_listening({"0.0.0.0", port})) {
      return port;
    }
  }
  return std::nullopt;
}

} // namespace

TEST(InProcessTransportTests, ClientAndServerReachTransportConnected) {
  auto transports = arv::net::make_in_process_transport_pair();
  ASSERT_TRUE(transports.server->start_listening({}));

  const auto connect_result = transports.client->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  EXPECT_EQ(transports.client->state(connect_result.value()), arv::net::ConnectionState::TransportConnected);
  EXPECT_EQ(transports.server->state(connect_result.value()), arv::net::ConnectionState::TransportConnected);

  const std::vector<arv::net::NetworkEvent> server_events = transports.server->poll_events();
  const arv::net::NetworkEvent* server_accepted =
      find_event(server_events, arv::net::NetworkEventType::ConnectionAccepted);
  ASSERT_NE(server_accepted, nullptr);
  EXPECT_EQ(server_accepted->connection_id, connect_result.value());

  const std::vector<arv::net::NetworkEvent> client_events = transports.client->poll_events();
  const arv::net::NetworkEvent* client_accepted =
      find_event(client_events, arv::net::NetworkEventType::ConnectionAccepted);
  ASSERT_NE(client_accepted, nullptr);
  EXPECT_EQ(client_accepted->connection_id, connect_result.value());
}

TEST(InProcessTransportTests, DeliversMessagesBothDirections) {
  auto transports = arv::net::make_in_process_transport_pair();
  connect_and_drain_accept(*transports.client, *transports.server);

  ASSERT_TRUE(transports.client->send({arv::net::ConnectionID{1},
                                       arv::protocol::ReliabilityClass::UnreliableEvent,
                                       std::vector<arv::u8>{1, 2, 3}}));

  std::vector<arv::net::NetworkEvent> server_events = transports.server->poll_events();
  const arv::net::NetworkEvent* client_message =
      find_event(server_events, arv::net::NetworkEventType::MessageReceived);
  ASSERT_NE(client_message, nullptr);
  EXPECT_EQ(client_message->connection_id, arv::net::ConnectionID{1});
  EXPECT_EQ(client_message->reliability, arv::protocol::ReliabilityClass::UnreliableEvent);
  EXPECT_EQ(client_message->payload, (std::vector<arv::u8>{1, 2, 3}));

  ASSERT_TRUE(transports.server->send({arv::net::ConnectionID{1},
                                       arv::protocol::ReliabilityClass::ReliableOrdered,
                                       std::vector<arv::u8>{4, 5}}));

  std::vector<arv::net::NetworkEvent> client_events = transports.client->poll_events();
  const arv::net::NetworkEvent* server_message =
      find_event(client_events, arv::net::NetworkEventType::MessageReceived);
  ASSERT_NE(server_message, nullptr);
  EXPECT_EQ(server_message->connection_id, arv::net::ConnectionID{1});
  EXPECT_EQ(server_message->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(server_message->payload, (std::vector<arv::u8>{4, 5}));
}

TEST(InProcessTransportTests, ShutdownDeliversConnectionClosedToPeer) {
  auto transports = arv::net::make_in_process_transport_pair();
  connect_and_drain_accept(*transports.client, *transports.server);

  transports.client->shutdown();

  EXPECT_EQ(transports.client->state(arv::net::ConnectionID{1}), arv::net::ConnectionState::Disconnected);
  EXPECT_EQ(transports.server->state(arv::net::ConnectionID{1}), arv::net::ConnectionState::Disconnected);

  const std::vector<arv::net::NetworkEvent> server_events = transports.server->poll_events();
  const arv::net::NetworkEvent* closed =
      find_event(server_events, arv::net::NetworkEventType::ConnectionClosed);
  ASSERT_NE(closed, nullptr);
  EXPECT_EQ(closed->connection_id, arv::net::ConnectionID{1});
}

TEST(InProcessTransportTests, SendFailsWhenPeerQueueIsFull) {
  auto transports = arv::net::make_in_process_transport_pair(1U);
  connect_and_drain_accept(*transports.client, *transports.server);

  bool saw_failure = false;
  std::string failure_message;
  for (arv::usize index = 0; index < 4096U; ++index) {
    std::vector<arv::u8> payload{static_cast<arv::u8>(index & 0xFFU)};
    auto result = transports.client->send({arv::net::ConnectionID{1},
                                           arv::protocol::ReliabilityClass::ReliableOrdered,
                                           std::move(payload)});
    if (!result) {
      saw_failure = true;
      failure_message = result.error().message;
      break;
    }
  }

  EXPECT_TRUE(saw_failure);
  EXPECT_NE(failure_message.find("full"), std::string::npos);
}

TEST(GameNetworkingSocketsTransportTests, ReliabilityFlagsUseNormalUnreliableByDefault) {
  EXPECT_EQ(
      arv::net::detail::gns_send_flags(arv::protocol::ReliabilityClass::ReliableOrdered),
      k_nSteamNetworkingSend_Reliable);
  EXPECT_EQ(
      arv::net::detail::gns_send_flags(arv::protocol::ReliabilityClass::UnreliableEvent),
      k_nSteamNetworkingSend_UnreliableNoDelay);
  EXPECT_EQ(
      arv::net::detail::gns_send_flags(arv::protocol::ReliabilityClass::UnreliableLatestWins),
      k_nSteamNetworkingSend_UnreliableNoDelay);
  EXPECT_EQ(
      arv::net::detail::gns_lane_index(arv::protocol::NetworkLane::Session),
      0U);
  EXPECT_EQ(
      arv::net::detail::gns_lane_index(arv::protocol::NetworkLane::Movement),
      2U);
  EXPECT_EQ(
      arv::net::detail::gns_lane_index(arv::protocol::NetworkLane::ChunkStreaming),
      3U);
}

TEST(GameNetworkingSocketsTransportTests, LoopbackDeliversMessagesBetweenTwoTransports) {
  arv::net::GameNetworkingSocketsEnvironment environment;
  if (!environment.initialized()) {
    GTEST_SKIP() << environment.error_message();
  }

  arv::net::GameNetworkingSocketsTransport server;
  arv::net::GameNetworkingSocketsTransport client;
  const std::optional<arv::u16> port = start_gns_test_listener(server);
  if (!port.has_value()) {
    GTEST_SKIP() << "failed to create GameNetworkingSockets listen socket";
  }

  const auto connect_result = client.connect({"localhost", port.value()});
  if (!connect_result) {
    GTEST_SKIP() << connect_result.error().message;
  }

  bool server_accepted = false;
  bool client_accepted = false;
  bool sent_client_payload = false;
  bool sent_server_payload = false;
  bool server_received_payload = false;
  bool client_received_payload = false;
  arv::net::ConnectionID server_connection{};

  for (int attempt = 0; attempt < 200 &&
                        (!server_received_payload || !client_received_payload);
       ++attempt) {
    for (const arv::net::NetworkEvent& event : server.poll_events()) {
      if (event.type == arv::net::NetworkEventType::ConnectionAccepted) {
        server_accepted = true;
        server_connection = event.connection_id;
      } else if (event.type == arv::net::NetworkEventType::MessageReceived &&
                 event.payload == std::vector<arv::u8>{1, 2, 3}) {
        server_received_payload = true;
      }
    }

    for (const arv::net::NetworkEvent& event : client.poll_events()) {
      if (event.type == arv::net::NetworkEventType::ConnectionAccepted) {
        client_accepted = true;
      } else if (event.type == arv::net::NetworkEventType::MessageReceived &&
                 event.payload == std::vector<arv::u8>{4, 5}) {
        client_received_payload = true;
      }
    }

    if (server_accepted && client_accepted && !sent_client_payload) {
      ASSERT_TRUE(client.send(
          {connect_result.value(),
           arv::protocol::ReliabilityClass::ReliableOrdered,
           std::vector<arv::u8>{1, 2, 3}}));
      sent_client_payload = true;
    }
    if (server_received_payload && !sent_server_payload) {
      ASSERT_TRUE(server.send(
          {server_connection,
           arv::protocol::ReliabilityClass::ReliableOrdered,
           std::vector<arv::u8>{4, 5}}));
      sent_server_payload = true;
    }

    if (!server_received_payload || !client_received_payload) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_TRUE(server_accepted);
  EXPECT_TRUE(client_accepted);
  EXPECT_TRUE(server_received_payload);
  EXPECT_TRUE(client_received_payload);
}
