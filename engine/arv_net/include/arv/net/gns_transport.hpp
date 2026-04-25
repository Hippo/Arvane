#pragma once

#include "arv/net/transport.hpp"

#include <array>
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
  GameNetworkingSocketsEnvironment() {
    SteamNetworkingErrMsg error_message{};
    initialized_ = GameNetworkingSockets_Init(nullptr, error_message);
    if (!initialized_) {
      error_message_ = error_message;
    }
  }

  GameNetworkingSocketsEnvironment(const GameNetworkingSocketsEnvironment&) = delete;
  GameNetworkingSocketsEnvironment& operator=(const GameNetworkingSocketsEnvironment&) = delete;

  ~GameNetworkingSocketsEnvironment() {
    if (initialized_) {
      GameNetworkingSockets_Kill();
    }
  }

  [[nodiscard]] bool initialized() const noexcept {
    return initialized_;
  }

  [[nodiscard]] const std::string& error_message() const noexcept {
    return error_message_;
  }

private:
  bool initialized_{};
  std::string error_message_;
};

class GameNetworkingSocketsTransport final : public ITransport {
public:
  GameNetworkingSocketsTransport() : sockets_(SteamNetworkingSockets()) {
    register_callback_owner();
  }

  GameNetworkingSocketsTransport(const GameNetworkingSocketsTransport&) = delete;
  GameNetworkingSocketsTransport& operator=(const GameNetworkingSocketsTransport&) = delete;

  ~GameNetworkingSocketsTransport() override {
    shutdown();
    unregister_callback_owner();
  }

  NetworkResult start_listening(const ListenAddress& address) override {
    if (sockets_ == nullptr) {
      return NetworkResult::failure(NetworkError{"GameNetworkingSockets interface is unavailable"});
    }

    SteamNetworkingIPAddr local_address;
    if (address.bind_address.empty() || address.bind_address == "0.0.0.0") {
      local_address.Clear();
      local_address.m_port = address.port;
    } else if (address.bind_address == "127.0.0.1" || address.bind_address == "localhost") {
      local_address.SetIPv6LocalHost(address.port);
    } else {
      auto parsed = parse_ipv4(address.bind_address);
      if (!parsed) {
        return NetworkResult::failure(NetworkError{"only wildcard, localhost, and IPv4 listen addresses are supported"});
      }
      local_address.SetIPv4(parsed.value(), address.port);
    }

    auto poll_group_result = ensure_poll_group();
    if (!poll_group_result) {
      return poll_group_result;
    }

    SteamNetworkingConfigValue_t callback_option;
    callback_option.SetPtr(
        k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        reinterpret_cast<void*>(connection_status_changed));

    listen_socket_ = sockets_->CreateListenSocketIP(local_address, 1, &callback_option);
    if (listen_socket_ == k_HSteamListenSocket_Invalid) {
      return NetworkResult::failure(NetworkError{"failed to create GameNetworkingSockets listen socket"});
    }

    return NetworkResult::success();
  }

  ConnectResult connect(const NetworkAddress& address) override {
    if (sockets_ == nullptr) {
      return ConnectResult::failure(NetworkError{"GameNetworkingSockets interface is unavailable"});
    }

    SteamNetworkingIPAddr remote_address;
    if (address.host == "localhost") {
      remote_address.SetIPv6LocalHost(address.port);
    } else {
      auto parsed = parse_ipv4(address.host);
      if (!parsed) {
        return ConnectResult::failure(NetworkError{"only localhost and IPv4 remote addresses are supported"});
      }
      remote_address.SetIPv4(parsed.value(), address.port);
    }

    auto poll_group_result = ensure_poll_group();
    if (!poll_group_result) {
      return ConnectResult::failure(NetworkError{poll_group_result.error().message});
    }

    SteamNetworkingConfigValue_t callback_option;
    callback_option.SetPtr(
        k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        reinterpret_cast<void*>(connection_status_changed));

    const HSteamNetConnection handle = sockets_->ConnectByIPAddress(remote_address, 1, &callback_option);
    if (handle == k_HSteamNetConnection_Invalid) {
      return ConnectResult::failure(NetworkError{"failed to create GameNetworkingSockets connection"});
    }

    const ConnectionID id{next_connection_id_++};
    {
      std::lock_guard lock(mutex_);
      connections_.emplace(id, handle);
      connection_ids_.emplace(handle, id);
      states_.emplace(id, ConnectionState::Connecting);
    }
    return ConnectResult::success(id);
  }

