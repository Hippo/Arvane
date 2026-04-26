#pragma once

#include "arv/net/transport.hpp"

#include <deque>
#include <memory>
#include <mutex>

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
  InProcessTransport(std::shared_ptr<detail::InProcessSharedState> shared, InProcessEndpointSide side);
  NetworkResult start_listening(const ListenAddress&) override;
  ConnectResult connect(const NetworkAddress&) override;
  NetworkResult send(TransportMessage message) override;
  std::vector<NetworkEvent> poll_events() override;
  ConnectionState state(ConnectionID) const override;
  void shutdown() override;

private:
  static inline const ConnectionID LocalConnectionID{1};
  detail::InProcessEndpointState& local_state();
  const detail::InProcessEndpointState& local_state() const;
  detail::InProcessEndpointState& peer_state();

  std::shared_ptr<detail::InProcessSharedState> shared_;
  InProcessEndpointSide side_{InProcessEndpointSide::Client};
};

struct InProcessTransportPair {
  std::unique_ptr<ITransport> client;
  std::unique_ptr<ITransport> server;
};

InProcessTransportPair make_in_process_transport_pair();

} // namespace arv::net
