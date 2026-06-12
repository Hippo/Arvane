#include "arv/net/websocket_client_transport.hpp"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace arv::net {
namespace {

inline constexpr ConnectionID WebSocketConnectionID{1U};

EM_JS(int, arv_ws_create, (const char* url_ptr), {
  const url = UTF8ToString(url_ptr);
  const root = Module.arvWebSocketBridge ||
      (Module.arvWebSocketBridge = { nextId: 1, sockets: {} });
  const id = root.nextId++;
  const record = { socket: null, events: [] };
  root.sockets[id] = record;
  try {
    const socket = new WebSocket(url);
    socket.binaryType = 'arraybuffer';
    record.socket = socket;
    socket.onopen = () => {
      record.events.push({ type: 1 });
    };
    socket.onerror = () => {
      record.events.push({ type: 4, reason: 'browser WebSocket error' });
    };
    socket.onclose = (event) => {
      const reason = event && event.reason
        ? event.reason
        : `browser WebSocket closed with code ${event ? event.code : 1006}`;
      record.events.push({ type: 3, reason });
    };
    socket.onmessage = (event) => {
      if (typeof event.data === 'string') {
        record.events.push({ type: 2, text: true, data: new Uint8Array(0) });
        return;
      }
      const view = new Uint8Array(event.data);
      const copy = new Uint8Array(view.byteLength);
      copy.set(view);
      record.events.push({ type: 2, text: false, data: copy });
    };
  } catch (error) {
    record.events.push({ type: 4, reason: String(error) });
  }
  return id;
});

EM_JS(void, arv_ws_delete, (int id), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record) {
    return;
  }
  try {
    if (record.socket &&
        record.socket.readyState !== WebSocket.CLOSED &&
        record.socket.readyState !== WebSocket.CLOSING) {
      record.socket.close(1000, 'Arvane shutdown');
    }
  } catch (_) {
  }
  delete root.sockets[id];
});

EM_JS(void, arv_ws_close, (int id, int code, const char* reason_ptr), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || !record.socket) {
    return;
  }
  const reason = UTF8ToString(reason_ptr);
  try {
    record.socket.close(code, reason);
  } catch (_) {
  }
});

EM_JS(int, arv_ws_send, (int id, const unsigned char* data, int size), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || !record.socket ||
      record.socket.readyState !== WebSocket.OPEN) {
    return 0;
  }
  try {
    const copy = new Uint8Array(size);
    copy.set(HEAPU8.subarray(data, data + size));
    record.socket.send(copy);
    return 1;
  } catch (_) {
    return 0;
  }
});

EM_JS(int, arv_ws_buffered_amount, (int id), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || !record.socket) {
    return -1;
  }
  return record.socket.bufferedAmount | 0;
});

EM_JS(int, arv_ws_event_type, (int id), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || record.events.length === 0) {
    return 0;
  }
  return record.events[0].type | 0;
});

EM_JS(int, arv_ws_event_is_text, (int id), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || record.events.length === 0) {
    return 0;
  }
  return record.events[0].text ? 1 : 0;
});

EM_JS(int, arv_ws_event_data_size, (int id), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || record.events.length === 0 || !record.events[0].data) {
    return 0;
  }
  return record.events[0].data.byteLength | 0;
});

EM_JS(void, arv_ws_copy_event_data, (int id, unsigned char* out), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || record.events.length === 0 || !record.events[0].data) {
    return;
  }
  HEAPU8.set(record.events[0].data, out);
});

EM_JS(int, arv_ws_event_reason_size, (int id), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || record.events.length === 0 || !record.events[0].reason) {
    return 0;
  }
  return lengthBytesUTF8(record.events[0].reason) | 0;
});

EM_JS(void, arv_ws_copy_event_reason, (int id, char* out, int size), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || record.events.length === 0 || !record.events[0].reason ||
      size <= 0) {
    return;
  }
  stringToUTF8(record.events[0].reason, out, size);
});

EM_JS(void, arv_ws_pop_event, (int id), {
  const root = Module.arvWebSocketBridge;
  const record = root && root.sockets[id];
  if (!record || record.events.length === 0) {
    return;
  }
  record.events.shift();
});

[[nodiscard]] bool has_websocket_scheme(std::string_view value) noexcept {
  return value.starts_with("ws://") || value.starts_with("wss://");
}

