#pragma once

#include "arv/client/local_identity.hpp"
#include "arv/net/transport.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::lang {
class LanguageCatalog;
} // namespace arv::lang

namespace arv::client::detail {

class ClientProtocolSender;

class ClientConnectionRuntime {
public:
  explicit ClientConnectionRuntime(std::unique_ptr<net::ITransport> transport);

  net::ConnectResult connect_integrated(
      ClientProtocolSender& sender,
      const LocalPlayerIdentity& identity);
  net::ConnectResult connect_remote(net::NetworkAddress address);
  void configure_timeouts(u32 server_keep_alive_timeout_seconds, u32 connection_timeout_seconds) noexcept;
  void configure_language(const lang::LanguageCatalog* language) noexcept;

  std::vector<net::NetworkEvent> poll_events();

  void handle_connection_accepted(
      ClientProtocolSender& sender,
      const LocalPlayerIdentity& identity);
  void handle_connection_closed_or_failed(const net::NetworkEvent& event);
  void handle_message_send_failed() noexcept;
  void disconnect_local(std::string_view reason);
  void record_server_keep_alive() noexcept;
  [[nodiscard]] bool tick_timeouts();
  [[nodiscard]] std::optional<std::string> take_disconnect_reason();
  [[nodiscard]] std::optional<std::string> disconnect_reason() const;
  void clear_disconnect_reason() noexcept;

  [[nodiscard]] net::ITransport* transport() noexcept;
  [[nodiscard]] const net::ITransport* transport() const noexcept;

  [[nodiscard]] net::ConnectionID connection_id() const noexcept;
  [[nodiscard]] net::ConnectionState state() const noexcept;
  void set_state(net::ConnectionState state) noexcept;

private:
  std::unique_ptr<net::ITransport> transport_;
  net::ConnectionID connection_id_{};
  net::ConnectionState connection_state_{net::ConnectionState::Disconnected};
  u32 server_keep_alive_timeout_seconds_{30U};
  u32 connection_timeout_seconds_{10U};
  std::optional<std::string> disconnect_reason_{};
  const lang::LanguageCatalog* language_{};
  std::chrono::steady_clock::time_point connection_attempt_started_time_{};
  std::chrono::steady_clock::time_point last_server_keep_alive_time_{};
};

} // namespace arv::client::detail
