#pragma once

#include "arv/net/transport.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arv::net {

struct MultiplexTransportEntry {
  std::string name{};
  std::unique_ptr<ITransport> transport{};
  std::optional<ListenAddress> listen_address{};
};

class MultiplexTransport final : public ITransport {
public:
  MultiplexTransport() = default;
  explicit MultiplexTransport(std::vector<MultiplexTransportEntry> transports);

  MultiplexTransport(const MultiplexTransport&) = delete;
  MultiplexTransport& operator=(const MultiplexTransport&) = delete;

  void add_transport(MultiplexTransportEntry entry);
  [[nodiscard]] usize transport_count() const noexcept;

  NetworkResult start_listening(const ListenAddress& address) override;
  ConnectResult connect(const NetworkAddress& address) override;
  NetworkResult send(TransportMessage message) override;
  NetworkResult close_connection(
      ConnectionID connection_id,
      std::string_view reason = {}) override;
  std::vector<NetworkEvent> poll_events() override;
  ConnectionState state(ConnectionID connection_id) const override;
  void shutdown() override;

private:
  struct Route {
    usize transport_index{};
    ConnectionID local_id{};
  };

  [[nodiscard]] ConnectionID public_id_for(usize transport_index, ConnectionID local_id);
  [[nodiscard]] std::optional<Route> route_for(ConnectionID public_id) const;
  void remove_route(ConnectionID public_id);

  std::vector<MultiplexTransportEntry> transports_{};
  std::unordered_map<ConnectionID, Route> routes_{};
  std::unordered_map<u64, ConnectionID> public_ids_by_child_{};
  mutable std::mutex mutex_{};
  u32 next_connection_id_{1U};
};

} // namespace arv::net
