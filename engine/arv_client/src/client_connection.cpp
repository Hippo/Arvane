#include "client_connection.hpp"

#include "arv/core/log.hpp"

#include "client_protocol_send.hpp"

#include <utility>

namespace arv::client::detail {

ClientConnectionRuntime::ClientConnectionRuntime(std::unique_ptr<net::ITransport> transport) : transport_(std::move(transport)) {
}

net::ConnectResult ClientConnectionRuntime::connect_integrated(
    ClientProtocolSender& sender,
    const LocalPlayerIdentity& identity) {
  if (!transport_) {
    return net::ConnectResult::failure(net::NetworkError{"client has no transport"});
  }
  auto result = transport_->connect({"in-process", 0});
  if (result) {
    connection_id_ = result.value();
    connection_state_ = net::ConnectionState::TransportConnected;
    auto hello_result = sender.send_client_hello(identity);
    if (!hello_result) {
      return net::ConnectResult::failure(hello_result.error());
    }
    connection_state_ = net::ConnectionState::ProtocolHandshake;
  }
  return result;
}

net::ConnectResult ClientConnectionRuntime::connect_remote(net::NetworkAddress address) {
  if (!transport_) {
    return net::ConnectResult::failure(net::NetworkError{"client has no transport"});
  }
  auto result = transport_->connect(std::move(address));
  if (result) {
    connection_id_ = result.value();
    connection_state_ = net::ConnectionState::Connecting;
  }
  return result;
}

std::vector<net::NetworkEvent> ClientConnectionRuntime::poll_events() {
  if (!transport_) {
    return {};
  }
  return transport_->poll_events();
}

void ClientConnectionRuntime::handle_connection_accepted(
    ClientProtocolSender& sender,
    const LocalPlayerIdentity& identity) {
  if (connection_state_ != net::ConnectionState::Connecting) {
    return;
  }
  connection_state_ = net::ConnectionState::TransportConnected;
  auto hello_result = sender.send_client_hello(identity);
  if (!hello_result) {
    connection_state_ = net::ConnectionState::Disconnecting;
    return;
  }
  connection_state_ = net::ConnectionState::ProtocolHandshake;
}

void ClientConnectionRuntime::handle_connection_closed_or_failed(
    const net::NetworkEvent& event) {
  core::warn(
      "client",
      "network connection {}: {}",
      event.type == net::NetworkEventType::ConnectionClosed ? "closed" : "failed",
      event.reason);
  connection_state_ = net::ConnectionState::Disconnected;
}

void ClientConnectionRuntime::handle_message_send_failed() noexcept {
  connection_state_ = net::ConnectionState::Disconnecting;
}

net::ITransport* ClientConnectionRuntime::transport() noexcept {
  return transport_.get();
}

const net::ITransport* ClientConnectionRuntime::transport() const noexcept {
  return transport_.get();
}

net::ConnectionID ClientConnectionRuntime::connection_id() const noexcept {
  return connection_id_;
}

net::ConnectionState ClientConnectionRuntime::state() const noexcept {
  return connection_state_;
}

void ClientConnectionRuntime::set_state(net::ConnectionState state) noexcept {
  connection_state_ = state;
}

} // namespace arv::client::detail
