#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/server/server_runtime.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace arv::testsupport {

inline void complete_integrated_handshake(
    server::ServerRuntime& server,
    client::ClientRuntime& client) {
  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.connection_state(), net::ConnectionState::InGame);
}

inline player::PlayerState& connected_player(server::ServerRuntime& server) {
  const std::vector<SessionID> session_ids = server.sessions().session_ids();
  EXPECT_EQ(session_ids.size(), 1U);
  const server::ServerSession* session = server.sessions().find(session_ids.front());
  EXPECT_NE(session, nullptr);
  player::PlayerState* player_state = server.voxel_world().find_player(session->player_id);
  EXPECT_NE(player_state, nullptr);
  return *player_state;
}

} // namespace arv::testsupport
