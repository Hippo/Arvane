#include "arv/client/client_runtime.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/server/world_registry.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

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
  EXPECT_NE(server.worlds().find(arv::server::DefaultWorldID), nullptr);
}
