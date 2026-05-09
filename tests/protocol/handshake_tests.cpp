#include "arv/protocol/handshake.hpp"

#include <gtest/gtest.h>

TEST(ProtocolHandshakeTests, ClientHelloRoundTrips) {
  arv::protocol::ServerboundHello hello;
  hello.client_build_id = "test-build";
  hello.registry_hashes_known_by_client.push_back({"blocks", 42});

  const std::vector<arv::u8> bytes = arv::protocol::serialize_serverbound_hello(hello);
  const auto decoded = arv::protocol::deserialize_serverbound_hello(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().client_build_id, "test-build");
  ASSERT_EQ(decoded.value().registry_hashes_known_by_client.size(), 1U);
  EXPECT_EQ(decoded.value().registry_hashes_known_by_client[0].hash, 42U);
}

TEST(ProtocolHandshakeTests, ServerHelloRoundTripsAssignedIds) {
  arv::protocol::ClientboundHello hello;
  hello.accepted = true;
  hello.assigned_session_id = arv::SessionID{7};
  hello.assigned_player_id = arv::PlayerID{9};
  hello.initial_world_id = arv::WorldID{3};

  const std::vector<arv::u8> bytes = arv::protocol::serialize_clientbound_hello(hello);
  const auto decoded = arv::protocol::deserialize_clientbound_hello(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_TRUE(decoded.value().accepted);
  EXPECT_EQ(decoded.value().assigned_session_id, arv::SessionID{7});
  EXPECT_EQ(decoded.value().assigned_player_id, arv::PlayerID{9});
  EXPECT_EQ(decoded.value().initial_world_id, arv::WorldID{3});
}

TEST(ProtocolHandshakeTests, RejectsUnknownRegistryHash) {
  arv::protocol::ServerboundHello client;
  client.registry_hashes_known_by_client.push_back({"blocks", 1});

  arv::protocol::HandshakePolicy policy;
  policy.assigned_session_id = arv::SessionID{1};
  policy.assigned_player_id = arv::PlayerID{1};
  policy.initial_world_id = arv::WorldID{1};
  policy.authoritative_registry_hashes.push_back({"blocks", 2});

  const arv::protocol::ClientboundHello server = arv::protocol::make_clientbound_hello(client, policy);

  EXPECT_FALSE(server.accepted);
  EXPECT_TRUE(server.required_followup_sync);
  EXPECT_EQ(server.reject_reason, "registry sync required");
}
