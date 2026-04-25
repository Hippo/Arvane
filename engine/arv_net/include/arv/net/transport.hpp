#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/net/connection_state.hpp"
#include "arv/protocol/message_type.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace arv::net {

struct NetworkError {
  std::string message{};
};

struct NetworkAddress {
  std::string host{"127.0.0.1"};
  u16 port{27020};
};

struct ListenAddress {
  std::string bind_address{"0.0.0.0"};
  u16 port{27020};
};

struct TransportMessage {
  ConnectionID connection_id{};
  protocol::ReliabilityClass reliability{protocol::ReliabilityClass::ReliableOrdered};
  std::vector<u8> payload{};
};

enum class NetworkEventType {
  ConnectionRequested,
  ConnectionAccepted,
  ConnectionClosed,
  ConnectionFailed,
  MessageReceived,
  MessageSendFailed,
};

struct NetworkEvent {
  NetworkEventType type{NetworkEventType::MessageReceived};
  ConnectionID connection_id{};
  protocol::ReliabilityClass reliability{protocol::ReliabilityClass::ReliableOrdered};
  std::vector<u8> payload{};
  std::string reason{};
  std::chrono::steady_clock::time_point receive_time{};
};

using NetworkResult = Expected<void, NetworkError>;
using ConnectResult = Expected<ConnectionID, NetworkError>;

class ITransport {
public:
  virtual ~ITransport() = default;

  virtual NetworkResult start_listening(const ListenAddress& address) = 0;
  virtual ConnectResult connect(const NetworkAddress& address) = 0;
  virtual NetworkResult send(TransportMessage message) = 0;
  virtual std::vector<NetworkEvent> poll_events() = 0;
  virtual std::vector<TransportMessage> poll() {
    std::vector<TransportMessage> messages;
    for (NetworkEvent& event : poll_events()) {
      if (event.type != NetworkEventType::MessageReceived) {
        continue;
      }
      messages.push_back({
          event.connection_id,
          event.reliability,
          std::move(event.payload)});
    }
    return messages;
  }
  virtual ConnectionState state(ConnectionID connection_id) const = 0;
  virtual void shutdown() = 0;
};

} // namespace arv::net
