#pragma once

#include "arv/net/transport.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace arv::net {

struct WebSocketServerTransportOptions {
  std::string bind_address{"0.0.0.0"};
  u16 port{42070};
  std::string path{"/arvane"};
  u32 max_frame_bytes{1024U * 1024U};
  u32 max_pending_bytes{4U * 1024U * 1024U};
};

namespace detail {

[[nodiscard]] std::string websocket_accept_key(std::string_view client_key);

} // namespace detail

class WebSocketServerTransport final : public ITransport {
public:
  WebSocketServerTransport();
  explicit WebSocketServerTransport(WebSocketServerTransportOptions options);
  ~WebSocketServerTransport() override;

  WebSocketServerTransport(const WebSocketServerTransport&) = delete;
  WebSocketServerTransport& operator=(const WebSocketServerTransport&) = delete;
  WebSocketServerTransport(WebSocketServerTransport&&) noexcept;
  WebSocketServerTransport& operator=(WebSocketServerTransport&&) noexcept;

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
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace arv::net
