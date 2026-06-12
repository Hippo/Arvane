#include "arv/net/multiplex_transport.hpp"

#include <limits>
#include <string>
#include <utility>

namespace arv::net {
namespace {

[[nodiscard]] std::string transport_name(
    const MultiplexTransportEntry& entry,
    usize index) {
  return entry.name.empty() ? "transport-" + std::to_string(index) : entry.name;
}

[[nodiscard]] u64 child_key(usize transport_index, ConnectionID local_id) noexcept {
  return (static_cast<u64>(transport_index + 1U) << 32U) |
         static_cast<u64>(local_id.value);
}

[[nodiscard]] NetworkError prefixed_error(
    const MultiplexTransportEntry& entry,
    usize index,
    std::string_view action,
    std::string_view reason) {
  return NetworkError{
      "transport '" + transport_name(entry, index) +
      "' failed to " + std::string{action} + ": " + std::string{reason}};
}

} // namespace

MultiplexTransport::MultiplexTransport(
    std::vector<MultiplexTransportEntry> transports)
    : transports_(std::move(transports)) {
}

void MultiplexTransport::add_transport(MultiplexTransportEntry entry) {
  const std::lock_guard lock{mutex_};
  transports_.push_back(std::move(entry));
}

usize MultiplexTransport::transport_count() const noexcept {
  const std::lock_guard lock{mutex_};
  return transports_.size();
}

NetworkResult MultiplexTransport::start_listening(const ListenAddress& address) {
  const std::lock_guard lock{mutex_};
  if (transports_.empty()) {
    return NetworkResult::failure(NetworkError{"multiplex transport has no child transports"});
  }

  usize started = 0U;
  for (usize index = 0; index < transports_.size(); ++index) {
    MultiplexTransportEntry& entry = transports_[index];
    if (entry.transport == nullptr) {
      for (usize rollback = 0; rollback < started; ++rollback) {
        transports_[rollback].transport->shutdown();
      }
      return NetworkResult::failure(
          NetworkError{"multiplex transport child '" +
                       transport_name(entry, index) + "' is null"});
    }

    const ListenAddress& child_address =
        entry.listen_address.has_value() ? entry.listen_address.value() : address;
    NetworkResult result = entry.transport->start_listening(child_address);
    if (!result) {
      for (usize rollback = 0; rollback < started; ++rollback) {
        transports_[rollback].transport->shutdown();
      }
      return NetworkResult::failure(
          prefixed_error(entry, index, "listen", result.error().message));
    }
    ++started;
  }

  return NetworkResult::success();
}

ConnectResult MultiplexTransport::connect(const NetworkAddress& address) {
  const std::lock_guard lock{mutex_};
  if (transports_.empty()) {
    return ConnectResult::failure(NetworkError{"multiplex transport has no child transports"});
  }

  MultiplexTransportEntry& entry = transports_.front();
  if (entry.transport == nullptr) {
    return ConnectResult::failure(NetworkError{"multiplex transport child '" +
                                               transport_name(entry, 0U) + "' is null"});
  }

  ConnectResult result = entry.transport->connect(address);
  if (!result) {
    return ConnectResult::failure(
        prefixed_error(entry, 0U, "connect", result.error().message));
  }
  return ConnectResult::success(public_id_for(0U, result.value()));
}

NetworkResult MultiplexTransport::send(TransportMessage message) {
  const std::lock_guard lock{mutex_};
  std::optional<Route> route = route_for(message.connection_id);
  if (!route.has_value()) {
    return NetworkResult::failure(NetworkError{"unknown multiplex connection"});
  }
  if (route->transport_index >= transports_.size() ||
      transports_[route->transport_index].transport == nullptr) {
    return NetworkResult::failure(NetworkError{"multiplex connection route is invalid"});
  }

  message.connection_id = route->local_id;
  return transports_[route->transport_index].transport->send(std::move(message));
}

NetworkResult MultiplexTransport::close_connection(
    ConnectionID connection_id,
    std::string_view reason) {
  const std::lock_guard lock{mutex_};
  std::optional<Route> route = route_for(connection_id);
  if (!route.has_value()) {
    return NetworkResult::success();
  }
  if (route->transport_index >= transports_.size() ||
      transports_[route->transport_index].transport == nullptr) {
    remove_route(connection_id);
    return NetworkResult::failure(NetworkError{"multiplex connection route is invalid"});
  }

  NetworkResult result =
      transports_[route->transport_index].transport->close_connection(
          route->local_id,
          reason);
  if (result) {
    remove_route(connection_id);
  }
  return result;
}

std::vector<NetworkEvent> MultiplexTransport::poll_events() {
  const std::lock_guard lock{mutex_};
  std::vector<NetworkEvent> translated;
  for (usize index = 0; index < transports_.size(); ++index) {
    MultiplexTransportEntry& entry = transports_[index];
    if (entry.transport == nullptr) {
      continue;
    }

    std::vector<NetworkEvent> child_events = entry.transport->poll_events();
    translated.reserve(translated.size() + child_events.size());
    for (NetworkEvent& event : child_events) {
      const ConnectionID public_id = public_id_for(index, event.connection_id);
      event.connection_id = public_id;
      translated.emplace_back(std::move(event));
      const NetworkEventType type = translated.back().type;
      if (type == NetworkEventType::ConnectionClosed ||
          type == NetworkEventType::ConnectionFailed) {
        remove_route(public_id);
      }
    }
  }
  return translated;
}

ConnectionState MultiplexTransport::state(ConnectionID connection_id) const {
  const std::lock_guard lock{mutex_};
  std::optional<Route> route = route_for(connection_id);
  if (!route.has_value() || route->transport_index >= transports_.size() ||
      transports_[route->transport_index].transport == nullptr) {
    return ConnectionState::Disconnected;
  }
  return transports_[route->transport_index].transport->state(route->local_id);
}

void MultiplexTransport::shutdown() {
  const std::lock_guard lock{mutex_};
  for (MultiplexTransportEntry& entry : transports_) {
    if (entry.transport != nullptr) {
      entry.transport->shutdown();
    }
  }
  routes_.clear();
  public_ids_by_child_.clear();
}

ConnectionID MultiplexTransport::public_id_for(
    usize transport_index,
    ConnectionID local_id) {
  if (local_id.value == 0U) {
    return {};
  }

  const u64 key = child_key(transport_index, local_id);
  if (const auto found = public_ids_by_child_.find(key);
      found != public_ids_by_child_.end()) {
    return found->second;
  }

  if (next_connection_id_ == std::numeric_limits<u32>::max()) {
    next_connection_id_ = 1U;
  }
  ConnectionID public_id{next_connection_id_++};
  while (routes_.find(public_id) != routes_.end()) {
    public_id = ConnectionID{next_connection_id_++};
  }
  routes_[public_id] = Route{transport_index, local_id};
  public_ids_by_child_[key] = public_id;
  return public_id;
}

std::optional<MultiplexTransport::Route> MultiplexTransport::route_for(
    ConnectionID public_id) const {
  const auto found = routes_.find(public_id);
  if (found == routes_.end()) {
    return std::nullopt;
  }
  return found->second;
}

void MultiplexTransport::remove_route(ConnectionID public_id) {
  const auto found = routes_.find(public_id);
  if (found == routes_.end()) {
    return;
  }
  public_ids_by_child_.erase(child_key(found->second.transport_index, found->second.local_id));
  routes_.erase(found);
}

} // namespace arv::net
