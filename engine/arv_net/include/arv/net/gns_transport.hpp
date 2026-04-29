#pragma once

#include "arv/core/types.hpp"
#include "arv/net/transport.hpp"

#include <atomic>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <concurrentqueue.h>
#include <steam/steamnetworkingsockets.h>

namespace arv::net {

namespace detail {

[[nodiscard]] int gns_send_flags(protocol::ReliabilityClass reliability) noexcept;

} // namespace detail

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
  struct OutboundSendCommand {
    TransportMessage message{};
  };

  static constexpr usize DefaultOutboundCommandQueueCapacity = 4096U;
  static constexpr usize MaxOutboundCommandsPerPoll = 512U;
  static constexpr usize MaxInboundMessagesPerPoll = 512U;

  NetworkResult ensure_poll_group();
  static void connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info);
  void register_callback_owner();
  void unregister_callback_owner();
  void handle_connection_status_change(const SteamNetConnectionStatusChangedCallback_t& info);
  void drain_outbound_send_commands();
  void send_outbound_command(OutboundSendCommand command);
  void receive_poll_group_messages();
  ConnectionID connection_id_for_handle(HSteamNetConnection handle);
  std::optional<ConnectionID> connection_id_for_message(const ISteamNetworkingMessage& message);
  void register_connection(ConnectionID id, HSteamNetConnection handle, ConnectionState state);
  void set_state(ConnectionID id, ConnectionState state);
  void queue_event(NetworkEvent event);
  void assert_owner_thread() const noexcept;
  void claim_or_assert_owner_thread() const noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;
  [[nodiscard]] static usize current_thread_hash() noexcept;
  [[nodiscard]] static std::optional<u32> parse_ipv4(std::string_view value) noexcept;

  ISteamNetworkingSockets* sockets_{};
  HSteamListenSocket listen_socket_{k_HSteamListenSocket_Invalid};
  HSteamNetPollGroup poll_group_{k_HSteamNetPollGroup_Invalid};
  u32 next_connection_id_{1};
  mutable std::atomic<usize> owner_thread_hash_{0U};
  std::atomic_bool accepting_sends_{true};
  moodycamel::ConcurrentQueue<OutboundSendCommand> outbound_sends_{DefaultOutboundCommandQueueCapacity};
  HashMap<ConnectionID, HSteamNetConnection> connections_;
  HashMap<HSteamNetConnection, ConnectionID> connection_ids_;
  HashMap<ConnectionID, ConnectionState> states_;
  std::vector<NetworkEvent> queued_events_;

  inline static std::atomic<GameNetworkingSocketsTransport*> callback_owner_{nullptr};
};

} // namespace arv::net
