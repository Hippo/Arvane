#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/net/connection_state.hpp"
#include "arv/protocol/auth.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace arv::server {

struct ReservedSessionIdentity {
  SessionID session_id{};
  PlayerID player_id{};
};

struct PendingAuthChallenge {
  protocol::AuthChallengeNonce nonce{};
  u64 issued_server_tick{};
  u64 expires_server_tick{};
  protocol::AuthServerInstanceID server_instance_id{};
};

struct SessionProfileIdentity {
  crypto::PlayerProfileID profile_id{};
  std::string display_name{"local_player"};
  crypto::Ed25519PublicKey public_key{};
  bool has_public_key{};
};

struct ServerSession {
  SessionID session_id{};
  PlayerID player_id{};
  net::ConnectionID connection_id{};
  net::ConnectionState connection_state{net::ConnectionState::ProtocolHandshake};
  WorldID world_id{};
  std::string profile_name{};
  crypto::PlayerProfileID profile_id{};
  std::string display_name{"local_player"};
  crypto::Ed25519PublicKey public_key{};
  bool has_public_key{};
  bool authenticated{};
  bool pending_auth{true};
  std::optional<PendingAuthChallenge> challenge{};
  std::chrono::steady_clock::time_point last_keep_alive_sent_time{};
  std::chrono::steady_clock::time_point last_keep_alive_received_time{};
  u64 last_keep_alive_id{};
  bool closing{};
};

class SessionRegistry {
public:
  [[nodiscard]] ReservedSessionIdentity reserve_identity() noexcept;
  ServerSession& create(
      net::ConnectionID connection_id,
      ReservedSessionIdentity identity,
      WorldID world_id,
      std::string profile_name);
  ServerSession& create(
      net::ConnectionID connection_id,
      ReservedSessionIdentity identity,
      WorldID world_id,
      SessionProfileIdentity profile,
      bool authenticated);
  [[nodiscard]] ServerSession* find(SessionID session_id) noexcept;
  [[nodiscard]] const ServerSession* find(SessionID session_id) const noexcept;
  [[nodiscard]] const ServerSession* first() const noexcept;
  [[nodiscard]] ServerSession* find_by_connection(net::ConnectionID connection_id) noexcept;
  [[nodiscard]] const ServerSession* find_by_connection(net::ConnectionID connection_id) const noexcept;
  bool remove_by_connection(net::ConnectionID connection_id);
  [[nodiscard]] usize size() const noexcept;
  void for_each_session(FunctionRef<void(const ServerSession&)> callback) const;
  [[nodiscard]] std::vector<SessionID> session_ids() const;

private:
  u64 next_session_id_{1};
  u64 next_player_id_{1};
  StableHashMap<SessionID, ServerSession> sessions_;
  HashMap<net::ConnectionID, SessionID> connection_index_;
};

} // namespace arv::server