  NetworkResult send(TransportMessage message) override {
    if (sockets_ == nullptr) {
      return NetworkResult::failure(NetworkError{"GameNetworkingSockets interface is unavailable"});
    }

    HSteamNetConnection handle = k_HSteamNetConnection_Invalid;
    {
      std::lock_guard lock(mutex_);
      const auto found = connections_.find(message.connection_id);
      if (found == connections_.end()) {
        return NetworkResult::failure(NetworkError{"unknown connection"});
      }
      handle = found->second;
    }

    const EResult result = sockets_->SendMessageToConnection(
        handle,
        message.payload.data(),
        static_cast<u32>(message.payload.size()),
        send_flags(message.reliability),
        nullptr);

    if (result != k_EResultOK) {
      queue_event({
          NetworkEventType::MessageSendFailed,
          message.connection_id,
          message.reliability,
          {},
          "failed to send GameNetworkingSockets message",
          std::chrono::steady_clock::now()});
      return NetworkResult::failure(NetworkError{"failed to send GameNetworkingSockets message"});
    }

    return NetworkResult::success();
  }

  std::vector<NetworkEvent> poll_events() override {
    if (sockets_ != nullptr) {
      sockets_->RunCallbacks();
    }

    if (sockets_ != nullptr && poll_group_ != k_HSteamNetPollGroup_Invalid) {
      std::array<ISteamNetworkingMessage*, 32> messages{};
      int received = 0;
      do {
        received = sockets_->ReceiveMessagesOnPollGroup(
            poll_group_,
            messages.data(),
            static_cast<int>(messages.size()));

        for (int i = 0; i < received; ++i) {
          ISteamNetworkingMessage* raw_message = messages[static_cast<usize>(i)];
          const ConnectionID id = connection_id_for_handle(raw_message->m_conn);
          std::vector<u8> payload(
              static_cast<const u8*>(raw_message->m_pData),
              static_cast<const u8*>(raw_message->m_pData) + raw_message->m_cbSize);
          queue_event({
              NetworkEventType::MessageReceived,
              id,
              protocol::ReliabilityClass::ReliableOrdered,
              std::move(payload),
              {},
              std::chrono::steady_clock::now()});
          raw_message->Release();
        }
      } while (received == static_cast<int>(messages.size()));
    }

    std::lock_guard lock(mutex_);
    std::vector<NetworkEvent> events;
    events.reserve(queued_events_.size());
    while (!queued_events_.empty()) {
      events.push_back(std::move(queued_events_.front()));
      queued_events_.pop_front();
    }
    return events;
  }

  ConnectionState state(ConnectionID connection_id) const override {
    std::lock_guard lock(mutex_);
    const auto found = states_.find(connection_id);
    if (found == states_.end()) {
      return ConnectionState::Disconnected;
    }
    return found->second;
  }

  void shutdown() override {
    if (sockets_ == nullptr) {
      return;
    }

    std::vector<HSteamNetConnection> handles;
    {
      std::lock_guard lock(mutex_);
      handles.reserve(connections_.size());
      for (auto& [id, handle] : connections_) {
        (void)id;
        handles.push_back(handle);
      }
      states_.clear();
      connections_.clear();
      connection_ids_.clear();
      queued_events_.clear();
    }

    for (HSteamNetConnection handle : handles) {
      sockets_->CloseConnection(handle, k_ESteamNetConnectionEnd_App_Generic, "Arvane shutdown", false);
    }

    if (listen_socket_ != k_HSteamListenSocket_Invalid) {
      sockets_->CloseListenSocket(listen_socket_);
      listen_socket_ = k_HSteamListenSocket_Invalid;
    }

    if (poll_group_ != k_HSteamNetPollGroup_Invalid) {
      sockets_->DestroyPollGroup(poll_group_);
      poll_group_ = k_HSteamNetPollGroup_Invalid;
    }
  }

private:
  NetworkResult ensure_poll_group() {
    if (poll_group_ != k_HSteamNetPollGroup_Invalid) {
      return NetworkResult::success();
    }

    poll_group_ = sockets_->CreatePollGroup();
    if (poll_group_ == k_HSteamNetPollGroup_Invalid) {
      return NetworkResult::failure(NetworkError{"failed to create GameNetworkingSockets poll group"});
    }
    return NetworkResult::success();
  }

