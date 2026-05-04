#include "arv/server/session_registry.hpp"

#include <utility>

namespace arv::server {

ReservedSessionIdentity SessionRegistry::reserve_identity() noexcept {
  return {SessionID{next_session_id_++}, PlayerID{next_player_id_++}};
}

ServerSession& SessionRegistry::create(net::ConnectionID connection_id, ReservedSessionIdentity identity, WorldID world_id, std::string profile_name) {
  SessionProfileIdentity profile;
  profile.display_name = profile_name;
  return create(connection_id, identity, world_id, std::move(profile), true);
}

ServerSession& SessionRegistry::create(
    net::ConnectionID connection_id,
    ReservedSessionIdentity identity,
    WorldID world_id,
    SessionProfileIdentity profile,
    bool authenticated) {
  ServerSession session;
  session.session_id = identity.session_id;
  session.player_id = identity.player_id;
  session.connection_id = connection_id;
  session.connection_state =
      authenticated ? net::ConnectionState::InGame : net::ConnectionState::ProtocolHandshake;
  session.world_id = world_id;
  session.profile_id = profile.profile_id;
  session.display_name = profile.display_name.empty() ? "local_player" : std::move(profile.display_name);
  session.profile_name = session.display_name;
  session.public_key = profile.public_key;
  session.has_public_key = profile.has_public_key;
  session.authenticated = authenticated;
  session.pending_auth = !authenticated;

  auto [it, inserted] = sessions_.insert_or_assign(session.session_id, std::move(session));
  (void)inserted;
  connection_index_[connection_id] = it->first;
  return it->second;
}

ServerSession* SessionRegistry::find(SessionID session_id) noexcept {
  const auto found = sessions_.find(session_id);
  return found == sessions_.end() ? nullptr : &found->second;
}

const ServerSession* SessionRegistry::find(SessionID session_id) const noexcept {
  const auto found = sessions_.find(session_id);
  return found == sessions_.end() ? nullptr : &found->second;
}

const ServerSession* SessionRegistry::first() const noexcept {
  return sessions_.empty() ? nullptr : &sessions_.begin()->second;
}

ServerSession* SessionRegistry::find_by_connection(net::ConnectionID connection_id) noexcept {
  const auto found_id = connection_index_.find(connection_id);
  if (found_id == connection_index_.end()) {
    return nullptr;
  }
  return find(found_id->second);
}

const ServerSession* SessionRegistry::find_by_connection(net::ConnectionID connection_id) const noexcept {
  const auto found_id = connection_index_.find(connection_id);
  if (found_id == connection_index_.end()) {
    return nullptr;
  }
  return find(found_id->second);
}

bool SessionRegistry::remove_by_connection(net::ConnectionID connection_id) {
  const auto found_id = connection_index_.find(connection_id);
  if (found_id == connection_index_.end()) {
    return false;
  }

  sessions_.erase(found_id->second);
  connection_index_.erase(found_id);
  return true;
}

usize SessionRegistry::size() const noexcept {
  return sessions_.size();
}

void SessionRegistry::for_each_session(FunctionRef<void(const ServerSession&)> callback) const {
  if (!callback) {
    return;
  }
  for (const auto& [session_id, session] : sessions_) {
    (void)session_id;
    callback(session);
  }
}

std::vector<SessionID> SessionRegistry::session_ids() const {
  std::vector<SessionID> ids;
  ids.reserve(sessions_.size());
  for (const auto& [session_id, session] : sessions_) {
    (void)session;
    ids.emplace_back(session_id);
  }
  return ids;
}

} // namespace arv::server
