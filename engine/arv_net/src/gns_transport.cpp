#include "arv/net/gns_transport.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <steam/isteamnetworkingutils.h>

namespace arv::net {
namespace {

inline constexpr int GNSLaneCount = 5;
inline constexpr int GNSConnectionSendBufferBytes = 8 * 1024 * 1024;
inline constexpr int GNSConnectionReceiveBufferBytes = 8 * 1024 * 1024;
inline constexpr int GNSConnectionReceiveMaxMessageBytes = 1024 * 1024;
inline constexpr int GNSConnectionSendRateBytesPerSecond = 4 * 1024 * 1024;

[[nodiscard]] const char* reliability_name(protocol::ReliabilityClass reliability) noexcept {
  switch (reliability) {
    case protocol::ReliabilityClass::ReliableOrdered:
      return "reliable_ordered";
    case protocol::ReliabilityClass::ReliableImmediate:
      return "reliable_immediate";
    case protocol::ReliabilityClass::UnreliableLatestWins:
      return "unreliable_latest_wins";
    case protocol::ReliabilityClass::UnreliableEvent:
      return "unreliable_event";
  }
  return "unknown";
}

[[nodiscard]] std::string send_failure_reason(
    EResult result,
    const TransportMessage& message) {
  return "failed to send GameNetworkingSockets message: result=" +
         std::to_string(static_cast<int>(result)) +
         " reliability=" + reliability_name(message.reliability) +
         " lane=" + std::to_string(detail::gns_lane_index(message.lane)) +
         " bytes=" + std::to_string(message.payload.size()) +
         " type=" + std::to_string(static_cast<u16>(message.message_type));
}

void set_common_connection_options(
    std::array<SteamNetworkingConfigValue_t, 6>& options,
    FnSteamNetConnectionStatusChanged callback) {
  options[0].SetPtr(
      k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
      reinterpret_cast<void*>(callback));
  options[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, GNSConnectionSendBufferBytes);
  options[2].SetInt32(k_ESteamNetworkingConfig_RecvBufferSize, GNSConnectionReceiveBufferBytes);
  options[3].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize, GNSConnectionReceiveMaxMessageBytes);
  options[4].SetInt32(k_ESteamNetworkingConfig_SendRateMin, GNSConnectionSendRateBytesPerSecond);
  options[5].SetInt32(k_ESteamNetworkingConfig_SendRateMax, GNSConnectionSendRateBytesPerSecond);
}

} // namespace

GameNetworkingSocketsEnvironment::GameNetworkingSocketsEnvironment() {
  SteamNetworkingErrMsg error_message{};
  initialized_ = GameNetworkingSockets_Init(nullptr, error_message);
  if (!initialized_) {
    error_message_ = error_message;
  }
}

GameNetworkingSocketsEnvironment::~GameNetworkingSocketsEnvironment() {
  if (initialized_) {
    GameNetworkingSockets_Kill();
  }
}

bool GameNetworkingSocketsEnvironment::initialized() const noexcept {
  return initialized_;
}

const std::string& GameNetworkingSocketsEnvironment::error_message() const noexcept {
  return error_message_;
}

GameNetworkingSocketsTransport::GameNetworkingSocketsTransport() : sockets_(SteamNetworkingSockets()) {
}

GameNetworkingSocketsTransport::~GameNetworkingSocketsTransport() {
  shutdown();
}