[[nodiscard]] std::string websocket_url_from(const NetworkAddress& address) {
  if (has_websocket_scheme(address.host)) {
    return address.host;
  }
  return "ws://" + address.host + ":" + std::to_string(address.port) + "/arvane";
}

[[nodiscard]] bool active_state(ConnectionState state) noexcept {
  return state != ConnectionState::Disconnected &&
         state != ConnectionState::Disconnecting;
}

[[nodiscard]] NetworkEvent make_event(
    NetworkEventType type,
    std::string reason = {}) {
  NetworkEvent event;
  event.type = type;
  event.connection_id = WebSocketConnectionID;
  event.reason = std::move(reason);
  event.receive_time = std::chrono::steady_clock::now();
  return event;
}

[[nodiscard]] std::string read_event_reason(int socket_id) {
  const int size = arv_ws_event_reason_size(socket_id);
  if (size <= 0) {
    return {};
  }
  std::string reason(static_cast<usize>(size + 1), '\0');
  arv_ws_copy_event_reason(socket_id, reason.data(), size + 1);
  reason.resize(static_cast<usize>(size));
  return reason;
}

} // namespace

struct WebSocketClientTransport::Impl {
  explicit Impl(WebSocketClientTransportOptions init_options)
      : options(init_options) {
  }

  void queue(NetworkEvent event) {
    queued_events.emplace_back(std::move(event));
  }

  void close_socket(u16 code, std::string_view reason) {
    if (socket_id == 0) {
      return;
    }
    close_reason = std::string{reason};
    arv_ws_close(socket_id, static_cast<int>(code), close_reason.c_str());
  }

  void delete_socket() {
    if (socket_id == 0) {
      return;
    }
    arv_ws_delete(socket_id);
    socket_id = 0;
    latest_backlogged_message.reset();
  }

  [[nodiscard]] bool has_send_capacity(usize payload_size) const {
    const int buffered_amount = arv_ws_buffered_amount(socket_id);
    return buffered_amount < 0 ||
           static_cast<usize>(buffered_amount) + payload_size <= options.max_pending_bytes;
  }

  [[nodiscard]] NetworkResult send_now(const TransportMessage& message) const {
    const int result = arv_ws_send(
        socket_id,
        message.payload.data(),
        static_cast<int>(message.payload.size()));
    if (result == 0) {
      return NetworkResult::failure(NetworkError{"failed to send browser WebSocket frame"});
    }
    return NetworkResult::success();
  }

  void flush_latest_backlog() {
    if (!latest_backlogged_message.has_value() ||
        socket_id == 0 ||
        connection_state != ConnectionState::TransportConnected ||
        !has_send_capacity(latest_backlogged_message->payload.size())) {
      return;
    }

    TransportMessage message = std::move(*latest_backlogged_message);
    latest_backlogged_message.reset();
    auto result = send_now(message);
    if (!result) {
      queue(make_event(NetworkEventType::MessageSendFailed, result.error().message));
    }
  }

  WebSocketClientTransportOptions options{};
  int socket_id{};
  ConnectionState connection_state{ConnectionState::Disconnected};
  std::string url{};
  std::string close_reason{};
  std::vector<NetworkEvent> queued_events{};
  std::optional<TransportMessage> latest_backlogged_message{};
};

WebSocketClientTransport::WebSocketClientTransport(
    WebSocketClientTransportOptions options)
    : impl_(std::make_unique<Impl>(options)) {
}

WebSocketClientTransport::~WebSocketClientTransport() {
  shutdown();
}

NetworkResult WebSocketClientTransport::start_listening(const ListenAddress&) {
  return NetworkResult::failure(
      NetworkError{"browser WebSocket transport cannot listen"});
}

ConnectResult WebSocketClientTransport::connect(const NetworkAddress& address) {
  if (impl_->socket_id != 0 && active_state(impl_->connection_state)) {
    return ConnectResult::failure(NetworkError{"browser WebSocket is already connected"});
  }

  impl_->delete_socket();
  impl_->url = websocket_url_from(address);
  impl_->socket_id = arv_ws_create(impl_->url.c_str());
  if (impl_->socket_id == 0) {
    impl_->connection_state = ConnectionState::Disconnected;
    return ConnectResult::failure(NetworkError{"failed to create browser WebSocket"});
  }

  impl_->connection_state = ConnectionState::Connecting;
  return ConnectResult::success(WebSocketConnectionID);
}

