#include "client_connection.hpp"

#include "arv/core/log.hpp"
#include "arv/lang/language.hpp"

#include "client_protocol_send.hpp"

#include <chrono>
#include <utility>

namespace arv::client::detail {
namespace {

[[nodiscard]] bool is_connection_attempt_state(net::ConnectionState state) noexcept {
  return state == net::ConnectionState::Connecting ||
         state == net::ConnectionState::TransportConnected ||
         state == net::ConnectionState::ProtocolHandshake;
}

[[nodiscard]] bool expects_server_keep_alive(net::ConnectionState state) noexcept {
  return state == net::ConnectionState::WorldJoin ||
         state == net::ConnectionState::InGame;
}

[[nodiscard]] std::string tr(
    const lang::LanguageCatalog* language,
    std::string_view key,
    std::string_view fallback) {
  return language == nullptr ? std::string{fallback} : language->text(key, fallback);
}

} // namespace

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
    disconnect_reason_.reset();
    connection_attempt_started_time_ = std::chrono::steady_clock::now();
    set_state(net::ConnectionState::TransportConnected);
    auto hello_result = sender.send_client_hello(identity);
    if (!hello_result) {
      return net::ConnectResult::failure(hello_result.error());
    }
    set_state(net::ConnectionState::ProtocolHandshake);
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
    disconnect_reason_.reset();
    connection_attempt_started_time_ = std::chrono::steady_clock::now();
    set_state(net::ConnectionState::Connecting);
  }
  return result;
}

void ClientConnectionRuntime::configure_timeouts(
    u32 server_keep_alive_timeout_seconds,
    u32 connection_timeout_seconds) noexcept {
  server_keep_alive_timeout_seconds_ =
      server_keep_alive_timeout_seconds == 0U ? 30U : server_keep_alive_timeout_seconds;
  connection_timeout_seconds_ =
      connection_timeout_seconds == 0U ? 10U : connection_timeout_seconds;
}

void ClientConnectionRuntime::configure_language(const lang::LanguageCatalog* language) noexcept {
  language_ = language;
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
  set_state(net::ConnectionState::TransportConnected);
    auto hello_result = sender.send_client_hello(identity);
    if (!hello_result) {
      disconnect_local(tr(
          language_,
          "ui.connection.server_offline",
          "Server is offline or unreachable."));
      return;
    }
  set_state(net::ConnectionState::ProtocolHandshake);
}

void ClientConnectionRuntime::handle_connection_closed_or_failed(
    const net::NetworkEvent& event) {
  core::warn(
      "client",
      "network connection {}: {}",
      event.type == net::NetworkEventType::ConnectionClosed ? "closed" : "failed",
      event.reason);
  if (!disconnect_reason_.has_value()) {
    disconnect_reason_ =
        is_connection_attempt_state(connection_state_) ||
                connection_state_ == net::ConnectionState::RegistrySync
            ? tr(
                  language_,
                  "ui.connection.server_offline",
                  "Server is offline or unreachable.")
            : tr(language_, "ui.connection.lost", "Lost connection to the server.");
  }
  set_state(net::ConnectionState::Disconnected);
}

void ClientConnectionRuntime::handle_message_send_failed() noexcept {
  if (!disconnect_reason_.has_value()) {
    disconnect_reason_ = tr(language_, "ui.connection.lost", "Lost connection to the server.");
  }
  if (transport_ && connection_state_ != net::ConnectionState::Disconnected) {
    (void)transport_->close_connection(connection_id_, disconnect_reason_.value());
  }
  set_state(net::ConnectionState::Disconnected);
}

void ClientConnectionRuntime::disconnect_local(std::string_view reason) {
  if (!disconnect_reason_.has_value()) {
    disconnect_reason_ =
        reason.empty() ? tr(language_, "ui.connection.disconnected", "Disconnected.")
                       : std::string{reason};
  }
  if (transport_ && connection_state_ != net::ConnectionState::Disconnected) {
    (void)transport_->close_connection(connection_id_, disconnect_reason_.value());
  }
  set_state(net::ConnectionState::Disconnected);
}

void ClientConnectionRuntime::record_server_keep_alive() noexcept {
  last_server_keep_alive_time_ = std::chrono::steady_clock::now();
}

bool ClientConnectionRuntime::tick_timeouts() {
  const auto now = std::chrono::steady_clock::now();
  if (is_connection_attempt_state(connection_state_) &&
      connection_attempt_started_time_.time_since_epoch().count() != 0 &&
      now - connection_attempt_started_time_ >
          std::chrono::seconds(connection_timeout_seconds_)) {
    disconnect_local(tr(
        language_,
        "ui.connection.server_offline",
        "Server is offline or unreachable."));
    return true;
  }
  if (expects_server_keep_alive(connection_state_) &&
      last_server_keep_alive_time_.time_since_epoch().count() != 0 &&
      now - last_server_keep_alive_time_ >
          std::chrono::seconds(server_keep_alive_timeout_seconds_)) {
    disconnect_local(tr(language_, "ui.connection.lost", "Lost connection to the server."));
    return true;
  }
  return false;
}

std::optional<std::string> ClientConnectionRuntime::take_disconnect_reason() {
  std::optional<std::string> reason = std::move(disconnect_reason_);
  disconnect_reason_.reset();
  return reason;
}

std::optional<std::string> ClientConnectionRuntime::disconnect_reason() const {
  return disconnect_reason_;
}

void ClientConnectionRuntime::clear_disconnect_reason() noexcept {
  disconnect_reason_.reset();
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
  if (expects_server_keep_alive(state)) {
    last_server_keep_alive_time_ = std::chrono::steady_clock::now();
  }
}

} // namespace arv::client::detail
