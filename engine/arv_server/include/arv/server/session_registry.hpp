#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"
#include "arv/net/connection_state.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace arv::server {

struct ReservedSessionIdentity {
  SessionID session_id{};
  PlayerID player_id{};
};

struct ServerSession {
  SessionID session_id{};
  PlayerID player_id{};
  net::ConnectionID connection_id{};
  net::ConnectionState connection_state{net::ConnectionState::ProtocolHandshake};
  WorldID world_id{};
  std::string profile_name{};
};

class SessionRegistry {
public:
  [[nodiscard]] ReservedSessionIdentity reserve_identity() noexcept;
  ServerSession& create(
      net::ConnectionID connection_id,
      ReservedSessionIdentity identity,
      WorldID world_id,
      std::string profile_name);
  [[nodiscard]] ServerSession* find(SessionID session_id) noexcept;
  [[nodiscard]] const ServerSession* find(SessionID session_id) const noexcept;
  [[nodiscard]] ServerSession* find_by_connection(net::ConnectionID connection_id) noexcept;
  [[nodiscard]] const ServerSession* find_by_connection(net::ConnectionID connection_id) const noexcept;
  bool remove_by_connection(net::ConnectionID connection_id);
  [[nodiscard]] usize size() const noexcept;
  [[nodiscard]] std::vector<SessionID> session_ids() const;

private:
  u64 next_session_id_{1};
  u64 next_player_id_{1};
  std::unordered_map<SessionID, ServerSession> sessions_;
  std::unordered_map<net::ConnectionID, SessionID> connection_index_;
};

} // namespace arv::server
