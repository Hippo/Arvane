#pragma once

#include "arv/core/completion_queue.hpp"
#include "arv/net/transport.hpp"

#include <atomic>
#include <memory>

namespace arv::net {

inline constexpr usize DefaultInProcessEventQueueCapacity = 1024U;

enum class InProcessEndpointSide {
  Client,
  Server,
};

namespace detail {

struct InProcessEndpointState {
  std::atomic<ConnectionState> state{ConnectionState::Disconnected};
};

struct InProcessSharedState {
  explicit InProcessSharedState(usize event_queue_capacity = DefaultInProcessEventQueueCapacity);

  InProcessEndpointState client;
  InProcessEndpointState server;
  std::atomic_bool server_listening{};
  core::SpscQueue<NetworkEvent> client_to_server_events;
  core::SpscQueue<NetworkEvent> server_to_client_events;
};

} // namespace detail

class InProcessTransport final : public ITransport {
public:
  InProcessTransport(std::shared_ptr<detail::InProcessSharedState> shared, InProcessEndpointSide side);
  NetworkResult start_listening(const ListenAddress&) override;
  ConnectResult connect(const NetworkAddress&) override;
  NetworkResult send(TransportMessage message) override;
  NetworkResult close_connection(ConnectionID, std::string_view reason = {}) override;
  std::vector<NetworkEvent> poll_events() override;
  ConnectionState state(ConnectionID) const override;
  void shutdown() override;

private:
  static inline const ConnectionID LocalConnectionID{1};
  detail::InProcessEndpointState& local_state();
  const detail::InProcessEndpointState& local_state() const;
  detail::InProcessEndpointState& peer_state();
  const detail::InProcessEndpointState& peer_state() const;
  core::SpscQueue<NetworkEvent>& inbound_events();
  core::SpscQueue<NetworkEvent>& outbound_events();
  [[nodiscard]] static bool connection_is_active(ConnectionState state) noexcept;

  std::shared_ptr<detail::InProcessSharedState> shared_;
  InProcessEndpointSide side_{InProcessEndpointSide::Client};
};

struct InProcessTransportPair {
  std::unique_ptr<ITransport> client;
  std::unique_ptr<ITransport> server;
};

InProcessTransportPair make_in_process_transport_pair(
    usize event_queue_capacity = DefaultInProcessEventQueueCapacity);

} // namespace arv::net
