#include "arv/net/websocket_server_transport.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <iterator>
#include <span>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace arv::net {
namespace {

constexpr std::string_view WebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr usize MaxHandshakeBytes = 16U * 1024U;
constexpr int InvalidSocket = -1;

[[nodiscard]] u32 rotl(u32 value, u32 bits) noexcept {
  return (value << bits) | (value >> (32U - bits));
}

[[nodiscard]] std::array<u8, 20> sha1(std::string_view input) {
  std::vector<u8> bytes(input.begin(), input.end());
  const u64 bit_count = static_cast<u64>(bytes.size()) * 8ULL;
  bytes.push_back(0x80U);
  while ((bytes.size() % 64U) != 56U) {
    bytes.push_back(0U);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<u8>((bit_count >> shift) & 0xFFU));
  }

  u32 h0 = 0x67452301U;
  u32 h1 = 0xEFCDAB89U;
  u32 h2 = 0x98BADCFEU;
  u32 h3 = 0x10325476U;
  u32 h4 = 0xC3D2E1F0U;

  for (usize offset = 0; offset < bytes.size(); offset += 64U) {
    std::array<u32, 80> words{};
    for (usize i = 0; i < 16U; ++i) {
      const usize base = offset + (i * 4U);
      words[i] = (static_cast<u32>(bytes[base]) << 24U) |
                 (static_cast<u32>(bytes[base + 1U]) << 16U) |
                 (static_cast<u32>(bytes[base + 2U]) << 8U) |
                 static_cast<u32>(bytes[base + 3U]);
    }
    for (usize i = 16U; i < 80U; ++i) {
      words[i] = rotl(words[i - 3U] ^ words[i - 8U] ^ words[i - 14U] ^ words[i - 16U], 1U);
    }

    u32 a = h0;
    u32 b = h1;
    u32 c = h2;
    u32 d = h3;
    u32 e = h4;
    for (usize i = 0; i < 80U; ++i) {
      u32 f = 0U;
      u32 k = 0U;
      if (i < 20U) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999U;
      } else if (i < 40U) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1U;
      } else if (i < 60U) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCU;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6U;
      }
      const u32 temp = rotl(a, 5U) + f + e + k + words[i];
      e = d;
      d = c;
      c = rotl(b, 30U);
      b = a;
      a = temp;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<u8, 20> digest{};
  const std::array<u32, 5> words{h0, h1, h2, h3, h4};
  for (usize i = 0; i < words.size(); ++i) {
    digest[(i * 4U)] = static_cast<u8>((words[i] >> 24U) & 0xFFU);
    digest[(i * 4U) + 1U] = static_cast<u8>((words[i] >> 16U) & 0xFFU);
    digest[(i * 4U) + 2U] = static_cast<u8>((words[i] >> 8U) & 0xFFU);
    digest[(i * 4U) + 3U] = static_cast<u8>(words[i] & 0xFFU);
  }
  return digest;
}

[[nodiscard]] std::string base64_encode(std::span<const u8> bytes) {
  static constexpr char Table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((bytes.size() + 2U) / 3U) * 4U);
  for (usize i = 0; i < bytes.size(); i += 3U) {
    const u32 b0 = bytes[i];
    const u32 b1 = i + 1U < bytes.size() ? bytes[i + 1U] : 0U;
    const u32 b2 = i + 2U < bytes.size() ? bytes[i + 2U] : 0U;
    const u32 triple = (b0 << 16U) | (b1 << 8U) | b2;
    encoded.push_back(Table[(triple >> 18U) & 0x3FU]);
    encoded.push_back(Table[(triple >> 12U) & 0x3FU]);
    encoded.push_back(i + 1U < bytes.size() ? Table[(triple >> 6U) & 0x3FU] : '=');
    encoded.push_back(i + 2U < bytes.size() ? Table[triple & 0x3FU] : '=');
  }
  return encoded;
}

[[nodiscard]] char ascii_lower(char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] std::string lower_copy(std::string_view value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const char c : value) {
    lowered.push_back(ascii_lower(c));
  }
  return lowered;
}

