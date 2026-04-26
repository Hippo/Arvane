#pragma once

#include "arv/net/transport.hpp"

#include <deque>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <steam/steamnetworkingsockets.h>

namespace arv::net {

class GameNetworkingSocketsEnvironment {
public:
  GameNetworkingSocketsEnvironment();

  GameNetworkingSocketsEnvironment(const GameNetworkingSocketsEnvironment&) = delete;
  GameNetworkingSocketsEnvironment& operator=(const GameNetworkingSocketsEnvironment&) = delete;
  ~GameNetworkingSocketsEnvironment();
  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const std::string& error_message() const noexcept;

private:
  bool initialized_{};
  std::string error_message_;
};

class GameNetworkingSocketsTransport final : public ITransport {
public:
  GameNetworkingSocketsTransport();

  GameNetworkingSocketsTransport(const GameNetworkingSocketsTransport&) = delete;
  GameNetworkingSocketsTransport& operator=(const GameNetworkingSocketsTransport&) = delete;
  ~GameNetworkingSocketsTransport() override;
  NetworkResult start_listening(const ListenAddress& address) override;
  ConnectResult connect(const NetworkAddress& address) override;
  NetworkResult send(TransportMessage message) override;
  std::vector<NetworkEvent> poll_events() override;
  ConnectionState state(ConnectionID connection_id) const override;
  void shutdown() override;

private:
  NetworkResult ensure_poll_group();
  static void connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info);
  void register_callback_owner();
  void unregister_callback_owner();
  void queue_connection_status_change(const SteamNetConnectionStatusChangedCallback_t& info);
  ConnectionID connection_id_for_handle(HSteamNetConnection handle);
  void set_state(ConnectionID id, ConnectionState state);
  void queue_event(NetworkEvent event);
  [[nodiscard]] static std::optional<u32> parse_ipv4(std::string_view value) noexcept;
  [[nodiscard]] static int send_flags(protocol::ReliabilityClass reliability) noexcept;

  ISteamNetworkingSockets* sockets_{};
  HSteamListenSocket listen_socket_{k_HSteamListenSocket_Invalid};
  HSteamNetPollGroup poll_group_{k_HSteamNetPollGroup_Invalid};
  u32 next_connection_id_{1};
  mutable std::mutex mutex_;
  std::unordered_map<ConnectionID, HSteamNetConnection> connections_;
  std::unordered_map<HSteamNetConnection, ConnectionID> connection_ids_;
  std::unordered_map<ConnectionID, ConnectionState> states_;
  std::deque<NetworkEvent> queued_events_;

  inline static std::mutex callback_mutex_;
  inline static GameNetworkingSocketsTransport* callback_owner_{};
};

} // namespace arv::net
