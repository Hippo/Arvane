#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"
#include "arv/net/connection_state.hpp"

#include <string>
#include <unordered_map>

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
  [[nodiscard]] ReservedSessionIdentity reserve_identity() noexcept {
    return {SessionID{next_session_id_++}, PlayerID{next_player_id_++}};
  }

  ServerSession& create(
      net::ConnectionID connection_id,
      ReservedSessionIdentity identity,
      WorldID world_id,
      std::string profile_name) {
    ServerSession session;
    session.session_id = identity.session_id;
    session.player_id = identity.player_id;
    session.connection_id = connection_id;
    session.connection_state = net::ConnectionState::InGame;
    session.world_id = world_id;
    session.profile_name = std::move(profile_name);

    auto [it, inserted] = sessions_.insert_or_assign(session.session_id, std::move(session));
    (void)inserted;
    connection_index_[connection_id] = it->first;
    return it->second;
  }

  [[nodiscard]] ServerSession* find(SessionID session_id) noexcept {
    const auto found = sessions_.find(session_id);
    return found == sessions_.end() ? nullptr : &found->second;
  }

  [[nodiscard]] const ServerSession* find(SessionID session_id) const noexcept {
    const auto found = sessions_.find(session_id);
    return found == sessions_.end() ? nullptr : &found->second;
  }

  [[nodiscard]] ServerSession* find_by_connection(net::ConnectionID connection_id) noexcept {
    const auto found_id = connection_index_.find(connection_id);
    if (found_id == connection_index_.end()) {
      return nullptr;
    }
    return find(found_id->second);
  }

  [[nodiscard]] const ServerSession* find_by_connection(net::ConnectionID connection_id) const noexcept {
    const auto found_id = connection_index_.find(connection_id);
    if (found_id == connection_index_.end()) {
      return nullptr;
    }
    return find(found_id->second);
  }

  bool remove_by_connection(net::ConnectionID connection_id) {
    const auto found_id = connection_index_.find(connection_id);
    if (found_id == connection_index_.end()) {
      return false;
    }

    sessions_.erase(found_id->second);
    connection_index_.erase(found_id);
    return true;
  }

  [[nodiscard]] usize size() const noexcept {
    return sessions_.size();
  }

private:
  u64 next_session_id_{1};
  u64 next_player_id_{1};
  std::unordered_map<SessionID, ServerSession> sessions_;
  std::unordered_map<net::ConnectionID, SessionID> connection_index_;
};

} // namespace arv::server
