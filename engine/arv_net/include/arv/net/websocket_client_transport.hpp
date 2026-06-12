#pragma once

#include "arv/net/transport.hpp"
#include "arv/protocol/message_envelope.hpp"

#include <memory>

namespace arv::net {

struct WebSocketClientTransportOptions {
  usize max_frame_bytes{protocol::MaxPacketFrameBytes};
  usize max_pending_bytes{4U * 1024U * 1024U};
};

#ifdef __EMSCRIPTEN__

class WebSocketClientTransport final : public ITransport {
public:
  explicit WebSocketClientTransport(WebSocketClientTransportOptions options = {});
  ~WebSocketClientTransport() override;

  WebSocketClientTransport(const WebSocketClientTransport&) = delete;
  WebSocketClientTransport& operator=(const WebSocketClientTransport&) = delete;

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

#endif

} // namespace arv::net