  static void connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
    GameNetworkingSocketsTransport* owner = nullptr;
    {
      std::lock_guard lock(callback_mutex_);
      owner = callback_owner_;
    }
    if (owner != nullptr && info != nullptr) {
      owner->queue_connection_status_change(*info);
    }
  }

  void register_callback_owner() {
    std::lock_guard lock(callback_mutex_);
    callback_owner_ = this;
  }

  void unregister_callback_owner() {
    std::lock_guard lock(callback_mutex_);
    if (callback_owner_ == this) {
      callback_owner_ = nullptr;
    }
  }

  void queue_connection_status_change(const SteamNetConnectionStatusChangedCallback_t& info) {
    if (sockets_ == nullptr) {
      return;
    }

    const auto state = info.m_info.m_eState;
    if (state == k_ESteamNetworkingConnectionState_Connecting &&
        info.m_info.m_hListenSocket == listen_socket_) {
      const EResult accepted = sockets_->AcceptConnection(info.m_hConn);
      const ConnectionID id = connection_id_for_handle(info.m_hConn);
      if (accepted == k_EResultOK && poll_group_ != k_HSteamNetPollGroup_Invalid) {
        sockets_->SetConnectionPollGroup(info.m_hConn, poll_group_);
      }
      set_state(id, accepted == k_EResultOK ? ConnectionState::TransportConnected
                                            : ConnectionState::Disconnecting);
      queue_event({
          NetworkEventType::ConnectionRequested,
          id,
          protocol::ReliabilityClass::ReliableOrdered,
          {},
          {},
          std::chrono::steady_clock::now()});
      queue_event({
          accepted == k_EResultOK ? NetworkEventType::ConnectionAccepted : NetworkEventType::ConnectionFailed,
          id,
          protocol::ReliabilityClass::ReliableOrdered,
          {},
          accepted == k_EResultOK ? std::string{} : "failed to accept GameNetworkingSockets connection",
          std::chrono::steady_clock::now()});
      return;
    }

    const ConnectionID id = connection_id_for_handle(info.m_hConn);
    if (state == k_ESteamNetworkingConnectionState_Connected) {
      if (poll_group_ != k_HSteamNetPollGroup_Invalid) {
        sockets_->SetConnectionPollGroup(info.m_hConn, poll_group_);
      }
      set_state(id, ConnectionState::TransportConnected);
      queue_event({
          NetworkEventType::ConnectionAccepted,
          id,
          protocol::ReliabilityClass::ReliableOrdered,
          {},
          {},
          std::chrono::steady_clock::now()});
      return;
    }

    if (state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
        state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
      set_state(id, ConnectionState::Disconnected);
      queue_event({
          state == k_ESteamNetworkingConnectionState_ClosedByPeer ? NetworkEventType::ConnectionClosed
                                                                 : NetworkEventType::ConnectionFailed,
          id,
          protocol::ReliabilityClass::ReliableOrdered,
          {},
          info.m_info.m_szEndDebug,
          std::chrono::steady_clock::now()});
    }
  }

  ConnectionID connection_id_for_handle(HSteamNetConnection handle) {
    std::lock_guard lock(mutex_);
    const auto found = connection_ids_.find(handle);
    if (found != connection_ids_.end()) {
      return found->second;
    }

    const ConnectionID id{next_connection_id_++};
    connection_ids_.emplace(handle, id);
    connections_.emplace(id, handle);
    states_.emplace(id, ConnectionState::TransportConnected);
    return id;
  }

  void set_state(ConnectionID id, ConnectionState state) {
    std::lock_guard lock(mutex_);
    states_[id] = state;
  }

  void queue_event(NetworkEvent event) {
    std::lock_guard lock(mutex_);
    queued_events_.push_back(std::move(event));
  }

  [[nodiscard]] static std::optional<u32> parse_ipv4(std::string_view value) noexcept {
    u32 parts[4]{};
    usize part_index = 0;
    u32 current = 0;
    bool has_digit = false;

    for (char ch : value) {
      if (ch >= '0' && ch <= '9') {
        has_digit = true;
        current = current * 10U + static_cast<u32>(ch - '0');
        if (current > 255U) {
          return std::nullopt;
        }
        continue;
      }

      if (ch == '.' && has_digit && part_index < 3) {
        parts[part_index++] = current;
        current = 0;
        has_digit = false;
        continue;
      }

      return std::nullopt;
    }

    if (!has_digit || part_index != 3) {
      return std::nullopt;
    }
    parts[part_index] = current;

    return (parts[0] << 24U) | (parts[1] << 16U) | (parts[2] << 8U) | parts[3];
  }

  [[nodiscard]] static int send_flags(protocol::ReliabilityClass reliability) noexcept {
    switch (reliability) {
    case protocol::ReliabilityClass::ReliableOrdered:
      return k_nSteamNetworkingSend_Reliable;
    case protocol::ReliabilityClass::UnreliableLatestWins:
    case protocol::ReliabilityClass::UnreliableEvent:
      return k_nSteamNetworkingSend_UnreliableNoNagle;
    }
    return k_nSteamNetworkingSend_Reliable;
  }

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