NetworkResult GameNetworkingSocketsTransport::start_listening(const ListenAddress& address) {
  claim_or_assert_owner_thread();
  if (sockets_ == nullptr) {
    return NetworkResult::failure(NetworkError{"GameNetworkingSockets interface is unavailable"});
  }

  SteamNetworkingIPAddr local_address;
  if (address.bind_address.empty()) {
    local_address.Clear();
    local_address.m_port = address.port;
  } else if (address.bind_address == "127.0.0.1" || address.bind_address == "localhost") {
    local_address.SetIPv4(0x7f000001U, address.port);
  } else if (address.bind_address == "0.0.0.0") {
    local_address.SetIPv4(0U, address.port);
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

  std::array<SteamNetworkingConfigValue_t, 6> options{};
  set_common_connection_options(options, connection_status_changed);

  listen_socket_ = sockets_->CreateListenSocketIP(
      local_address,
      static_cast<int>(options.size()),
      options.data());
  if (listen_socket_ == k_HSteamListenSocket_Invalid) {
    return NetworkResult::failure(NetworkError{"failed to create GameNetworkingSockets listen socket"});
  }
  register_listen_socket_owner();

  SteamNetworkingIPAddr bound_address;
  if (sockets_->GetListenSocketAddress(listen_socket_, &bound_address)) {
    bound_port_ = bound_address.m_port;
  } else {
    bound_port_ = address.port;
  }

  return NetworkResult::success();
}

ConnectResult GameNetworkingSocketsTransport::connect(const NetworkAddress& address) {
  claim_or_assert_owner_thread();
  if (sockets_ == nullptr) {
    return ConnectResult::failure(NetworkError{"GameNetworkingSockets interface is unavailable"});
  }

  SteamNetworkingIPAddr remote_address;
  if (address.host == "localhost") {
    remote_address.SetIPv4(0x7f000001U, address.port);
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

  std::array<SteamNetworkingConfigValue_t, 6> options{};
  set_common_connection_options(options, connection_status_changed);

  const HSteamNetConnection handle = sockets_->ConnectByIPAddress(
      remote_address,
      static_cast<int>(options.size()),
      options.data());
  if (handle == k_HSteamNetConnection_Invalid) {
    return ConnectResult::failure(NetworkError{"failed to create GameNetworkingSockets connection"});
  }

  const ConnectionID id{next_connection_id_++};
  register_connection(id, handle, ConnectionState::Connecting);
  return ConnectResult::success(id);
}

NetworkResult GameNetworkingSocketsTransport::send(TransportMessage message) {
  if (sockets_ == nullptr || !accepting_sends_.load(std::memory_order_acquire)) {
    return NetworkResult::failure(NetworkError{"GameNetworkingSockets transport is not accepting sends"});
  }

  if (is_owner_thread()) {
    return send_outbound_command(OutboundSendCommand{std::move(message)});
  }

  if (!outbound_sends_.try_push(OutboundSendCommand{std::move(message)})) {
    return NetworkResult::failure(NetworkError{"GameNetworkingSockets outbound send queue is full"});
  }

  return NetworkResult::success();
}

NetworkResult GameNetworkingSocketsTransport::close_connection(
    ConnectionID connection_id,
    std::string_view reason) {
  if (owner_thread_hash_.load(std::memory_order_acquire) == 0U || is_owner_thread()) {
    claim_or_assert_owner_thread();
    return close_connection_on_owner(connection_id, reason);
  }

  if (!accepting_sends_.load(std::memory_order_acquire)) {
    return NetworkResult::failure(NetworkError{"GameNetworkingSockets transport is not accepting closes"});
  }
  if (!outbound_closes_.try_push(OutboundCloseCommand{
          connection_id,
          std::string{reason}})) {
    return NetworkResult::failure(NetworkError{"GameNetworkingSockets outbound close queue is full"});
  }
  return NetworkResult::success();
}

NetworkResult GameNetworkingSocketsTransport::close_connection_on_owner(
    ConnectionID connection_id,
    std::string_view reason) {
  assert_owner_thread();
  if (sockets_ == nullptr) {
    return NetworkResult::failure(NetworkError{"GameNetworkingSockets interface is unavailable"});
  }

  const auto found = connections_.find(connection_id);
  if (found == connections_.end()) {
    states_[connection_id] = ConnectionState::Disconnected;
    return NetworkResult::success();
  }

  const HSteamNetConnection handle = found->second;
  const std::string reason_text = reason.empty() ? std::string{"Arvane close"}
                                                 : std::string{reason};
  (void)sockets_->FlushMessagesOnConnection(handle);
  unregister_connection_owner(handle);
  sockets_->CloseConnection(
      handle,
      k_ESteamNetConnectionEnd_App_Generic,
      reason_text.c_str(),
      true);
  connection_ids_.erase(handle);
  connections_.erase(found);
  states_[connection_id] = ConnectionState::Disconnected;
  return NetworkResult::success();
}

std::vector<NetworkEvent> GameNetworkingSocketsTransport::poll_events() {
  claim_or_assert_owner_thread();
  drain_outbound_close_commands();
  drain_outbound_send_commands();

  if (sockets_ != nullptr) {
    sockets_->RunCallbacks();
  }

  receive_poll_group_messages();

  std::vector<NetworkEvent> events;
  events.swap(queued_events_);
  return events;
}

ConnectionState GameNetworkingSocketsTransport::state(ConnectionID connection_id) const {
  claim_or_assert_owner_thread();
  const auto found = states_.find(connection_id);
  if (found == states_.end()) {
    return ConnectionState::Disconnected;
  }
  return found->second;
}

void GameNetworkingSocketsTransport::shutdown() {
  claim_or_assert_owner_thread();
  accepting_sends_.store(false, std::memory_order_release);

  if (sockets_ == nullptr) {
    return;
  }

  OutboundSendCommand discarded;
  while (outbound_sends_.try_pop(discarded)) {
    discarded = {};
  }
  OutboundCloseCommand discarded_close;
  while (outbound_closes_.try_pop(discarded_close)) {
    discarded_close = {};
  }

  std::vector<HSteamNetConnection> handles;
  handles.reserve(connections_.size());
  for (auto& [id, handle] : connections_) {
    (void)id;
    handles.emplace_back(handle);
  }
  states_.clear();
  connections_.clear();
  connection_ids_.clear();
  queued_events_.clear();

  for (HSteamNetConnection handle : handles) {
    unregister_connection_owner(handle);
    sockets_->CloseConnection(handle, k_ESteamNetConnectionEnd_App_Generic, "Arvane shutdown", false);
  }

  if (listen_socket_ != k_HSteamListenSocket_Invalid) {
    unregister_listen_socket_owner();
    sockets_->CloseListenSocket(listen_socket_);
    listen_socket_ = k_HSteamListenSocket_Invalid;
  }

  if (poll_group_ != k_HSteamNetPollGroup_Invalid) {
    sockets_->DestroyPollGroup(poll_group_);
    poll_group_ = k_HSteamNetPollGroup_Invalid;
  }
  bound_port_ = 0;
}

NetworkResult GameNetworkingSocketsTransport::ensure_poll_group() {
  assert_owner_thread();
  if (poll_group_ != k_HSteamNetPollGroup_Invalid) {
    return NetworkResult::success();
  }

  poll_group_ = sockets_->CreatePollGroup();
  if (poll_group_ == k_HSteamNetPollGroup_Invalid) {
    return NetworkResult::failure(NetworkError{"failed to create GameNetworkingSockets poll group"});
  }
  return NetworkResult::success();
}

void GameNetworkingSocketsTransport::connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
  GameNetworkingSocketsTransport* owner = info == nullptr ? nullptr : callback_owner_for(*info);
  if (owner != nullptr && info != nullptr) {
    assert(owner->is_owner_thread() && "GNS callbacks must be dispatched by the transport owner thread");
    owner->handle_connection_status_change(*info);
  }
}

GameNetworkingSocketsTransport* GameNetworkingSocketsTransport::callback_owner_for(
    const SteamNetConnectionStatusChangedCallback_t& info) {
  std::scoped_lock lock(callback_owner_mutex_);
  if (info.m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
    const auto listen_owner = listen_socket_callback_owners_.find(info.m_info.m_hListenSocket);
    if (listen_owner != listen_socket_callback_owners_.end()) {
      return listen_owner->second;
    }
  }

  const auto connection_owner = connection_callback_owners_.find(info.m_hConn);
  return connection_owner == connection_callback_owners_.end() ? nullptr : connection_owner->second;
}

void GameNetworkingSocketsTransport::register_listen_socket_owner() {
  if (listen_socket_ == k_HSteamListenSocket_Invalid) {
    return;
  }
  std::scoped_lock lock(callback_owner_mutex_);
  listen_socket_callback_owners_[listen_socket_] = this;
}

void GameNetworkingSocketsTransport::unregister_listen_socket_owner() {
  if (listen_socket_ == k_HSteamListenSocket_Invalid) {
    return;
  }
  std::scoped_lock lock(callback_owner_mutex_);
  const auto found = listen_socket_callback_owners_.find(listen_socket_);
  if (found != listen_socket_callback_owners_.end() && found->second == this) {
    listen_socket_callback_owners_.erase(found);
  }
}

void GameNetworkingSocketsTransport::register_connection_owner(HSteamNetConnection handle) {
  if (handle == k_HSteamNetConnection_Invalid) {
    return;
  }
  std::scoped_lock lock(callback_owner_mutex_);
  connection_callback_owners_[handle] = this;
}

void GameNetworkingSocketsTransport::unregister_connection_owner(HSteamNetConnection handle) {
  if (handle == k_HSteamNetConnection_Invalid) {
    return;
  }
  std::scoped_lock lock(callback_owner_mutex_);
  const auto found = connection_callback_owners_.find(handle);
  if (found != connection_callback_owners_.end() && found->second == this) {
    connection_callback_owners_.erase(found);
  }
}

void GameNetworkingSocketsTransport::configure_connection(HSteamNetConnection handle) {
  if (sockets_ == nullptr || handle == k_HSteamNetConnection_Invalid) {
    return;
  }

  ISteamNetworkingUtils* utils = SteamNetworkingUtils();
  if (utils != nullptr) {
    (void)utils->SetConnectionConfigValueInt32(
        handle,
        k_ESteamNetworkingConfig_SendBufferSize,
        GNSConnectionSendBufferBytes);
    (void)utils->SetConnectionConfigValueInt32(
        handle,
        k_ESteamNetworkingConfig_RecvBufferSize,
        GNSConnectionReceiveBufferBytes);
    (void)utils->SetConnectionConfigValueInt32(
        handle,
        k_ESteamNetworkingConfig_RecvMaxMessageSize,
        GNSConnectionReceiveMaxMessageBytes);
    (void)utils->SetConnectionConfigValueInt32(
        handle,
        k_ESteamNetworkingConfig_SendRateMin,
        GNSConnectionSendRateBytesPerSecond);
    (void)utils->SetConnectionConfigValueInt32(
        handle,
        k_ESteamNetworkingConfig_SendRateMax,
        GNSConnectionSendRateBytesPerSecond);
  }

  constexpr std::array<int, GNSLaneCount> priorities{{
      0, // session
      2, // gameplay
      1, // movement
      3, // chunk streaming
      4, // debug
  }};
  constexpr std::array<uint16, GNSLaneCount> weights{{
      1,
      4,
      8,
      1,
      1,
  }};
  (void)sockets_->ConfigureConnectionLanes(
      handle,
      GNSLaneCount,
      priorities.data(),
      weights.data());
}

void GameNetworkingSocketsTransport::handle_connection_status_change(const SteamNetConnectionStatusChangedCallback_t& info) {
  assert_owner_thread();
  if (sockets_ == nullptr) {
    return;
  }

  const auto state = info.m_info.m_eState;
  if (state == k_ESteamNetworkingConnectionState_Connecting &&
      listen_socket_ != k_HSteamListenSocket_Invalid &&
      info.m_info.m_hListenSocket == listen_socket_) {
    const EResult accepted = sockets_->AcceptConnection(info.m_hConn);
    const ConnectionID id = connection_id_for_handle(info.m_hConn);
    if (accepted == k_EResultOK && poll_group_ != k_HSteamNetPollGroup_Invalid) {
      sockets_->SetConnectionPollGroup(info.m_hConn, poll_group_);
    }
    if (accepted == k_EResultOK) {
      configure_connection(info.m_hConn);
    }
    set_state(id, accepted == k_EResultOK ? ConnectionState::TransportConnected
                                          : ConnectionState::Disconnecting);
    queue_event({NetworkEventType::ConnectionRequested,
                 id,
                 protocol::ReliabilityClass::ReliableOrdered,
                 {},
                 {},
                 std::chrono::steady_clock::now()});
    queue_event({accepted == k_EResultOK ? NetworkEventType::ConnectionAccepted : NetworkEventType::ConnectionFailed,
                 id,
                 protocol::ReliabilityClass::ReliableOrdered,
                 {},
                 accepted == k_EResultOK
                     ? std::string{}
                     : "failed to accept GameNetworkingSockets connection: result=" +
                           std::to_string(static_cast<int>(accepted)),
                 std::chrono::steady_clock::now()});
    return;
  }

  const ConnectionID id = connection_id_for_handle(info.m_hConn);
  if (state == k_ESteamNetworkingConnectionState_Connected) {
    if (poll_group_ != k_HSteamNetPollGroup_Invalid) {
      sockets_->SetConnectionPollGroup(info.m_hConn, poll_group_);
    }
    configure_connection(info.m_hConn);
    set_state(id, ConnectionState::TransportConnected);
    queue_event({NetworkEventType::ConnectionAccepted,
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
    queue_event({state == k_ESteamNetworkingConnectionState_ClosedByPeer ? NetworkEventType::ConnectionClosed
                                                                         : NetworkEventType::ConnectionFailed,
                 id,
                 protocol::ReliabilityClass::ReliableOrdered,
                 {},
                 info.m_info.m_szEndDebug,
                 std::chrono::steady_clock::now()});
  }
}

void GameNetworkingSocketsTransport::drain_outbound_close_commands() {
  assert_owner_thread();
  OutboundCloseCommand command;
  usize drained = 0;
  while (drained < MaxOutboundCommandsPerPoll &&
         outbound_closes_.try_pop(command)) {
    (void)close_connection_on_owner(command.connection_id, command.reason);
    command = {};
    ++drained;
  }
}

void GameNetworkingSocketsTransport::drain_outbound_send_commands() {
  assert_owner_thread();
  OutboundSendCommand command;
  usize drained = 0;
  while (drained < MaxOutboundCommandsPerPoll &&
         outbound_sends_.try_pop(command)) {
    NetworkResult result = send_outbound_command(command);
    if (!result) {
      queue_event({NetworkEventType::MessageSendFailed,
                   command.message.connection_id,
                   command.message.reliability,
                   {},
                   result.error().message,
                   std::chrono::steady_clock::now(),
                   command.message.message_type,
                   command.message.lane});
    }
    command = {};
    ++drained;
  }
}

NetworkResult GameNetworkingSocketsTransport::send_outbound_command(const OutboundSendCommand& command) {
  assert_owner_thread();
  if (sockets_ == nullptr) {
    return NetworkResult::failure(NetworkError{"GameNetworkingSockets interface is unavailable"});
  }

  const auto found = connections_.find(command.message.connection_id);
  if (found == connections_.end()) {
    return NetworkResult::failure(NetworkError{"unknown GameNetworkingSockets connection"});
  }

  if (command.message.payload.size() > static_cast<usize>(std::numeric_limits<int>::max())) {
    return NetworkResult::failure(NetworkError{"GameNetworkingSockets message payload is too large"});
  }

  SteamNetworkingMessage_t* message =
      SteamNetworkingUtils()->AllocateMessage(static_cast<int>(command.message.payload.size()));
  if (message == nullptr) {
    return NetworkResult::failure(NetworkError{"failed to allocate GameNetworkingSockets message"});
  }

  if (!command.message.payload.empty()) {
    std::memcpy(
        message->m_pData,
        command.message.payload.data(),
        command.message.payload.size());
  }
  message->m_conn = found->second;
  message->m_nFlags = detail::gns_send_flags(command.message.reliability);
  message->m_idxLane = detail::gns_lane_index(command.message.lane);

  SteamNetworkingMessage_t* messages[1]{message};
  int64 result_or_message_number = 0;
  sockets_->SendMessages(1, messages, &result_or_message_number, true);
  if (result_or_message_number < 0) {
    const EResult result = static_cast<EResult>(-result_or_message_number);
    if (result == k_EResultIgnored &&
        command.message.reliability != protocol::ReliabilityClass::ReliableOrdered) {
      return NetworkResult::success();
    }
    return NetworkResult::failure(
        NetworkError{send_failure_reason(result, command.message)});
  }

  return NetworkResult::success();
}

void GameNetworkingSocketsTransport::receive_poll_group_messages() {
  assert_owner_thread();
  if (sockets_ == nullptr || poll_group_ == k_HSteamNetPollGroup_Invalid) {
    return;
  }

  std::array<ISteamNetworkingMessage*, 32> messages{};
  usize total_received = 0;
  int received = 0;
  do {
    const usize remaining_budget = MaxInboundMessagesPerPoll - total_received;
    if (remaining_budget == 0U) {
      break;
    }
    const int batch_size = static_cast<int>(std::min<usize>(
        messages.size(),
        remaining_budget));
    received = sockets_->ReceiveMessagesOnPollGroup(
        poll_group_,
        messages.data(),
        batch_size);

    for (int i = 0; i < received; ++i) {
      ISteamNetworkingMessage* raw_message = messages[static_cast<usize>(i)];
      if (raw_message == nullptr) {
        continue;
      }

      const std::optional<ConnectionID> id = connection_id_for_message(*raw_message);
      if (id.has_value()) {
        std::vector<u8> payload;
        if (raw_message->m_pData != nullptr && raw_message->m_cbSize > 0) {
          const auto* bytes = static_cast<const u8*>(raw_message->m_pData);
          payload.assign(bytes, bytes + raw_message->m_cbSize);
        }
        // TODO(net): replace this copy with a PacketBuffer/message-owned payload
        // abstraction that can safely release SteamNetworkingMessage_t after processing.
        queue_event({NetworkEventType::MessageReceived,
                     id.value(),
                     (raw_message->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0
                         ? protocol::ReliabilityClass::ReliableOrdered
                         : protocol::ReliabilityClass::UnreliableEvent,
                     std::move(payload),
                     {},
                     std::chrono::steady_clock::now(),
                     protocol::MessageType::Ping,
                     static_cast<protocol::NetworkLane>(raw_message->m_idxLane)});
      }
      raw_message->Release();
    }
    total_received += static_cast<usize>(received);
  } while (received == static_cast<int>(messages.size()) &&
           total_received < MaxInboundMessagesPerPoll);
}

ConnectionID GameNetworkingSocketsTransport::connection_id_for_handle(HSteamNetConnection handle) {
  assert_owner_thread();
  const auto found = connection_ids_.find(handle);
  if (found != connection_ids_.end()) {
    return found->second;
  }

  const ConnectionID id{next_connection_id_++};
  register_connection(id, handle, ConnectionState::TransportConnected);
  return id;
}

std::optional<ConnectionID> GameNetworkingSocketsTransport::connection_id_for_message(const ISteamNetworkingMessage& message) {
  assert_owner_thread();
  if (message.m_nConnUserData > 0 &&
      message.m_nConnUserData <= static_cast<std::int64_t>(std::numeric_limits<u32>::max())) {
    return ConnectionID{static_cast<u32>(message.m_nConnUserData)};
  }

  if (message.m_conn == k_HSteamNetConnection_Invalid) {
    return std::nullopt;
  }
  return connection_id_for_handle(message.m_conn);
}

void GameNetworkingSocketsTransport::register_connection(ConnectionID id, HSteamNetConnection handle, ConnectionState state) {
  assert_owner_thread();
  connections_[id] = handle;
  connection_ids_[handle] = id;
  states_[id] = state;
  if (sockets_ != nullptr && handle != k_HSteamNetConnection_Invalid) {
    (void)sockets_->SetConnectionUserData(handle, static_cast<std::int64_t>(id.value));
    register_connection_owner(handle);
    configure_connection(handle);
  }
}

void GameNetworkingSocketsTransport::set_state(ConnectionID id, ConnectionState state) {
  assert_owner_thread();
  states_[id] = state;
}

void GameNetworkingSocketsTransport::queue_event(NetworkEvent event) {
  assert_owner_thread();
  queued_events_.emplace_back(std::move(event));
}

void GameNetworkingSocketsTransport::assert_owner_thread() const noexcept {
  const usize owner = owner_thread_hash_.load(std::memory_order_acquire);
  assert((owner == 0U || owner == current_thread_hash()) &&
         "GameNetworkingSocketsTransport state must be pumped by its owner thread");
}

void GameNetworkingSocketsTransport::claim_or_assert_owner_thread() const noexcept {
  const usize current = current_thread_hash();
  usize unclaimed = 0U;
  if (owner_thread_hash_.compare_exchange_strong(
          unclaimed,
          current,
          std::memory_order_acq_rel,
          std::memory_order_acquire)) {
    return;
  }
  assert(unclaimed == current &&
         "GameNetworkingSocketsTransport state must be pumped by its owner thread");
}

bool GameNetworkingSocketsTransport::is_owner_thread() const noexcept {
  return owner_thread_hash_.load(std::memory_order_acquire) == current_thread_hash();
}

usize GameNetworkingSocketsTransport::current_thread_hash() noexcept {
  const usize hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
  return hash == 0U ? 1U : hash;
}

std::optional<u32> GameNetworkingSocketsTransport::parse_ipv4(std::string_view value) noexcept {
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

namespace detail {

int gns_send_flags(protocol::ReliabilityClass reliability) noexcept {
  switch (reliability) {
    case protocol::ReliabilityClass::ReliableOrdered:
      return k_nSteamNetworkingSend_Reliable;
    case protocol::ReliabilityClass::ReliableImmediate:
      return k_nSteamNetworkingSend_ReliableNoNagle;
    case protocol::ReliabilityClass::UnreliableLatestWins:
      return k_nSteamNetworkingSend_UnreliableNoDelay;
    case protocol::ReliabilityClass::UnreliableEvent:
      return k_nSteamNetworkingSend_UnreliableNoDelay;
  }
  return k_nSteamNetworkingSend_Reliable;
}

u16 gns_lane_index(protocol::NetworkLane lane) noexcept {
  switch (lane) {
    case protocol::NetworkLane::Session:
      return 0;
    case protocol::NetworkLane::Gameplay:
      return 1;
    case protocol::NetworkLane::Movement:
      return 2;
    case protocol::NetworkLane::ChunkStreaming:
      return 3;
    case protocol::NetworkLane::Debug:
      return 4;
  }
  return 0;
}

} // namespace detail

} // namespace arv::net
