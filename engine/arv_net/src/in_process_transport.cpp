#include "arv/net/in_process_transport.hpp"

#include <utility>

namespace arv::net {

InProcessTransport::InProcessTransport(std::shared_ptr<detail::InProcessSharedState> shared, InProcessEndpointSide side) : shared_(std::move(shared)), side_(side) {
}

NetworkResult InProcessTransport::start_listening(const ListenAddress&) {
  if (side_ != InProcessEndpointSide::Server) {
    return NetworkResult::failure(NetworkError{"only the server endpoint can listen"});
  }

  std::lock_guard lock(shared_->server.mutex);
  shared_->server_listening = true;
  return NetworkResult::success();
}

ConnectResult InProcessTransport::connect(const NetworkAddress&) {
  if (side_ != InProcessEndpointSide::Client) {
    return ConnectResult::failure(NetworkError{"only the client endpoint can connect"});
  }

  {
    std::lock_guard server_lock(shared_->server.mutex);
    if (!shared_->server_listening) {
      return ConnectResult::failure(NetworkError{"in-process server is not listening"});
    }
    shared_->server.state = ConnectionState::TransportConnected;
    shared_->server.events.push_back({NetworkEventType::ConnectionAccepted,
                                      LocalConnectionID,
                                      protocol::ReliabilityClass::ReliableOrdered,
                                      {},
                                      {},
                                      std::chrono::steady_clock::now()});
  }

  {
    std::lock_guard client_lock(shared_->client.mutex);
    shared_->client.state = ConnectionState::TransportConnected;
    shared_->client.events.push_back({NetworkEventType::ConnectionAccepted,
                                      LocalConnectionID,
                                      protocol::ReliabilityClass::ReliableOrdered,
                                      {},
                                      {},
                                      std::chrono::steady_clock::now()});
  }

  return ConnectResult::success(LocalConnectionID);
}

NetworkResult InProcessTransport::send(TransportMessage message) {
  auto& local = local_state();
  auto& peer = peer_state();

  {
    std::lock_guard lock(local.mutex);
    if (local.state == ConnectionState::Disconnected || local.state == ConnectionState::Disconnecting) {
      return NetworkResult::failure(NetworkError{"connection is not active"});
    }
  }

  std::lock_guard peer_lock(peer.mutex);
  message.connection_id = LocalConnectionID;
  peer.events.push_back({NetworkEventType::MessageReceived,
                         message.connection_id,
                         message.reliability,
                         std::move(message.payload),
                         {},
                         std::chrono::steady_clock::now()});
  return NetworkResult::success();
}

std::vector<NetworkEvent> InProcessTransport::poll_events() {
  auto& local = local_state();
  std::lock_guard lock(local.mutex);
  std::vector<NetworkEvent> events;
  events.reserve(local.events.size());
  while (!local.events.empty()) {
    events.push_back(std::move(local.events.front()));
    local.events.pop_front();
  }
  return events;
}

ConnectionState InProcessTransport::state(ConnectionID) const {
  const auto& local = local_state();
  std::lock_guard lock(local.mutex);
  return local.state;
}

void InProcessTransport::shutdown() {
  {
    std::lock_guard lock(shared_->client.mutex);
    shared_->client.state = ConnectionState::Disconnected;
    shared_->client.events.clear();
  }
  {
    std::lock_guard lock(shared_->server.mutex);
    shared_->server.state = ConnectionState::Disconnected;
    shared_->server.events.clear();
    shared_->server_listening = false;
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

InProcessTransportPair make_in_process_transport_pair() {
  auto shared = std::make_shared<detail::InProcessSharedState>();
  return {
      std::make_unique<InProcessTransport>(shared, InProcessEndpointSide::Client),
      std::make_unique<InProcessTransport>(shared, InProcessEndpointSide::Server)};
}

} // namespace arv::net
