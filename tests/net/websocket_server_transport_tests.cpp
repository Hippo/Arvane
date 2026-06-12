#include "arv/net/websocket_server_transport.hpp"
#include "support/net_events.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using arv::testsupport::find_event;

struct SocketHandle {
  int fd{-1};

  SocketHandle() = default;
  explicit SocketHandle(int socket_fd) : fd(socket_fd) {}
  ~SocketHandle() {
    if (fd >= 0) {
      (void)::close(fd);
    }
  }

  SocketHandle(const SocketHandle&) = delete;
  SocketHandle& operator=(const SocketHandle&) = delete;

  SocketHandle(SocketHandle&& other) noexcept : fd(other.fd) {
    other.fd = -1;
  }

  SocketHandle& operator=(SocketHandle&& other) noexcept {
    if (this != &other) {
      if (fd >= 0) {
        (void)::close(fd);
      }
      fd = other.fd;
      other.fd = -1;
    }
    return *this;
  }
};

[[nodiscard]] bool set_nonblocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

[[nodiscard]] std::optional<arv::u16> start_websocket_test_listener(
    arv::net::WebSocketServerTransport& server) {
  const auto seed = static_cast<arv::u16>(
      std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
  for (arv::u16 attempt = 0; attempt < 100; ++attempt) {
    const arv::u16 port = static_cast<arv::u16>(46000U + ((seed + attempt) % 1000U));
    if (server.start_listening({"127.0.0.1", port})) {
      return port;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<SocketHandle> connect_loopback(arv::u16 port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::nullopt;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    (void)::close(fd);
    return std::nullopt;
  }
  if (!set_nonblocking(fd)) {
    (void)::close(fd);
    return std::nullopt;
  }
  return SocketHandle{fd};
}

bool socket_would_block() {
  return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool send_all(int fd, std::span<const arv::u8> bytes) {
  while (!bytes.empty()) {
    const ssize_t sent = ::send(fd, bytes.data(), bytes.size(), 0);
    if (sent > 0) {
      bytes = bytes.subspan(static_cast<arv::usize>(sent));
      continue;
    }
    if (sent < 0 && socket_would_block()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    return false;
  }
  return true;
}

bool send_all(int fd, std::string_view text) {
  return send_all(
      fd,
      std::span<const arv::u8>{
          reinterpret_cast<const arv::u8*>(text.data()),
          text.size()});
}

void read_available(int fd, std::vector<arv::u8>& buffer) {
  std::array<arv::u8, 4096> bytes{};
  while (true) {
    const ssize_t received = ::recv(fd, bytes.data(), bytes.size(), 0);
    if (received > 0) {
      buffer.insert(buffer.end(), bytes.begin(), bytes.begin() + received);
      continue;
    }
    if (received < 0 && socket_would_block()) {
      return;
    }
    return;
  }
}

[[nodiscard]] std::string text_from_bytes(const std::vector<arv::u8>& bytes) {
  return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::vector<arv::u8> client_frame(
    arv::u8 opcode,
    std::span<const arv::u8> payload,
    bool masked = true) {
  std::vector<arv::u8> frame;
  frame.push_back(static_cast<arv::u8>(0x80U | (opcode & 0x0FU)));
  const arv::u8 mask_bit = masked ? 0x80U : 0U;
  if (payload.size() <= 125U) {
    frame.push_back(static_cast<arv::u8>(mask_bit | payload.size()));
  } else {
    frame.push_back(static_cast<arv::u8>(mask_bit | 126U));
    frame.push_back(static_cast<arv::u8>((payload.size() >> 8U) & 0xFFU));
    frame.push_back(static_cast<arv::u8>(payload.size() & 0xFFU));
  }
  constexpr std::array<arv::u8, 4> mask{0x11U, 0x22U, 0x33U, 0x44U};
  if (masked) {
    frame.insert(frame.end(), mask.begin(), mask.end());
  }
  for (arv::usize i = 0; i < payload.size(); ++i) {
    frame.push_back(masked ? static_cast<arv::u8>(payload[i] ^ mask[i % 4U]) : payload[i]);
  }
  return frame;
}

struct DecodedFrame {
  arv::u8 opcode{};
  std::vector<arv::u8> payload{};
};

[[nodiscard]] std::optional<DecodedFrame> decode_server_frame(
    const std::vector<arv::u8>& buffer) {
  if (buffer.size() < 2U) {
    return std::nullopt;
  }
  const arv::u8 opcode = buffer[0] & 0x0FU;
  arv::u64 payload_length = buffer[1] & 0x7FU;
  arv::usize offset = 2U;
  if (payload_length == 126U) {
    if (buffer.size() < offset + 2U) {
      return std::nullopt;
    }
    payload_length = (static_cast<arv::u64>(buffer[offset]) << 8U) |
                     static_cast<arv::u64>(buffer[offset + 1U]);
    offset += 2U;
  } else if (payload_length == 127U) {
    if (buffer.size() < offset + 8U) {
      return std::nullopt;
    }
    payload_length = 0U;
    for (arv::usize i = 0; i < 8U; ++i) {
      payload_length = (payload_length << 8U) | static_cast<arv::u64>(buffer[offset + i]);
    }
    offset += 8U;
  }
  if (buffer.size() < offset + static_cast<arv::usize>(payload_length)) {
    return std::nullopt;
  }
  return DecodedFrame{
      opcode,
      std::vector<arv::u8>{
          buffer.begin() + static_cast<arv::isize>(offset),
          buffer.begin() + static_cast<arv::isize>(offset + payload_length)}};
}

[[nodiscard]] std::optional<arv::net::ConnectionID> open_websocket(
    arv::net::WebSocketServerTransport& server,
    int fd,
    std::string_view path,
    std::vector<arv::u8>& response) {
  const std::string request =
      "GET " + std::string{path} + " HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n";
  if (!send_all(fd, request)) {
    return std::nullopt;
  }

  std::optional<arv::net::ConnectionID> id;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const std::vector<arv::net::NetworkEvent> events = server.poll_events();
    if (const arv::net::NetworkEvent* accepted =
            find_event(events, arv::net::NetworkEventType::ConnectionAccepted);
        accepted != nullptr) {
      id = accepted->connection_id;
    }
    read_available(fd, response);
    if (id.has_value() &&
        text_from_bytes(response).find("101 Switching Protocols") != std::string::npos) {
      return id;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return std::nullopt;
}

} // namespace

TEST(WebSocketServerTransportTests, AcceptKeyMatchesRfcSample) {
  EXPECT_EQ(
      arv::net::detail::websocket_accept_key("dGhlIHNhbXBsZSBub25jZQ=="),
      "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WebSocketServerTransportTests, LoopbackDeliversBinaryMessagesBothDirections) {
  arv::net::WebSocketServerTransport server{
      arv::net::WebSocketServerTransportOptions{
          "127.0.0.1",
          0U,
          "/arvane-test",
          1024U,
          4096U}};
  const std::optional<arv::u16> port = start_websocket_test_listener(server);
  ASSERT_TRUE(port.has_value());
  std::optional<SocketHandle> client = connect_loopback(port.value());
  ASSERT_TRUE(client.has_value());

  std::vector<arv::u8> response;
  const std::optional<arv::net::ConnectionID> connection_id =
      open_websocket(server, client->fd, "/arvane-test", response);
  ASSERT_TRUE(connection_id.has_value());
  EXPECT_NE(text_from_bytes(response).find("Sec-WebSocket-Accept"), std::string::npos);

  const std::vector<arv::u8> client_payload{1, 2, 3};
  ASSERT_TRUE(send_all(client->fd, client_frame(0x2U, client_payload)));
  bool server_received = false;
  for (int attempt = 0; attempt < 100 && !server_received; ++attempt) {
    for (const arv::net::NetworkEvent& event : server.poll_events()) {
      if (event.type == arv::net::NetworkEventType::MessageReceived &&
          event.connection_id == connection_id.value() &&
          event.payload == client_payload) {
        server_received = true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_TRUE(server_received);

  ASSERT_TRUE(server.send({
      connection_id.value(),
      arv::protocol::ReliabilityClass::ReliableOrdered,
      std::vector<arv::u8>{4, 5},
  }));
  std::vector<arv::u8> server_frame_bytes;
  std::optional<DecodedFrame> frame;
  for (int attempt = 0; attempt < 100 && !frame.has_value(); ++attempt) {
    (void)server.poll_events();
    read_available(client->fd, server_frame_bytes);
    frame = decode_server_frame(server_frame_bytes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->opcode, 0x2U);
  EXPECT_EQ(frame->payload, (std::vector<arv::u8>{4, 5}));
}

TEST(WebSocketServerTransportTests, RejectsWrongPathBeforeAcceptingSession) {
  arv::net::WebSocketServerTransport server{
      arv::net::WebSocketServerTransportOptions{
          "127.0.0.1",
          0U,
          "/arvane-test",
          1024U,
          4096U}};
  const std::optional<arv::u16> port = start_websocket_test_listener(server);
  ASSERT_TRUE(port.has_value());
  std::optional<SocketHandle> client = connect_loopback(port.value());
  ASSERT_TRUE(client.has_value());

  const std::string request =
      "GET /wrong HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n";
  ASSERT_TRUE(send_all(client->fd, request));

  std::vector<arv::u8> response;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const std::vector<arv::net::NetworkEvent> events = server.poll_events();
    EXPECT_EQ(
        find_event(events, arv::net::NetworkEventType::ConnectionAccepted),
        nullptr);
    read_available(client->fd, response);
    if (text_from_bytes(response).find("404 Not Found") != std::string::npos) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_NE(text_from_bytes(response).find("404 Not Found"), std::string::npos);
}

TEST(WebSocketServerTransportTests, RejectsTextFramesAfterHandshake) {
  arv::net::WebSocketServerTransport server{
      arv::net::WebSocketServerTransportOptions{
          "127.0.0.1",
          0U,
          "/arvane-test",
          1024U,
          4096U}};
  const std::optional<arv::u16> port = start_websocket_test_listener(server);
  ASSERT_TRUE(port.has_value());
  std::optional<SocketHandle> client = connect_loopback(port.value());
  ASSERT_TRUE(client.has_value());

  std::vector<arv::u8> response;
  const std::optional<arv::net::ConnectionID> connection_id =
      open_websocket(server, client->fd, "/arvane-test", response);
  ASSERT_TRUE(connection_id.has_value());

  const std::array<arv::u8, 5> text_payload{'h', 'e', 'l', 'l', 'o'};
  ASSERT_TRUE(send_all(client->fd, client_frame(0x1U, text_payload)));
  bool closed = false;
  for (int attempt = 0; attempt < 100 && !closed; ++attempt) {
    for (const arv::net::NetworkEvent& event : server.poll_events()) {
      if (event.type == arv::net::NetworkEventType::ConnectionClosed) {
        closed = true;
        EXPECT_EQ(event.connection_id, connection_id.value());
        EXPECT_NE(event.reason.find("text frames"), std::string::npos);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_TRUE(closed);
  EXPECT_EQ(server.state(connection_id.value()), arv::net::ConnectionState::Disconnected);
}

TEST(WebSocketServerTransportTests, RejectsOversizedBinaryFrames) {
  arv::net::WebSocketServerTransport server{
      arv::net::WebSocketServerTransportOptions{
          "127.0.0.1",
          0U,
          "/arvane-test",
          4U,
          4096U}};
  const std::optional<arv::u16> port = start_websocket_test_listener(server);
  ASSERT_TRUE(port.has_value());
  std::optional<SocketHandle> client = connect_loopback(port.value());
  ASSERT_TRUE(client.has_value());

  std::vector<arv::u8> response;
  const std::optional<arv::net::ConnectionID> connection_id =
      open_websocket(server, client->fd, "/arvane-test", response);
  ASSERT_TRUE(connection_id.has_value());

  ASSERT_TRUE(send_all(client->fd, client_frame(0x2U, std::vector<arv::u8>{1, 2, 3, 4, 5})));
  bool closed = false;
  for (int attempt = 0; attempt < 100 && !closed; ++attempt) {
    for (const arv::net::NetworkEvent& event : server.poll_events()) {
      if (event.type == arv::net::NetworkEventType::ConnectionClosed) {
        closed = true;
        EXPECT_EQ(event.connection_id, connection_id.value());
        EXPECT_NE(event.reason.find("maximum size"), std::string::npos);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_TRUE(closed);
}

TEST(WebSocketServerTransportTests, RejectsUnmaskedClientFrames) {
  arv::net::WebSocketServerTransport server{
      arv::net::WebSocketServerTransportOptions{
          "127.0.0.1",
          0U,
          "/arvane-test",
          1024U,
          4096U}};
  const std::optional<arv::u16> port = start_websocket_test_listener(server);
  ASSERT_TRUE(port.has_value());
  std::optional<SocketHandle> client = connect_loopback(port.value());
  ASSERT_TRUE(client.has_value());

  std::vector<arv::u8> response;
  const std::optional<arv::net::ConnectionID> connection_id =
      open_websocket(server, client->fd, "/arvane-test", response);
  ASSERT_TRUE(connection_id.has_value());

  ASSERT_TRUE(send_all(client->fd, client_frame(0x2U, std::vector<arv::u8>{1, 2, 3}, false)));
  bool closed = false;
  for (int attempt = 0; attempt < 100 && !closed; ++attempt) {
    for (const arv::net::NetworkEvent& event : server.poll_events()) {
      if (event.type == arv::net::NetworkEventType::ConnectionClosed) {
        closed = true;
        EXPECT_EQ(event.connection_id, connection_id.value());
        EXPECT_NE(event.reason.find("unmasked"), std::string::npos);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_TRUE(closed);
}