[[nodiscard]] std::string trim_copy(std::string_view value) {
  usize begin = 0U;
  while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t')) {
    ++begin;
  }
  usize end = value.size();
  while (end > begin &&
         (value[end - 1U] == ' ' || value[end - 1U] == '\t' ||
          value[end - 1U] == '\r')) {
    --end;
  }
  return std::string{value.substr(begin, end - begin)};
}

[[nodiscard]] bool iequals(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (usize i = 0; i < lhs.size(); ++i) {
    if (ascii_lower(lhs[i]) != ascii_lower(rhs[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool has_header_token(std::string_view value, std::string_view token) {
  usize begin = 0U;
  while (begin < value.size()) {
    usize end = value.find(',', begin);
    if (end == std::string_view::npos) {
      end = value.size();
    }
    if (iequals(trim_copy(value.substr(begin, end - begin)), token)) {
      return true;
    }
    begin = end + 1U;
  }
  return false;
}

[[nodiscard]] std::vector<u8> bytes_from_string(std::string_view text) {
  return {text.begin(), text.end()};
}

[[nodiscard]] std::vector<u8> encode_websocket_frame(u8 opcode, std::span<const u8> payload) {
  std::vector<u8> frame;
  frame.reserve(payload.size() + 10U);
  frame.push_back(static_cast<u8>(0x80U | (opcode & 0x0FU)));
  if (payload.size() <= 125U) {
    frame.push_back(static_cast<u8>(payload.size()));
  } else if (payload.size() <= std::numeric_limits<u16>::max()) {
    frame.push_back(126U);
    frame.push_back(static_cast<u8>((payload.size() >> 8U) & 0xFFU));
    frame.push_back(static_cast<u8>(payload.size() & 0xFFU));
  } else {
    frame.push_back(127U);
    const u64 length = static_cast<u64>(payload.size());
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<u8>((length >> shift) & 0xFFU));
    }
  }
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

[[nodiscard]] std::vector<u8> encode_close_frame(u16 code, std::string_view reason) {
  std::vector<u8> payload;
  payload.reserve(125U);
  payload.push_back(static_cast<u8>((code >> 8U) & 0xFFU));
  payload.push_back(static_cast<u8>(code & 0xFFU));
  const usize reason_bytes = std::min<usize>(reason.size(), 123U);
  payload.insert(payload.end(), reason.begin(), reason.begin() + static_cast<isize>(reason_bytes));
  return encode_websocket_frame(0x8U, payload);
}

[[nodiscard]] std::string http_error_response(
    std::string_view status,
    std::string_view reason) {
  std::ostringstream out;
  out << "HTTP/1.1 " << status << "\r\n"
      << "Connection: close\r\n"
      << "Content-Type: text/plain\r\n"
      << "Content-Length: " << reason.size() << "\r\n\r\n"
      << reason;
  return out.str();
}

[[nodiscard]] bool socket_would_block() noexcept {
  return errno == EAGAIN || errno == EWOULDBLOCK;
}

void close_fd(int& fd) noexcept {
  if (fd != InvalidSocket) {
    (void)::close(fd);
    fd = InvalidSocket;
  }
}

[[nodiscard]] NetworkError socket_error(std::string_view action) {
  return NetworkError{std::string{action} + ": " + std::strerror(errno)};
}

[[nodiscard]] bool set_nonblocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

[[nodiscard]] std::optional<sockaddr_in> make_sockaddr(
    std::string_view address,
    u16 port) {
  sockaddr_in socket_address{};
  socket_address.sin_family = AF_INET;
  socket_address.sin_port = htons(port);
  if (address.empty() || address == "0.0.0.0") {
    socket_address.sin_addr.s_addr = htonl(INADDR_ANY);
    return socket_address;
  }
  if (address == "localhost") {
    socket_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return socket_address;
  }
  if (::inet_pton(AF_INET, std::string{address}.c_str(), &socket_address.sin_addr) != 1) {
    return std::nullopt;
  }
  return socket_address;
}

[[nodiscard]] ssize_t send_no_signal(int fd, const u8* data, usize size) {
#ifdef MSG_NOSIGNAL
  return ::send(fd, data, size, MSG_NOSIGNAL);
#else
  return ::send(fd, data, size, 0);
#endif
}

[[nodiscard]] ssize_t send_no_signal(int fd, const char* data, usize size) {
#ifdef MSG_NOSIGNAL
  return ::send(fd, data, size, MSG_NOSIGNAL);
#else
  return ::send(fd, data, size, 0);
#endif
}

void best_effort_send(int fd, std::span<const u8> bytes) {
  while (!bytes.empty()) {
    const ssize_t sent = send_no_signal(fd, bytes.data(), bytes.size());
    if (sent > 0) {
      bytes = bytes.subspan(static_cast<usize>(sent));
      continue;
    }
    if (sent < 0 && socket_would_block()) {
      return;
    }
    return;
  }
}

void best_effort_send(int fd, std::string_view bytes) {
  while (!bytes.empty()) {
    const ssize_t sent = send_no_signal(fd, bytes.data(), bytes.size());
    if (sent > 0) {
      bytes.remove_prefix(static_cast<usize>(sent));
      continue;
    }
    if (sent < 0 && socket_would_block()) {
      return;
    }
    return;
  }
}

struct HandshakeResult {
  bool accepted{};
  std::string response{};
  std::string reason{};
};

[[nodiscard]] HandshakeResult make_handshake_response(
    std::string_view request,
    std::string_view expected_path) {
  std::istringstream stream{std::string{request}};
  std::string request_line;
  if (!std::getline(stream, request_line)) {
    return {false, http_error_response("400 Bad Request", "bad websocket request"), "bad request"};
  }
  if (!request_line.empty() && request_line.back() == '\r') {
    request_line.pop_back();
  }

  const usize first_space = request_line.find(' ');
  const usize second_space = first_space == std::string::npos
                                 ? std::string::npos
                                 : request_line.find(' ', first_space + 1U);
  if (first_space == std::string::npos || second_space == std::string::npos) {
    return {false, http_error_response("400 Bad Request", "bad websocket request"), "bad request"};
  }
  const std::string_view method{request_line.data(), first_space};
  const std::string_view target{
      request_line.data() + first_space + 1U,
      second_space - first_space - 1U};
  if (!iequals(method, "GET")) {
    return {false, http_error_response("405 Method Not Allowed", "websocket requires GET"), "bad method"};
  }
  if (target != expected_path) {
    return {false, http_error_response("404 Not Found", "websocket path not found"), "bad path"};
  }

  std::unordered_map<std::string, std::string> headers;
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      break;
    }
    const usize colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    headers[lower_copy(std::string_view{line}.substr(0, colon))] =
        trim_copy(std::string_view{line}.substr(colon + 1U));
  }

  const auto upgrade = headers.find("upgrade");
  const auto connection = headers.find("connection");
  const auto version = headers.find("sec-websocket-version");
  const auto key = headers.find("sec-websocket-key");
  if (upgrade == headers.end() || !iequals(upgrade->second, "websocket") ||
      connection == headers.end() || !has_header_token(connection->second, "upgrade") ||
      version == headers.end() || trim_copy(version->second) != "13" ||
      key == headers.end() || trim_copy(key->second).empty()) {
    return {false, http_error_response("400 Bad Request", "invalid websocket upgrade"), "bad upgrade"};
  }

  std::ostringstream response;
  response << "HTTP/1.1 101 Switching Protocols\r\n"
           << "Upgrade: websocket\r\n"
           << "Connection: Upgrade\r\n"
           << "Sec-WebSocket-Accept: "
           << detail::websocket_accept_key(trim_copy(key->second))
           << "\r\n\r\n";
  return {true, response.str(), {}};
}

} // namespace

namespace detail {

std::string websocket_accept_key(std::string_view client_key) {
  std::string seed{client_key};
  seed.append(WebSocketGuid);
  const std::array<u8, 20> digest = sha1(seed);
  return base64_encode(digest);
}

} // namespace detail

struct WebSocketServerTransport::Impl {
  struct Connection {
    int fd{InvalidSocket};
    ConnectionID id{};
    ConnectionState state{ConnectionState::Connecting};
    bool handshake_complete{};
    std::string handshake_buffer{};
    std::vector<u8> input_buffer{};
    std::vector<u8> write_buffer{};
    usize write_offset{};
    usize pending_bytes{};
  };

  explicit Impl(WebSocketServerTransportOptions options_in)
      : options(std::move(options_in)) {
  }

  ~Impl() {
    shutdown();
  }

  [[nodiscard]] NetworkResult start_listening(const ListenAddress& address) {
    const std::lock_guard lock{mutex};
    if (listen_fd != InvalidSocket) {
      return NetworkResult::failure(NetworkError{"websocket listener is already running"});
    }

    options.bind_address = address.bind_address;
    options.port = address.port;
    const std::optional<sockaddr_in> socket_address =
        make_sockaddr(options.bind_address, options.port);
    if (!socket_address.has_value()) {
      return NetworkResult::failure(
          NetworkError{"websocket bind address must be an IPv4 address"});
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == InvalidSocket) {
      return NetworkResult::failure(socket_error("create websocket socket"));
    }
    int enable = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (!set_nonblocking(fd)) {
      close_fd(fd);
      return NetworkResult::failure(socket_error("configure websocket socket"));
    }
    if (::bind(
            fd,
            reinterpret_cast<const sockaddr*>(&socket_address.value()),
            sizeof(socket_address.value())) != 0) {
      NetworkError error = socket_error("bind websocket socket");
      close_fd(fd);
      return NetworkResult::failure(std::move(error));
    }
    if (::listen(fd, 64) != 0) {
      NetworkError error = socket_error("listen websocket socket");
      close_fd(fd);
      return NetworkResult::failure(std::move(error));
    }
    listen_fd = fd;
    return NetworkResult::success();
  }

  [[nodiscard]] ConnectResult connect(const NetworkAddress&) {
    return ConnectResult::failure(NetworkError{"websocket server transport cannot connect"});
  }

  [[nodiscard]] NetworkResult send(TransportMessage message) {
    const std::lock_guard lock{mutex};
    auto found = connections.find(message.connection_id);
    if (found == connections.end() ||
        found->second.state == ConnectionState::Disconnected ||
        !found->second.handshake_complete) {
      return NetworkResult::failure(NetworkError{"websocket connection is not active"});
    }
    if (message.payload.size() > options.max_frame_bytes) {
      return NetworkResult::failure(NetworkError{"websocket frame exceeds maximum size"});
    }
    std::vector<u8> frame = encode_websocket_frame(0x2U, message.payload);
    if (!append_pending(found->second, std::move(frame))) {
      return NetworkResult::failure(NetworkError{"websocket pending send buffer is full"});
    }
    (void)flush_connection(found->second);
    return NetworkResult::success();
  }

  [[nodiscard]] NetworkResult close_connection(
      ConnectionID connection_id,
      std::string_view reason) {
    const std::lock_guard lock{mutex};
    close_connection_locked(connection_id, nullptr, std::string{reason}, true, 1000U);
    return NetworkResult::success();
  }

  [[nodiscard]] std::vector<NetworkEvent> poll_events() {
    const std::lock_guard lock{mutex};
    std::vector<NetworkEvent> events;
    accept_pending();

    std::vector<ConnectionID> ids;
    ids.reserve(connections.size());
    for (const auto& [id, _] : connections) {
      ids.push_back(id);
    }
    for (ConnectionID id : ids) {
      if (connections.find(id) != connections.end()) {
        read_connection(id, events);
      }
      if (auto found = connections.find(id); found != connections.end()) {
        (void)flush_connection(found->second);
      }
    }
    return events;
  }

  [[nodiscard]] ConnectionState state(ConnectionID connection_id) const {
    const std::lock_guard lock{mutex};
    const auto found = connections.find(connection_id);
    if (found == connections.end()) {
      return ConnectionState::Disconnected;
    }
    return found->second.state;
  }

  void shutdown() {
    const std::lock_guard lock{mutex};
    for (auto& [_, connection] : connections) {
      close_fd(connection.fd);
    }
    connections.clear();
    ids_by_fd.clear();
    close_fd(listen_fd);
  }

  [[nodiscard]] ConnectionID next_id() {
    if (next_connection_id == 0U ||
        next_connection_id == std::numeric_limits<u32>::max()) {
      next_connection_id = 1U;
    }
    ConnectionID id{next_connection_id++};
    while (connections.find(id) != connections.end()) {
      if (next_connection_id == 0U ||
          next_connection_id == std::numeric_limits<u32>::max()) {
        next_connection_id = 1U;
      }
      id = ConnectionID{next_connection_id++};
    }
    return id;
  }

  void accept_pending() {
    if (listen_fd == InvalidSocket) {
      return;
    }
    while (true) {
      sockaddr_in remote_address{};
      socklen_t remote_size = sizeof(remote_address);
      int fd = ::accept(
          listen_fd,
          reinterpret_cast<sockaddr*>(&remote_address),
          &remote_size);
      if (fd == InvalidSocket) {
        if (socket_would_block()) {
          return;
        }
        return;
      }
      if (!set_nonblocking(fd)) {
        close_fd(fd);
        continue;
      }
      Connection connection;
      connection.fd = fd;
      connection.id = next_id();
      ids_by_fd[fd] = connection.id;
      connections[connection.id] = std::move(connection);
    }
  }

  void read_connection(ConnectionID id, std::vector<NetworkEvent>& events) {
    std::array<u8, 8192> buffer{};
    while (true) {
      auto found = connections.find(id);
      if (found == connections.end()) {
        return;
      }
      Connection& connection = found->second;
      const ssize_t received = ::recv(connection.fd, buffer.data(), buffer.size(), 0);
      if (received > 0) {
        if (!connection.handshake_complete) {
          connection.handshake_buffer.append(
              reinterpret_cast<const char*>(buffer.data()),
              static_cast<usize>(received));
          if (connection.handshake_buffer.size() > MaxHandshakeBytes) {
            const std::string response =
                http_error_response("431 Request Header Fields Too Large", "websocket handshake too large");
            best_effort_send(connection.fd, response);
            close_connection_locked(id, nullptr, "websocket handshake too large", false, 1002U);
            return;
          }
          if (!complete_handshake(id, events)) {
            return;
          }
        } else {
          connection.input_buffer.insert(
              connection.input_buffer.end(),
              buffer.begin(),
              buffer.begin() + received);
        }
        if (auto after_read = connections.find(id);
            after_read != connections.end() && after_read->second.handshake_complete) {
          if (!process_frames(id, events)) {
            return;
          }
        }
        continue;
      }
      if (received == 0) {
        close_connection_locked(id, &events, "websocket peer closed", false, 1000U);
        return;
      }
      if (socket_would_block()) {
        return;
      }
      close_connection_locked(id, &events, std::strerror(errno), false, 1002U);
      return;
    }
  }

  [[nodiscard]] bool complete_handshake(
      ConnectionID id,
      std::vector<NetworkEvent>& events) {
    auto found = connections.find(id);
    if (found == connections.end()) {
      return false;
    }
    Connection& connection = found->second;
    const usize header_end = connection.handshake_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      return true;
    }

    const usize body_begin = header_end + 4U;
    const std::string request = connection.handshake_buffer.substr(0, body_begin);
    const HandshakeResult result = make_handshake_response(request, options.path);
    if (!result.accepted) {
      best_effort_send(connection.fd, result.response);
      close_connection_locked(id, nullptr, result.reason, false, 1002U);
      return false;
    }

    connection.write_buffer = bytes_from_string(result.response);
    connection.pending_bytes = connection.write_buffer.size();
    connection.write_offset = 0U;
    connection.handshake_complete = true;
    connection.state = ConnectionState::TransportConnected;
    if (body_begin < connection.handshake_buffer.size()) {
      const std::string_view tail{
          connection.handshake_buffer.data() + body_begin,
          connection.handshake_buffer.size() - body_begin};
      connection.input_buffer.insert(connection.input_buffer.end(), tail.begin(), tail.end());
    }
    connection.handshake_buffer.clear();

    events.push_back({
        NetworkEventType::ConnectionAccepted,
        id,
        protocol::ReliabilityClass::ReliableOrdered,
        {},
        {},
        std::chrono::steady_clock::now(),
    });
    return true;
  }

  [[nodiscard]] bool process_frames(
      ConnectionID id,
      std::vector<NetworkEvent>& events) {
    while (true) {
      auto found = connections.find(id);
      if (found == connections.end()) {
        return false;
      }
      Connection& connection = found->second;
      if (connection.input_buffer.size() < 2U) {
        return true;
      }

      const u8 b0 = connection.input_buffer[0];
      const u8 b1 = connection.input_buffer[1];
      const bool fin = (b0 & 0x80U) != 0U;
      const bool reserved = (b0 & 0x70U) != 0U;
      const u8 opcode = b0 & 0x0FU;
      const bool masked = (b1 & 0x80U) != 0U;
      u64 payload_length = b1 & 0x7FU;
      usize offset = 2U;

      if (!fin || reserved) {
        close_connection_locked(id, &events, "unsupported websocket frame flags", true, 1002U);
        return false;
      }
      if (!masked) {
        close_connection_locked(id, &events, "unmasked websocket frame rejected", true, 1002U);
        return false;
      }
      if (payload_length == 126U) {
        if (connection.input_buffer.size() < offset + 2U) {
          return true;
        }
        payload_length =
            (static_cast<u64>(connection.input_buffer[offset]) << 8U) |
            static_cast<u64>(connection.input_buffer[offset + 1U]);
        offset += 2U;
      } else if (payload_length == 127U) {
        if (connection.input_buffer.size() < offset + 8U) {
          return true;
        }
        payload_length = 0U;
        for (usize i = 0; i < 8U; ++i) {
          payload_length = (payload_length << 8U) |
                           static_cast<u64>(connection.input_buffer[offset + i]);
        }
        offset += 8U;
      }
      if (payload_length > options.max_frame_bytes ||
          payload_length > static_cast<u64>(std::numeric_limits<usize>::max())) {
        close_connection_locked(id, &events, "websocket frame exceeds maximum size", true, 1009U);
        return false;
      }
      if (connection.input_buffer.size() < offset + 4U + static_cast<usize>(payload_length)) {
        return true;
      }
      const std::array<u8, 4> mask{
          connection.input_buffer[offset],
          connection.input_buffer[offset + 1U],
          connection.input_buffer[offset + 2U],
          connection.input_buffer[offset + 3U],
      };
      offset += 4U;
      std::vector<u8> payload(static_cast<usize>(payload_length));
      for (usize i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<u8>(connection.input_buffer[offset + i] ^ mask[i % 4U]);
      }
      connection.input_buffer.erase(
          connection.input_buffer.begin(),
          connection.input_buffer.begin() + static_cast<isize>(offset + payload.size()));

      if (opcode == 0x2U) {
        events.push_back({
            NetworkEventType::MessageReceived,
            id,
            protocol::ReliabilityClass::ReliableOrdered,
            std::move(payload),
            {},
            std::chrono::steady_clock::now(),
        });
      } else if (opcode == 0x8U) {
        close_connection_locked(id, &events, "websocket close", true, 1000U);
        return false;
      } else if (opcode == 0x9U) {
        if (payload.size() > 125U) {
          close_connection_locked(id, &events, "oversized websocket control frame", true, 1002U);
          return false;
        }
        std::vector<u8> pong = encode_websocket_frame(0xAU, payload);
        if (!append_pending(connection, std::move(pong))) {
          close_connection_locked(id, &events, "websocket pending send buffer is full", true, 1009U);
          return false;
        }
      } else if (opcode == 0xAU) {
        continue;
      } else if (opcode == 0x1U) {
        close_connection_locked(id, &events, "websocket text frames are not accepted", true, 1003U);
        return false;
      } else {
        close_connection_locked(id, &events, "unsupported websocket opcode", true, 1002U);
        return false;
      }
    }
  }

  [[nodiscard]] bool append_pending(Connection& connection, std::vector<u8> frame) {
    if (frame.size() > static_cast<usize>(options.max_pending_bytes) ||
        connection.pending_bytes > static_cast<usize>(options.max_pending_bytes) - frame.size()) {
      return false;
    }
    connection.write_buffer.insert(
        connection.write_buffer.end(),
        std::make_move_iterator(frame.begin()),
        std::make_move_iterator(frame.end()));
    connection.pending_bytes += frame.size();
    return true;
  }

  [[nodiscard]] bool flush_connection(Connection& connection) {
    while (connection.write_offset < connection.write_buffer.size()) {
      const usize remaining = connection.write_buffer.size() - connection.write_offset;
      const ssize_t sent = send_no_signal(
          connection.fd,
          connection.write_buffer.data() + connection.write_offset,
          remaining);
      if (sent > 0) {
        const usize sent_bytes = static_cast<usize>(sent);
        connection.write_offset += sent_bytes;
        connection.pending_bytes =
            sent_bytes > connection.pending_bytes ? 0U : connection.pending_bytes - sent_bytes;
        continue;
      }
      if (sent < 0 && socket_would_block()) {
        return true;
      }
      return false;
    }
    connection.write_buffer.clear();
    connection.write_offset = 0U;
    connection.pending_bytes = 0U;
    return true;
  }

  void close_connection_locked(
      ConnectionID id,
      std::vector<NetworkEvent>* events,
      std::string reason,
      bool send_close,
      u16 close_code) {
    auto found = connections.find(id);
    if (found == connections.end()) {
      return;
    }
    Connection connection = std::move(found->second);
    connections.erase(found);
    ids_by_fd.erase(connection.fd);
    if (send_close && connection.handshake_complete) {
      const std::vector<u8> close_frame = encode_close_frame(close_code, reason);
      best_effort_send(connection.fd, close_frame);
    }
    close_fd(connection.fd);
    if (events != nullptr && connection.handshake_complete) {
      events->push_back({
          NetworkEventType::ConnectionClosed,
          id,
          protocol::ReliabilityClass::ReliableOrdered,
          {},
          std::move(reason),
          std::chrono::steady_clock::now(),
      });
    }
  }

  WebSocketServerTransportOptions options{};
  int listen_fd{InvalidSocket};
  std::unordered_map<ConnectionID, Connection> connections{};
  std::unordered_map<int, ConnectionID> ids_by_fd{};
  u32 next_connection_id{1U};
  mutable std::mutex mutex{};
};

