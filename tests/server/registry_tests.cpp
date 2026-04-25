#include "arv/server/session_registry.hpp"
#include "arv/server/world_registry.hpp"

#include <gtest/gtest.h>

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
