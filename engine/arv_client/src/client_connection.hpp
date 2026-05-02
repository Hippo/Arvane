#pragma once

#include "arv/net/transport.hpp"

#include <memory>
#include <vector>

namespace arv::client::detail {

class ClientProtocolSender;

class ClientConnectionRuntime {
public:
  explicit ClientConnectionRuntime(std::unique_ptr<net::ITransport> transport);

  net::ConnectResult connect_integrated(ClientProtocolSender& sender);
  net::ConnectResult connect_remote(net::NetworkAddress address);

  std::vector<net::NetworkEvent> poll_events();

  void handle_connection_accepted(ClientProtocolSender& sender);
  void handle_connection_closed_or_failed(const net::NetworkEvent& event);
  void handle_message_send_failed() noexcept;

  [[nodiscard]] net::ITransport* transport() noexcept;
  [[nodiscard]] const net::ITransport* transport() const noexcept;

  [[nodiscard]] net::ConnectionID connection_id() const noexcept;
  [[nodiscard]] net::ConnectionState state() const noexcept;
  void set_state(net::ConnectionState state) noexcept;

private:
  std::unique_ptr<net::ITransport> transport_;
  net::ConnectionID connection_id_{};
  net::ConnectionState connection_state_{net::ConnectionState::Disconnected};
};

} // namespace arv::client::detail
