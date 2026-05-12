#include "arv/server/session_registry.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/world/world_generation.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>

TEST(ServerRegistryTests, SessionRegistryLooksUpByConnection) {
  arv::server::SessionRegistry sessions;
  const arv::server::ReservedSessionIdentity identity = sessions.reserve_identity();

  arv::server::ServerSession& session = sessions.create(
      arv::net::ConnectionID{5},
      identity,
      arv::WorldID{1},
      "tester");

  EXPECT_EQ(session.session_id, identity.session_id);
  ASSERT_NE(sessions.find_by_connection(arv::net::ConnectionID{5}), nullptr);
  EXPECT_EQ(sessions.find_by_connection(arv::net::ConnectionID{5})->profile_name, "tester");

  EXPECT_TRUE(sessions.remove_by_connection(arv::net::ConnectionID{5}));
  EXPECT_EQ(sessions.find(identity.session_id), nullptr);
}

TEST(ServerRegistryTests, WorldRegistryEnsuresDefaultWorld) {
  arv::server::WorldRegistry worlds;
  worlds.ensure_default_world();

  const arv::server::WorldRecord* world = worlds.find(arv::server::DefaultWorldID);

  ASSERT_NE(world, nullptr);
  EXPECT_EQ(world->name, "default");
  EXPECT_EQ(world->state, arv::server::WorldRuntimeState::Running);
}

TEST(ServerRuntimeTests, ExplicitWorldHeightOverridesGenerationBounds) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);

  arv::world::WorldGenerationConfig config;
  config.min_y = -64;
  config.max_y = 128;
  config.surface_layers.push_back({"arvane:stone", 0, true});
  auto palette = arv::world::resolve_world_generation_palette(config, block_registry);
  ASSERT_TRUE(palette);

  arv::server::ServerChunkGenerationSettings generation;
  generation.enabled = true;
  generation.seed = 7;
  generation.config = config;
  generation.palette = std::move(palette.value());

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.chunk_generation = std::move(generation);
  start_info.world_height = arv::world::WorldHeightSettings{-1, 1};

  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start(start_info));

  const arv::world::WorldHeightSettings height = server.voxel_world().height_settings();
  EXPECT_EQ(height.min_chunk_y, -1);
  EXPECT_EQ(height.max_chunk_y, 1);
  server.shutdown();
}

TEST(ServerRuntimeTests, MissingConfiguredBlockBreakBehaviorFailsStartup) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.behavior_id = "test:block_break/missing";

  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  const arv::net::NetworkResult result = server.start(start_info);

  ASSERT_FALSE(result);
  EXPECT_NE(result.error().message.find("missing block break behavior handler"), std::string::npos);
}