NetworkResult WebSocketClientTransport::send(TransportMessage message) {
  if (message.connection_id != WebSocketConnectionID ||
      impl_->socket_id == 0 ||
      impl_->connection_state != ConnectionState::TransportConnected) {
    return NetworkResult::failure(NetworkError{"browser WebSocket is not connected"});
  }
  if (message.payload.size() > impl_->options.max_frame_bytes) {
    return NetworkResult::failure(NetworkError{"WebSocket frame exceeds max_frame_bytes"});
  }

  if (message.reliability == protocol::ReliabilityClass::UnreliableLatestWins) {
    impl_->latest_backlogged_message.reset();
  }
  if (!impl_->has_send_capacity(message.payload.size())) {
    if (message.reliability == protocol::ReliabilityClass::UnreliableLatestWins) {
      impl_->latest_backlogged_message = std::move(message);
      return NetworkResult::success();
    }
    return NetworkResult::failure(NetworkError{"browser WebSocket send backlog is full"});
  }

  return impl_->send_now(message);
}

NetworkResult WebSocketClientTransport::close_connection(
    ConnectionID connection_id,
    std::string_view reason) {
  if (connection_id != WebSocketConnectionID) {
    return NetworkResult::success();
  }
  impl_->connection_state = ConnectionState::Disconnecting;
  impl_->close_socket(1000U, reason.empty() ? "client disconnect" : reason);
  return NetworkResult::success();
}

std::vector<NetworkEvent> WebSocketClientTransport::poll_events() {
  impl_->flush_latest_backlog();
  if (impl_->socket_id != 0) {
    for (int guard = 0; guard < 128; ++guard) {
      const int type = arv_ws_event_type(impl_->socket_id);
      if (type == 0) {
        break;
      }
      if (type == 1) {
        impl_->connection_state = ConnectionState::TransportConnected;
        impl_->queue(make_event(NetworkEventType::ConnectionAccepted));
      } else if (type == 2) {
        if (arv_ws_event_is_text(impl_->socket_id) != 0) {
          impl_->queue(make_event(
              NetworkEventType::ConnectionFailed,
              "server sent unsupported WebSocket text frame"));
          impl_->connection_state = ConnectionState::Disconnecting;
          impl_->close_socket(1003U, "binary frames required");
        } else {
          const int size = arv_ws_event_data_size(impl_->socket_id);
          if (size < 0 || static_cast<usize>(size) > impl_->options.max_frame_bytes) {
            impl_->queue(make_event(
                NetworkEventType::ConnectionFailed,
                "server sent oversized WebSocket binary frame"));
            impl_->connection_state = ConnectionState::Disconnecting;
            impl_->close_socket(1009U, "frame too large");
          } else {
            NetworkEvent message = make_event(NetworkEventType::MessageReceived);
            message.payload.resize(static_cast<usize>(size));
            if (size > 0) {
              arv_ws_copy_event_data(impl_->socket_id, message.payload.data());
            }
            impl_->queue(std::move(message));
          }
        }
      } else if (type == 3) {
        const bool was_connecting =
            impl_->connection_state == ConnectionState::Connecting;
        impl_->connection_state = ConnectionState::Disconnected;
        impl_->queue(make_event(
            was_connecting ? NetworkEventType::ConnectionFailed
                           : NetworkEventType::ConnectionClosed,
            read_event_reason(impl_->socket_id)));
        impl_->delete_socket();
      } else if (type == 4) {
        impl_->connection_state = ConnectionState::Disconnected;
        impl_->queue(make_event(
            NetworkEventType::ConnectionFailed,
            read_event_reason(impl_->socket_id)));
      }
      if (impl_->socket_id == 0) {
        break;
      }
      arv_ws_pop_event(impl_->socket_id);
    }
  }

  std::vector<NetworkEvent> events;
  events.swap(impl_->queued_events);
  return events;
}

ConnectionState WebSocketClientTransport::state(ConnectionID connection_id) const {
  return connection_id == WebSocketConnectionID ? impl_->connection_state
                                                : ConnectionState::Disconnected;
}

void WebSocketClientTransport::shutdown() {
  if (impl_ == nullptr) {
    return;
  }
  if (impl_->socket_id != 0) {
    impl_->connection_state = ConnectionState::Disconnecting;
    impl_->close_socket(1000U, "Arvane shutdown");
    impl_->delete_socket();
  }
  impl_->connection_state = ConnectionState::Disconnected;
  impl_->queued_events.clear();
  impl_->latest_backlogged_message.reset();
}

} // namespace arv::net

#endif
