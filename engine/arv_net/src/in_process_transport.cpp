#include "arv/net/in_process_transport.hpp"

#include <chrono>
#include <utility>

namespace arv::net {

namespace detail {

InProcessSharedState::InProcessSharedState(usize event_queue_capacity) : client_to_server_events(event_queue_capacity),
                                                                         server_to_client_events(event_queue_capacity) {
}

} // namespace detail

InProcessTransport::InProcessTransport(std::shared_ptr<detail::InProcessSharedState> shared, InProcessEndpointSide side) : shared_(std::move(shared)), side_(side) {
}

NetworkResult InProcessTransport::start_listening(const ListenAddress&) {
  if (side_ != InProcessEndpointSide::Server) {
    return NetworkResult::failure(NetworkError{"only the server endpoint can listen"});
  }

  shared_->server_listening.store(true, std::memory_order_release);
  return NetworkResult::success();
}

ConnectResult InProcessTransport::connect(const NetworkAddress&) {
  if (side_ != InProcessEndpointSide::Client) {
    return ConnectResult::failure(NetworkError{"only the client endpoint can connect"});
  }

  if (!shared_->server_listening.load(std::memory_order_acquire)) {
    return ConnectResult::failure(NetworkError{"in-process server is not listening"});
  }

  shared_->client.state.store(ConnectionState::TransportConnected, std::memory_order_release);
  shared_->server.state.store(ConnectionState::TransportConnected, std::memory_order_release);

  NetworkEvent accepted{NetworkEventType::ConnectionAccepted,
                        LocalConnectionID,
                        protocol::ReliabilityClass::ReliableOrdered,
                        {},
                        {},
                        std::chrono::steady_clock::now()};
  if (!shared_->client_to_server_events.try_enqueue(std::move(accepted))) {
    shared_->client.state.store(ConnectionState::Disconnected, std::memory_order_release);
    shared_->server.state.store(ConnectionState::Disconnected, std::memory_order_release);
    return ConnectResult::failure(NetworkError{"in-process server event queue is full"});
  }
  return ConnectResult::success(LocalConnectionID);
}

NetworkResult InProcessTransport::send(TransportMessage message) {
  if (!connection_is_active(local_state().state.load(std::memory_order_acquire)) ||
      !connection_is_active(peer_state().state.load(std::memory_order_acquire))) {
    return NetworkResult::failure(NetworkError{"connection is not active"});
  }

  message.connection_id = LocalConnectionID;
  NetworkEvent received{NetworkEventType::MessageReceived,
                        message.connection_id,
                        message.reliability,
                        std::move(message.payload),
                        {},
                        std::chrono::steady_clock::now()};
  received.message_type = message.message_type;
  received.lane = message.lane;
  if (!outbound_events().try_enqueue(std::move(received))) {
    return NetworkResult::failure(NetworkError{"in-process peer event queue is full"});
  }
  return NetworkResult::success();
}

std::vector<NetworkEvent> InProcessTransport::poll_events() {
  std::vector<NetworkEvent> events;
  events.reserve(8U);
  NetworkEvent event;
  while (inbound_events().try_dequeue(event)) {
    if (side_ == InProcessEndpointSide::Server &&
        event.type == NetworkEventType::ConnectionAccepted) {
      NetworkEvent client_accepted{NetworkEventType::ConnectionAccepted,
                                   LocalConnectionID,
                                   protocol::ReliabilityClass::ReliableOrdered,
                                   {},
                                   {},
                                   std::chrono::steady_clock::now()};
      (void)shared_->server_to_client_events.try_enqueue(std::move(client_accepted));
    }
    events.emplace_back(std::move(event));
    event = {};
  }
  return events;
}

ConnectionState InProcessTransport::state(ConnectionID) const {
  return local_state().state.load(std::memory_order_acquire);
}

void InProcessTransport::shutdown() {
  const ConnectionState previous =
      local_state().state.exchange(ConnectionState::Disconnected, std::memory_order_acq_rel);
  peer_state().state.store(ConnectionState::Disconnected, std::memory_order_release);

  if (side_ == InProcessEndpointSide::Server) {
    shared_->server_listening.store(false, std::memory_order_release);
  }

  NetworkEvent discarded;
  while (inbound_events().try_dequeue(discarded)) {
    discarded = {};
  }

  if (connection_is_active(previous)) {
    NetworkEvent closed{NetworkEventType::ConnectionClosed,
                        LocalConnectionID,
                        protocol::ReliabilityClass::ReliableOrdered,
                        {},
                        {},
                        std::chrono::steady_clock::now()};
    (void)outbound_events().try_enqueue(std::move(closed));
  }
}

detail::InProcessEndpointState& InProcessTransport::local_state() {
  return side_ == InProcessEndpointSide::Client ? shared_->client : shared_->server;
}

const detail::InProcessEndpointState& InProcessTransport::local_state() const {
  return side_ == InProcessEndpointSide::Client ? shared_->client : shared_->server;
}

detail::InProcessEndpointState& InProcessTransport::peer_state() {
  return side_ == InProcessEndpointSide::Client ? shared_->server : shared_->client;
}

const detail::InProcessEndpointState& InProcessTransport::peer_state() const {
  return side_ == InProcessEndpointSide::Client ? shared_->server : shared_->client;
}

moodycamel::ReaderWriterQueue<NetworkEvent>& InProcessTransport::inbound_events() {
  return side_ == InProcessEndpointSide::Client ? shared_->server_to_client_events
                                                : shared_->client_to_server_events;
}

moodycamel::ReaderWriterQueue<NetworkEvent>& InProcessTransport::outbound_events() {
  return side_ == InProcessEndpointSide::Client ? shared_->client_to_server_events
                                                : shared_->server_to_client_events;
}

bool InProcessTransport::connection_is_active(ConnectionState state) noexcept {
  return state != ConnectionState::Disconnected && state != ConnectionState::Disconnecting;
}

InProcessTransportPair make_in_process_transport_pair(usize event_queue_capacity) {
  auto shared = std::make_shared<detail::InProcessSharedState>(event_queue_capacity);
  return {
      std::make_unique<InProcessTransport>(shared, InProcessEndpointSide::Client),
      std::make_unique<InProcessTransport>(shared, InProcessEndpointSide::Server)};
}

} // namespace arv::net