WebSocketServerTransport::WebSocketServerTransport()
    : WebSocketServerTransport(WebSocketServerTransportOptions{}) {
}

WebSocketServerTransport::WebSocketServerTransport(WebSocketServerTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {
}

WebSocketServerTransport::~WebSocketServerTransport() = default;

WebSocketServerTransport::WebSocketServerTransport(WebSocketServerTransport&&) noexcept = default;

WebSocketServerTransport& WebSocketServerTransport::operator=(
    WebSocketServerTransport&&) noexcept = default;

NetworkResult WebSocketServerTransport::start_listening(const ListenAddress& address) {
  return impl_->start_listening(address);
}

ConnectResult WebSocketServerTransport::connect(const NetworkAddress& address) {
  return impl_->connect(address);
}

NetworkResult WebSocketServerTransport::send(TransportMessage message) {
  return impl_->send(std::move(message));
}

NetworkResult WebSocketServerTransport::close_connection(
    ConnectionID connection_id,
    std::string_view reason) {
  return impl_->close_connection(connection_id, reason);
}

std::vector<NetworkEvent> WebSocketServerTransport::poll_events() {
  return impl_->poll_events();
}

ConnectionState WebSocketServerTransport::state(ConnectionID connection_id) const {
  return impl_->state(connection_id);
}

void WebSocketServerTransport::shutdown() {
  impl_->shutdown();
}

} // namespace arv::net
