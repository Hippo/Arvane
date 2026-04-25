#pragma once

#include "arv/net/transport.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <utility>

namespace arv::net {

enum class InProcessEndpointSide {
  Client,
  Server,
};

namespace detail {

struct InProcessEndpointState {
  mutable std::mutex mutex;
  std::deque<NetworkEvent> events;
  ConnectionState state{ConnectionState::Disconnected};
};

struct InProcessSharedState {
  InProcessEndpointState client;
  InProcessEndpointState server;
  bool server_listening{};
};

} // namespace detail

class InProcessTransport final : public ITransport {
public:
  InProcessTransport(std::shared_ptr<detail::InProcessSharedState> shared, InProcessEndpointSide side)
      : shared_(std::move(shared)), side_(side) {}

  NetworkResult start_listening(const ListenAddress&) override {
    if (side_ != InProcessEndpointSide::Server) {
      return NetworkResult::failure(NetworkError{"only the server endpoint can listen"});
    }

    std::lock_guard lock(shared_->server.mutex);
    shared_->server_listening = true;
    return NetworkResult::success();
  }

  ConnectResult connect(const NetworkAddress&) override {
    if (side_ != InProcessEndpointSide::Client) {
      return ConnectResult::failure(NetworkError{"only the client endpoint can connect"});
    }

    {
      std::lock_guard server_lock(shared_->server.mutex);
      if (!shared_->server_listening) {
        return ConnectResult::failure(NetworkError{"in-process server is not listening"});
      }
      shared_->server.state = ConnectionState::TransportConnected;
      shared_->server.events.push_back({
          NetworkEventType::ConnectionAccepted,
          LocalConnectionID,
          protocol::ReliabilityClass::ReliableOrdered,
          {},
          {},
          std::chrono::steady_clock::now()});
    }

    {
      std::lock_guard client_lock(shared_->client.mutex);
      shared_->client.state = ConnectionState::TransportConnected;
      shared_->client.events.push_back({
          NetworkEventType::ConnectionAccepted,
          LocalConnectionID,
          protocol::ReliabilityClass::ReliableOrdered,
          {},
          {},
          std::chrono::steady_clock::now()});
    }

    return ConnectResult::success(LocalConnectionID);
  }

  NetworkResult send(TransportMessage message) override {
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
    peer.events.push_back({
        NetworkEventType::MessageReceived,
        message.connection_id,
        message.reliability,
        std::move(message.payload),
        {},
        std::chrono::steady_clock::now()});
    return NetworkResult::success();
  }

  std::vector<NetworkEvent> poll_events() override {
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

  ConnectionState state(ConnectionID) const override {
    const auto& local = local_state();
    std::lock_guard lock(local.mutex);
    return local.state;
  }

  void shutdown() override {
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

private:
  static inline const ConnectionID LocalConnectionID{1};

  detail::InProcessEndpointState& local_state() {
    return side_ == InProcessEndpointSide::Client ? shared_->client : shared_->server;
  }

  const detail::InProcessEndpointState& local_state() const {
    return side_ == InProcessEndpointSide::Client ? shared_->client : shared_->server;
  }

  detail::InProcessEndpointState& peer_state() {
    return side_ == InProcessEndpointSide::Client ? shared_->server : shared_->client;
  }

  std::shared_ptr<detail::InProcessSharedState> shared_;
  InProcessEndpointSide side_{InProcessEndpointSide::Client};
};

struct InProcessTransportPair {
  std::unique_ptr<ITransport> client;
  std::unique_ptr<ITransport> server;
};

inline InProcessTransportPair make_in_process_transport_pair() {
  auto shared = std::make_shared<detail::InProcessSharedState>();
  return {
      std::make_unique<InProcessTransport>(shared, InProcessEndpointSide::Client),
      std::make_unique<InProcessTransport>(shared, InProcessEndpointSide::Server)};
}

} // namespace arv::net
