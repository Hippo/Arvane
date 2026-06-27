#include "arv/asset/pack.hpp"
#include "arv/asset/pack_bundle.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/net/websocket_server_transport.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_updates.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/message_type.hpp"
#include "arv/protocol/pack_sync.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/player_movement.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/world_join.hpp"
#include "arv/player/permissions.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/world/world_generation.hpp"
#include "support/server_harness.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using arv::testsupport::connected_player;
using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

std::vector<arv::u8> raw_client_hello_packet(
    const arv::client::LocalPlayerIdentity& identity) {
  arv::protocol::ServerboundHello hello;
  hello.requested_username_or_profile_name = identity.display_name;
  hello.auth_begin = arv::protocol::ServerboundAuthBegin{
      identity.key_pair.public_key,
      identity.display_name,
      identity.profile_id};
  const std::vector<arv::u8> payload = arv::protocol::serialize_serverbound_hello(hello);
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       arv::protocol::MessageType::ServerboundHello,
       0,
       0,
       0,
       0},
      payload);
  return packet;
}

void send_raw_client_hello(
    arv::net::ITransport& client_transport,
    arv::net::ConnectionID connection_id,
    const arv::client::LocalPlayerIdentity& identity) {
  std::vector<arv::u8> packet = raw_client_hello_packet(identity);
  (void)client_transport.send(
      {connection_id, arv::protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
}

void send_raw_keep_alive(
    arv::net::ITransport& client_transport,
    arv::net::ConnectionID connection_id,
    arv::u64 id) {
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       arv::protocol::MessageType::ServerboundKeepAlive,
       0,
       0,
       0,
       0},
      arv::protocol::serialize_serverbound_keep_alive({id}));
  (void)client_transport.send(
      {connection_id,
       arv::protocol::ReliabilityClass::UnreliableEvent,
       std::move(packet),
       arv::protocol::MessageType::ServerboundKeepAlive,
       arv::protocol::NetworkLane::Session});
}

std::vector<arv::u8> raw_auth_response_packet(
    const arv::client::LocalPlayerIdentity& identity,
    const arv::protocol::ClientboundAuthChallenge& challenge) {
  const arv::crypto::PlayerProfileID profile_id =
      identity.profile_id.empty()
          ? arv::crypto::derive_profile_id_from_public_key(identity.key_pair.public_key)
          : identity.profile_id;
  const std::vector<arv::u8> signed_payload = arv::protocol::build_auth_signed_payload(
      {arv::protocol::CurrentProtocolVersion,
       challenge.server_instance_id,
       challenge.session_id,
       profile_id,
       identity.key_pair.public_key,
       challenge.nonce,
       challenge.expires_server_tick});
  const auto* first = reinterpret_cast<const std::byte*>(signed_payload.data());
  arv::protocol::ServerboundAuthResponse response;
  response.signature = arv::crypto::sign_ed25519(
      std::span<const std::byte>{first, signed_payload.size()},
      identity.key_pair.private_key);
  return arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       arv::protocol::MessageType::ServerboundAuthResponse,
       0,
       0,
       0,
       0},
      arv::protocol::serialize_auth_response(response));
}

std::vector<std::string> kick_reasons(std::span<const arv::net::NetworkEvent> events) {
  std::vector<std::string> reasons;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ClientboundKickPlayer) {
      continue;
    }
    const auto kick =
        arv::protocol::deserialize_clientbound_kick_player(decoded.value().payload);
    if (kick) {
      reasons.push_back(kick.value().reason);
    }
  }
  return reasons;
}

std::vector<arv::u64> keep_alive_ids(std::span<const arv::net::NetworkEvent> events) {
  std::vector<arv::u64> ids;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ClientboundKeepAlive) {
      continue;
    }
    const auto keep_alive =
        arv::protocol::deserialize_clientbound_keep_alive(decoded.value().payload);
    if (keep_alive) {
      ids.push_back(keep_alive.value().id);
    }
  }
  return ids;
}

std::vector<std::pair<arv::net::ConnectionID, std::string>> sent_kick_reasons(
    std::span<const arv::net::TransportMessage> messages) {
  std::vector<std::pair<arv::net::ConnectionID, std::string>> reasons;
  for (const arv::net::TransportMessage& message : messages) {
    if (message.message_type != arv::protocol::MessageType::ClientboundKickPlayer) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(message.payload);
    if (!decoded) {
      continue;
    }
    const auto kick =
        arv::protocol::deserialize_clientbound_kick_player(decoded.value().payload);
    if (kick) {
      reasons.push_back({message.connection_id, kick.value().reason});
    }
  }
  return reasons;
}

class ScriptedServerTransport final : public arv::net::ITransport {
public:
  arv::net::NetworkResult start_listening(const arv::net::ListenAddress&) override {
    listening = true;
    return arv::net::NetworkResult::success();
  }

  arv::net::ConnectResult connect(const arv::net::NetworkAddress&) override {
    return arv::net::ConnectResult::failure({"server test transport cannot connect"});
  }

  arv::net::NetworkResult send(arv::net::TransportMessage message) override {
    sent.push_back(std::move(message));
    return arv::net::NetworkResult::success();
  }

  arv::net::NetworkResult close_connection(
      arv::net::ConnectionID connection_id,
      std::string_view reason = {}) override {
    states[connection_id] = arv::net::ConnectionState::Disconnected;
    closed.push_back({connection_id, std::string{reason}});
    return arv::net::NetworkResult::success();
  }

  std::vector<arv::net::NetworkEvent> poll_events() override {
    std::vector<arv::net::NetworkEvent> output;
    while (!events.empty()) {
      output.push_back(std::move(events.front()));
      events.pop_front();
    }
    return output;
  }

  arv::net::ConnectionState state(arv::net::ConnectionID connection_id) const override {
    const auto found = states.find(connection_id);
    return found == states.end() ? arv::net::ConnectionState::TransportConnected
                                 : found->second;
  }

  void shutdown() override {
    listening = false;
    for (auto& [connection_id, state] : states) {
      (void)connection_id;
      state = arv::net::ConnectionState::Disconnected;
    }
  }

  void push_message(arv::net::ConnectionID connection_id, std::vector<arv::u8> payload) {
    states[connection_id] = arv::net::ConnectionState::TransportConnected;
    events.push_back({arv::net::NetworkEventType::MessageReceived,
                      connection_id,
                      arv::protocol::ReliabilityClass::ReliableOrdered,
                      std::move(payload),
                      {},
                      std::chrono::steady_clock::now(),
                      arv::protocol::MessageType::ServerboundHello,
                      arv::protocol::NetworkLane::Session});
  }

  bool listening{};
  std::deque<arv::net::NetworkEvent> events{};
  std::vector<arv::net::TransportMessage> sent{};
  std::vector<std::pair<arv::net::ConnectionID, std::string>> closed{};
  arv::HashMap<arv::net::ConnectionID, arv::net::ConnectionState> states{};
};

arv::u32 player_correction_count(std::span<const arv::net::NetworkEvent> events) {
  arv::u32 count = 0;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (decoded &&
        decoded.value().envelope.message_type == arv::protocol::MessageType::ClientboundPlayerMoveCorrection) {
      ++count;
    }
  }
  return count;
}

std::vector<arv::protocol::ClientboundSetMovementHz> movement_hz_packets(
    std::span<const arv::net::NetworkEvent> events) {
  std::vector<arv::protocol::ClientboundSetMovementHz> packets;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ClientboundSetMovementHz) {
      continue;
    }
    const auto settings =
        arv::protocol::deserialize_clientbound_set_movement_hz(decoded.value().payload);
    if (settings) {
      packets.push_back(settings.value());
    }
  }
  return packets;
}

arv::server::ServerChunkGenerationSettings make_generation_settings(
    const arv::voxel::BlockRegistry& block_registry) {
  auto config = arv::world::parse_world_generation_yaml(R"yaml(
id: arvane:test_overworld
version: 1
seed_salt: "test"
vertical:
  min_y: -16
  max_y: 64
  sea_level: 16
noise:
  terrain:
    frequency: 0.1
density:
  base_height: 8
  height_scale: 12
surface:
  rules:
    - layers:
        - block: arvane:grass_block
          depth: 1
        - block: arvane:dirt
          depth: 2
        - block: arvane:stone
          depth: remaining
)yaml");
  EXPECT_TRUE(config);

  auto palette = arv::world::resolve_world_generation_palette(config.value(), block_registry);
  EXPECT_TRUE(palette);

  arv::server::ServerChunkGenerationSettings settings;
  settings.enabled = true;
  settings.seed = 1234;
  settings.config = std::move(config.value());
  settings.palette = std::move(palette.value());
  return settings;
}

void write_pack_sync_test_manifest(
    const std::filesystem::path& packs_root,
    std::string_view id,
    std::string_view name_space) {
  write_text(
      packs_root / std::string{id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{id} + "\"\n"
                            "display_name: \"" +
          std::string{id} + "\"\n"
                            "version: \"1.0.0\"\n"
                            "namespaces:\n"
                            "  - \"" +
          std::string{name_space} + "\"\n"
                                    "side: \"both\"\n"
                                    "dependencies: []\n"
                                    "load_after: []\n"
                                    "load_before: []\n"
                                    "capabilities:\n"
                                    "  assets: true\n"
                                    "  data: true\n"
                                    "  native_plugin: false\n");
}

std::optional<arv::u16> start_gns_server_runtime(
    arv::server::ServerRuntime& server,
    arv::server::ServerStartInfo start_info) {
  const auto seed = static_cast<arv::u16>(
      std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
  for (arv::u16 attempt = 0; attempt < 100; ++attempt) {
    const arv::u16 port = static_cast<arv::u16>(45000U + ((seed + attempt) % 1000U));
    start_info.listen_address = {"0.0.0.0", port};
    if (server.start(start_info)) {
      return port;
    }
  }
  return std::nullopt;
}

struct RawWebSocketSocket {
  int fd{-1};

  RawWebSocketSocket() = default;
  ~RawWebSocketSocket() {
    close();
  }

  RawWebSocketSocket(const RawWebSocketSocket&) = delete;
  RawWebSocketSocket& operator=(const RawWebSocketSocket&) = delete;

  RawWebSocketSocket(RawWebSocketSocket&& other) noexcept
      : fd(other.fd),
        read_buffer(std::move(other.read_buffer)) {
    other.fd = -1;
  }

  RawWebSocketSocket& operator=(RawWebSocketSocket&& other) noexcept {
    if (this != &other) {
      close();
      fd = other.fd;
      read_buffer = std::move(other.read_buffer);
      other.fd = -1;
    }
    return *this;
  }

  [[nodiscard]] bool connect(arv::u16 port) {
    close();
    fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      close();
      return false;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
      close();
      return false;
    }
    return true;
  }

  void close() {
    if (fd >= 0) {
      (void)::close(fd);
      fd = -1;
    }
    read_buffer.clear();
  }

  [[nodiscard]] bool send_text(std::string_view text) {
    return send_bytes(
        std::span<const arv::u8>{
            reinterpret_cast<const arv::u8*>(text.data()),
            text.size()});
  }

  [[nodiscard]] bool send_binary(std::span<const arv::u8> payload) {
    std::vector<arv::u8> frame;
    frame.push_back(0x82U);
    if (payload.size() <= 125U) {
      frame.push_back(static_cast<arv::u8>(0x80U | payload.size()));
    } else if (payload.size() <= std::numeric_limits<arv::u16>::max()) {
      frame.push_back(0x80U | 126U);
      frame.push_back(static_cast<arv::u8>((payload.size() >> 8U) & 0xFFU));
      frame.push_back(static_cast<arv::u8>(payload.size() & 0xFFU));
    } else {
      frame.push_back(0x80U | 127U);
      const arv::u64 size = static_cast<arv::u64>(payload.size());
      for (int shift = 56; shift >= 0; shift -= 8) {
        frame.push_back(static_cast<arv::u8>((size >> shift) & 0xFFU));
      }
    }
    constexpr std::array<arv::u8, 4> mask{0x45U, 0x23U, 0xA1U, 0x0FU};
    frame.insert(frame.end(), mask.begin(), mask.end());
    for (arv::usize i = 0; i < payload.size(); ++i) {
      frame.push_back(static_cast<arv::u8>(payload[i] ^ mask[i % 4U]));
    }
    return send_bytes(frame);
  }

  void read_available() {
    std::array<arv::u8, 4096> bytes{};
    while (fd >= 0) {
      const ssize_t received = ::recv(fd, bytes.data(), bytes.size(), 0);
      if (received > 0) {
        read_buffer.insert(read_buffer.end(), bytes.begin(), bytes.begin() + received);
        continue;
      }
      if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
      }
      return;
    }
  }

  [[nodiscard]] bool consume_upgrade_response() {
    const std::string text{read_buffer.begin(), read_buffer.end()};
    const arv::usize header_end = text.find("\r\n\r\n");
    if (header_end == std::string::npos ||
        text.find("101 Switching Protocols") == std::string::npos) {
      return false;
    }
    read_buffer.erase(
        read_buffer.begin(),
        read_buffer.begin() + static_cast<arv::isize>(header_end + 4U));
    return true;
  }

  [[nodiscard]] std::vector<std::vector<arv::u8>> drain_binary_messages() {
    read_available();
    std::vector<std::vector<arv::u8>> messages;
    while (read_buffer.size() >= 2U) {
      const arv::u8 opcode = read_buffer[0] & 0x0FU;
      arv::u64 payload_size = read_buffer[1] & 0x7FU;
      arv::usize offset = 2U;
      if (payload_size == 126U) {
        if (read_buffer.size() < offset + 2U) {
          break;
        }
        payload_size = (static_cast<arv::u64>(read_buffer[offset]) << 8U) |
                       static_cast<arv::u64>(read_buffer[offset + 1U]);
        offset += 2U;
      } else if (payload_size == 127U) {
        if (read_buffer.size() < offset + 8U) {
          break;
        }
        payload_size = 0U;
        for (arv::usize i = 0; i < 8U; ++i) {
          payload_size = (payload_size << 8U) |
                         static_cast<arv::u64>(read_buffer[offset + i]);
        }
        offset += 8U;
      }
      if (read_buffer.size() < offset + static_cast<arv::usize>(payload_size)) {
        break;
      }
      if (opcode == 0x2U) {
        messages.emplace_back(
            read_buffer.begin() + static_cast<arv::isize>(offset),
            read_buffer.begin() + static_cast<arv::isize>(offset + payload_size));
      }
      read_buffer.erase(
          read_buffer.begin(),
          read_buffer.begin() + static_cast<arv::isize>(offset + payload_size));
    }
    return messages;
  }

  [[nodiscard]] bool send_bytes(std::span<const arv::u8> bytes) {
    while (!bytes.empty()) {
#ifdef MSG_NOSIGNAL
      const ssize_t sent = ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
#else
      const ssize_t sent = ::send(fd, bytes.data(), bytes.size(), 0);
#endif
      if (sent > 0) {
        bytes = bytes.subspan(static_cast<arv::usize>(sent));
        continue;
      }
      if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      return false;
    }
    return true;
  }

  std::vector<arv::u8> read_buffer{};
};

[[nodiscard]] std::vector<arv::u8> raw_protocol_packet(
    arv::protocol::MessageType type,
    std::span<const arv::u8> payload) {
  return arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       type,
       0,
       0,
       0,
       0},
      payload);
}

[[nodiscard]] bool send_raw_protocol_packet(
    RawWebSocketSocket& socket,
    arv::protocol::MessageType type,
    std::vector<arv::u8> payload) {
  return socket.send_binary(raw_protocol_packet(type, payload));
}

struct WebSocketRuntimeHarness {
  std::unique_ptr<arv::server::ServerRuntime> server{};
  arv::u16 port{};
};

std::optional<WebSocketRuntimeHarness> start_websocket_server_runtime(
    arv::server::ServerStartInfo start_info,
    std::string_view path = "/arvane-test") {
  const auto seed = static_cast<arv::u16>(
      std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
  for (arv::u16 attempt = 0; attempt < 100; ++attempt) {
    const arv::u16 port = static_cast<arv::u16>(47000U + ((seed + attempt) % 1000U));
    auto transport = std::make_unique<arv::net::WebSocketServerTransport>(
        arv::net::WebSocketServerTransportOptions{
            "127.0.0.1",
            port,
            std::string{path},
            1024U * 1024U,
            4U * 1024U * 1024U});
    auto server = std::make_unique<arv::server::ServerRuntime>(std::move(transport));
    start_info.listen_address = {"127.0.0.1", port};
    if (server->start(start_info)) {
      return WebSocketRuntimeHarness{std::move(server), port};
    }
  }
  return std::nullopt;
}

} // namespace

TEST(IntegratedHandshakeTests, ClientAndServerReachInGameThroughProtocolHandshake) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::ProtocolHandshake);

  server.tick();
  client.fixed_tick(server.tick_index());

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(client.camera().world_id(), arv::server::DefaultWorldID);
  EXPECT_EQ(server.sessions().size(), 1U);
  const arv::server::ServerSession* session = server.sessions().first();
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->connection_state, arv::net::ConnectionState::InGame);
  EXPECT_NE(server.worlds().find(arv::server::DefaultWorldID), nullptr);
  EXPECT_TRUE(client.has_authoritative_inventory());
  EXPECT_GT(client.replicated_entity_count(), 0U);
}

TEST(IntegratedHandshakeTests, LargeEntitySnapshotsAreByteBudgetedAcrossTicks) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto scripted_transport = std::make_unique<ScriptedServerTransport>();
  ScriptedServerTransport* transport = scripted_transport.get();
  arv::server::ServerRuntime server(std::move(scripted_transport));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity("SnapshotBudgetTester");
  transport->push_message(arv::net::ConnectionID{1}, raw_client_hello_packet(identity));
  server.tick();

  const arv::server::ServerSession* session = server.sessions().first();
  ASSERT_NE(session, nullptr);
  ASSERT_EQ(session->connection_state, arv::net::ConnectionState::InGame);
  transport->sent.clear();

  std::vector<arv::EntityNetID> created_entities;
  constexpr arv::usize ExtraEntityCount = 800U;
  created_entities.reserve(ExtraEntityCount);
  for (arv::usize index = 0; index < ExtraEntityCount; ++index) {
    arv::world::EntityCreateInfo info;
    info.type_id = arv::EntityTypeID{777U};
    info.type_name = "arvane:test_snapshot_entity";
    info.position = {
        static_cast<arv::f32>(index % 40U),
        64.0F,
        static_cast<arv::f32>(index / 40U)};
    info.owning_player = session->player_id;
    const arv::EntityID entity = server.voxel_world().create_entity(std::move(info));
    const std::optional<arv::EntityNetID> net_id =
        server.voxel_world().network_id_for_entity(entity);
    ASSERT_TRUE(net_id.has_value());
    created_entities.push_back(net_id.value());
  }

  arv::HashSet<arv::EntityNetID> seen_snapshot_entities;
  arv::usize first_tick_snapshot_entities = 0U;
  arv::usize first_tick_snapshot_packets = 0U;
  server.tick();
  for (const arv::net::TransportMessage& message : transport->sent) {
    if (message.message_type != arv::protocol::MessageType::ClientboundEntitySnapshot) {
      continue;
    }
    ++first_tick_snapshot_packets;
    EXPECT_EQ(message.reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
    EXPECT_EQ(message.lane, arv::protocol::NetworkLane::Movement);
    EXPECT_LE(message.payload.size(), 16U * 1024U);
    const auto envelope = arv::protocol::deserialize_envelope(message.payload);
    ASSERT_TRUE(envelope) << envelope.error().message;
    const auto snapshot =
        arv::protocol::deserialize_clientbound_entity_snapshot(envelope.value().payload);
    ASSERT_TRUE(snapshot) << snapshot.error().message;
    first_tick_snapshot_entities += snapshot.value().entities.size();
    for (const arv::protocol::EntitySnapshotEntry& entry : snapshot.value().entities) {
      seen_snapshot_entities.insert(entry.entity_id);
    }
  }
  ASSERT_GT(first_tick_snapshot_packets, 0U);
  EXPECT_LT(first_tick_snapshot_entities, created_entities.size());

  for (int tick = 0; tick < 8 && seen_snapshot_entities.size() < created_entities.size(); ++tick) {
    transport->sent.clear();
    server.tick();
    for (const arv::net::TransportMessage& message : transport->sent) {
      if (message.message_type != arv::protocol::MessageType::ClientboundEntitySnapshot) {
        continue;
      }
      EXPECT_LE(message.payload.size(), 16U * 1024U);
      const auto envelope = arv::protocol::deserialize_envelope(message.payload);
      ASSERT_TRUE(envelope) << envelope.error().message;
      const auto snapshot =
          arv::protocol::deserialize_clientbound_entity_snapshot(envelope.value().payload);
      ASSERT_TRUE(snapshot) << snapshot.error().message;
      for (const arv::protocol::EntitySnapshotEntry& entry : snapshot.value().entities) {
        seen_snapshot_entities.insert(entry.entity_id);
      }
    }
  }

  for (const arv::EntityNetID entity_id : created_entities) {
    EXPECT_NE(seen_snapshot_entities.find(entity_id), seen_snapshot_entities.end());
  }
}

TEST(IntegratedHandshakeTests, PackSyncCompletesBeforeWorldJoin) {
  ScopedTempDir temp("arvane_pack_sync_join_");
  const std::filesystem::path packs = temp.path / "packs";
  write_pack_sync_test_manifest(packs, "sync_pack", "sync_pack");
  write_text(
      packs / "sync_pack/data/sync_pack/blocks.json",
      "{\"blocks\":[]}\n");
  write_text(
      packs / "sync_pack/assets/sync_pack/textures/block/test.png",
      "png");
  auto pack_manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(pack_manager) << pack_manager.error().describe();
  auto bundle = arv::asset::build_client_pack_bundle(
      pack_manager.value(),
      "{\"format_version\":1,\"blocks\":[]}");
  ASSERT_TRUE(bundle) << bundle.error().message;
  const std::filesystem::path bundle_file =
      temp.path / "server-cache" / (bundle.value().bundle_hash + ".apack");
  auto write_bundle =
      arv::asset::write_client_pack_bundle_file(bundle_file, bundle.value());
  ASSERT_TRUE(write_bundle) << write_bundle.error().message;

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair(4096U);
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.pack_sync.enabled = true;
  start_info.pack_sync.registry_hash = bundle.value().registry_hash;
  start_info.pack_sync.registry_snapshot_json = "{\"format_version\":1,\"blocks\":[]}";
  start_info.pack_sync.max_bundle_size =
      static_cast<arv::u64>(bundle.value().apack_bytes.size());
  start_info.pack_sync.required_bundles.push_back({
      arv::protocol::PackBundleDescriptor{
          "sync_pack",
          "Sync Pack",
          "1.0.0",
          bundle.value().bundle_hash,
          static_cast<arv::u64>(bundle.value().apack_bytes.size()),
          bundle.value().uncompressed_size},
      bundle_file});
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity("PackTester");
  identity.arvane_home = temp.path / "client_home";
  const std::filesystem::path stale_partial =
      identity.arvane_home / "cache" / "server-packs" / "tmp" / "stale.partial";
  write_text(stale_partial / "manifest.json", "partial\n");
  client.set_local_identity(std::move(identity));
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::RegistrySync);
  const arv::server::ServerSession* session = server.sessions().first();
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->connection_state, arv::net::ConnectionState::RegistrySync);

  for (int attempt = 0; attempt < 8 &&
                        client.connection_state() != arv::net::ConnectionState::InGame;
       ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
  }

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  ASSERT_TRUE(arv::asset::client_pack_cache_contains(
      (temp.path / "client_home" / "cache" / "server-packs"),
      bundle.value().bundle_hash));
  EXPECT_FALSE(std::filesystem::exists(stale_partial));
  ASSERT_EQ(client.multiplayer_pack_cache_paths().size(), 1U);
  EXPECT_EQ(
      client.multiplayer_pack_cache_paths().front(),
      temp.path / "client_home" / "cache" / "server-packs" / bundle.value().bundle_hash);
  EXPECT_EQ(
      client.multiplayer_registry_snapshot_json(),
      "{\"format_version\":1,\"blocks\":[]}");
  EXPECT_EQ(session->connection_state, arv::net::ConnectionState::InGame);
}

TEST(IntegratedHandshakeTests, ClientChatIsFormattedAndBroadcastByServer) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_local_identity(arv::client::make_ephemeral_local_identity("Hippo"));
  ASSERT_TRUE(client.connect_integrated());
  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  arv::testsupport::connected_player(server)
      .permission_overrides.custom_overrides["arvane.chat.send"] =
      arv::player::TriState::Allow;

  client.submit_input({.open_chat_pressed = true});
  client.submit_input({.text_input = "{\"text\":\"fake admin\"}"});
  client.submit_input({.text_submit_pressed = true});
  server.tick();
  client.fixed_tick(server.tick_index());

  ASSERT_EQ(client.chat_history().size(), 1U);
  EXPECT_EQ(client.chat_history().front().kind, arv::protocol::ChatMessageKind::Chat);
  EXPECT_EQ(client.chat_history().front().channel, "arvane:global");
  EXPECT_EQ(
      arv::text::to_plain_text(client.chat_history().front().message),
      "Hippo: {\"text\":\"fake admin\"}");
  const std::vector<arv::text::RichTextRun> runs =
      arv::text::flatten_rich_text(client.chat_history().front().message);
  ASSERT_GE(runs.size(), 3U);
  EXPECT_TRUE(runs.front().click_event.has_value());
}

TEST(IntegratedHandshakeTests, DuplicateProfileRejectsNewConnectionOnly) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto scripted_transport = std::make_unique<ScriptedServerTransport>();
  ScriptedServerTransport* transport = scripted_transport.get();
  arv::server::ServerRuntime server(std::move(scripted_transport));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity();
  transport->push_message(arv::net::ConnectionID{1}, raw_client_hello_packet(identity));
  transport->push_message(arv::net::ConnectionID{2}, raw_client_hello_packet(identity));

  server.tick();

  EXPECT_EQ(server.sessions().size(), 1U);
  const std::vector<std::pair<arv::net::ConnectionID, std::string>> kicks =
      sent_kick_reasons(transport->sent);
  ASSERT_EQ(kicks.size(), 1U);
  EXPECT_EQ(kicks.front().first, (arv::net::ConnectionID{2}));
  EXPECT_EQ(
      kicks.front().second,
      "This Arvane profile is already connected to this server.");
  ASSERT_EQ(transport->closed.size(), 1U);
  EXPECT_EQ(transport->closed.front().first, (arv::net::ConnectionID{2}));
}

TEST(IntegratedHandshakeTests, ServerShutdownSendsKickBeforeClosingSession) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  (void)client_transport->poll_events();

  server.shutdown();

  const std::vector<std::string> kicks = kick_reasons(client_transport->poll_events());
  ASSERT_EQ(kicks.size(), 1U);
  EXPECT_EQ(kicks.front(), "Server is shutting down.");
}

TEST(IntegratedHandshakeTests, ServerKeepAliveTimeoutKicksClient) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.keep_alive.interval_seconds = 1U;
  start_info.keep_alive.client_timeout_seconds = 1U;
  ASSERT_TRUE(server.start(start_info));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  const std::vector<arv::u64> ids = keep_alive_ids(client_transport->poll_events());
  ASSERT_FALSE(ids.empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  server.tick();

  const std::vector<std::string> kicks = kick_reasons(client_transport->poll_events());
  ASSERT_EQ(kicks.size(), 1U);
  EXPECT_EQ(kicks.front(), "Timed out.");
  EXPECT_EQ(server.sessions().size(), 0U);
}

TEST(IntegratedHandshakeTests, DisabledServerKeepAliveDoesNotKickClient) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.keep_alive.interval_seconds = 1U;
  start_info.keep_alive.client_timeout_seconds = 1U;
  start_info.keep_alive_enabled = false;
  ASSERT_TRUE(server.start(start_info));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  EXPECT_TRUE(keep_alive_ids(client_transport->poll_events()).empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  server.tick();

  EXPECT_EQ(server.sessions().size(), 1U);
  EXPECT_TRUE(kick_reasons(client_transport->poll_events()).empty());
}

TEST(IntegratedHandshakeTests, KeepAliveReplyPreventsServerTimeout) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.keep_alive.interval_seconds = 1U;
  start_info.keep_alive.client_timeout_seconds = 1U;
  ASSERT_TRUE(server.start(start_info));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  const std::vector<arv::u64> ids = keep_alive_ids(client_transport->poll_events());
  ASSERT_FALSE(ids.empty());
  send_raw_keep_alive(*client_transport, connect_result.value(), ids.back());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  server.tick();

  EXPECT_EQ(server.sessions().size(), 1U);
  EXPECT_TRUE(kick_reasons(client_transport->poll_events()).empty());
}

TEST(IntegratedHandshakeTests, ServerSendsMovementHzOverrideWhenConfigured) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.movement.send_rate_hz = 30.0F;
  ASSERT_TRUE(server.start(start_info));

  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity();
  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(*client_transport, connect_result.value(), identity);
  server.tick();

  const std::vector<arv::protocol::ClientboundSetMovementHz> packets =
      movement_hz_packets(client_transport->poll_events());
  ASSERT_EQ(packets.size(), 1U);
  EXPECT_FLOAT_EQ(packets.front().movement_hz, 30.0F);
}

TEST(IntegratedHandshakeTests, SavedPlayerStateIsAppliedBeforeWorldJoinResult) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));

  arv::server::ServerPlayerSaveState saved_player;
  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity();
  saved_player.profile_id = identity.profile_id;
  saved_player.profile_name = "local_player";
  saved_player.world_name = "default";
  saved_player.position = {12.0F, 34.0F, -56.0F};
  saved_player.velocity = {1.0F, 2.0F, 3.0F};
  saved_player.respawn_location =
      arv::Location{20.0F, 65.0F, 4.0F, 0.25F, -0.125F};
  saved_player.selected_hotbar_slot = 3;
  std::array<arv::server::ServerPlayerSaveState, 1> saved_players{saved_player};

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.initial_tick_index = 77;
  start_info.world_name = "default";
  start_info.saved_players = saved_players;
  ASSERT_TRUE(server.start(start_info));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(*client_transport, connect_result.value(), identity);
  server.tick();

  const std::vector<arv::net::NetworkEvent> events = client_transport->poll_events();
  std::optional<arv::protocol::ClientboundWorldJoinResult> world_join;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    auto envelope = arv::protocol::deserialize_envelope(event.payload);
    if (!envelope ||
        envelope.value().envelope.message_type != arv::protocol::MessageType::ClientboundWorldJoinResult) {
      continue;
    }
    auto decoded = arv::protocol::deserialize_clientbound_world_join_result(envelope.value().payload);
    ASSERT_TRUE(decoded);
    world_join = decoded.value();
  }

  ASSERT_TRUE(world_join.has_value());
  EXPECT_TRUE(world_join->accepted);
  EXPECT_EQ(world_join->initial_tick, 77U);
  EXPECT_EQ(world_join->spawn_position.x, 12.0F);
  EXPECT_EQ(world_join->spawn_position.y, 34.0F);
  EXPECT_EQ(world_join->spawn_position.z, -56.0F);
  const arv::player::PlayerState& player = connected_player(server);
  ASSERT_TRUE(player.respawn_location.has_value());
  EXPECT_EQ(
      arv::location_position(*player.respawn_location),
      (arv::Vec3f{20.0F, 65.0F, 4.0F}));
  EXPECT_EQ(player.selected_hotbar_slot, 3U);
}

TEST(IntegratedHandshakeTests, InventoryActionsFlowThroughAuthoritativeProtocolUpdates) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID bucket_item =
      item_registry.find_item("arvane:bucket").value();
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_TRUE(client.has_authoritative_inventory());
  arv::player::PlayerState& player = connected_player(server);
  ASSERT_TRUE(server.insert_player_inventory_stack(
      player.player_id,
      arv::player::ItemStack{bucket_item, 1U}).complete());
  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_TRUE(client.inventory().slot(0).has_value());

  ASSERT_TRUE(client.request_inventory_slot_click(
      0,
      arv::player::InventoryClickButton::Primary));
  server.tick();
  client.fixed_tick(server.tick_index());

  ASSERT_TRUE(player.inventory_cursor_stack.has_value());
  EXPECT_FALSE(player.inventory.slot(0).has_value());
  ASSERT_TRUE(client.inventory_cursor_stack().has_value());
  EXPECT_FALSE(client.inventory().slot(0).has_value());
  EXPECT_GE(client.inventory_revision(), 2U);
}

TEST(IntegratedHandshakeTests, PausedNetworkPumpProcessesInventoryActionsWithoutTickingWorld) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID bucket_item =
      item_registry.find_item("arvane:bucket").value();
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_TRUE(client.has_authoritative_inventory());
  arv::player::PlayerState& player = connected_player(server);
  ASSERT_TRUE(server.insert_player_inventory_stack(
      player.player_id,
      arv::player::ItemStack{bucket_item, 1U}).complete());
  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_TRUE(client.inventory().slot(0).has_value());

  const arv::TickIndex tick_before_click = server.tick_index();
  ASSERT_TRUE(client.request_inventory_slot_click(
      0,
      arv::player::InventoryClickButton::Primary));
  server.pump_integrated_network_frame();
  client.poll_network();

  EXPECT_EQ(server.tick_index(), tick_before_click);
  ASSERT_TRUE(player.inventory_cursor_stack.has_value());
  EXPECT_FALSE(player.inventory.slot(0).has_value());
  ASSERT_TRUE(client.inventory_cursor_stack().has_value());
  EXPECT_FALSE(client.inventory().slot(0).has_value());
}

TEST(IntegratedHandshakeTests, BlockUpdatesReplicateAfterChunkStreaming) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID dirt =
      block_registry.find_variant("arvane:dirt").value();
  const arv::voxel::FluidDefinition water =
      arv::voxel::make_water_fluid_definition();
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry,
                            arv::player::default_player_tuning(),
                            water,
                            streaming_config}));
  server.voxel_world().configure_height({1, 1});

  const arv::ChunkSectionCoord expected_chunk{{0, 1, -2}};
  (void)server.voxel_world().ensure_chunk(expected_chunk);
  const arv::BlockPos changed_block =
      arv::chunk_local_to_block(expected_chunk, {{1, 2, 3}});
  ASSERT_TRUE(server.voxel_world().set_block(changed_block, stone));
  (void)server.voxel_world().drain_replicated_block_updates();

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.drain_received_chunk_section_data().size(), 1U);

  ASSERT_TRUE(server.voxel_world().set_block(changed_block, dirt));
  server.tick();
  client.fixed_tick(server.tick_index());
  std::vector<arv::protocol::ClientboundBlockUpdateBatch> updates =
      client.drain_received_block_updates();
  ASSERT_EQ(updates.size(), 1U);
  ASSERT_EQ(updates.front().updates.size(), 1U);
  EXPECT_EQ(updates.front().world_id, arv::server::DefaultWorldID);
  EXPECT_EQ(updates.front().updates.front().pos, changed_block);
  EXPECT_EQ(updates.front().updates.front().new_state, dirt);
  EXPECT_FALSE(updates.front().updates.front().has_fluid);

  const arv::BlockPos changed_fluid =
      arv::chunk_local_to_block(expected_chunk, {{2, 2, 3}});
  ASSERT_TRUE(server.voxel_world().place_fluid_source(changed_fluid, water));
  server.tick();
  client.fixed_tick(server.tick_index());
  updates = client.drain_received_block_updates();
  ASSERT_EQ(updates.size(), 1U);
  ASSERT_EQ(updates.front().updates.size(), 1U);
  EXPECT_EQ(updates.front().updates.front().pos, changed_fluid);
  EXPECT_TRUE(updates.front().updates.front().has_fluid);
  EXPECT_EQ(
      updates.front().updates.front().new_fluid,
      arv::voxel::make_fluid_source(water));
}

TEST(IntegratedHandshakeTests, DroppedItemsReplicateAfterRelevantChunkStreaming) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item = item_registry.find_item("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry,
                            arv::player::default_player_tuning(),
                            arv::voxel::make_water_fluid_definition(),
                            streaming_config}));
  server.voxel_world().configure_height({1, 1});

  const arv::ChunkSectionCoord spawn_chunk{{0, 1, -2}};
  (void)server.voxel_world().ensure_chunk(spawn_chunk);

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  for (int tick = 0; tick < 4 && client.dropped_item_count() == 0U; ++tick) {
    server.tick();
    client.fixed_tick(server.tick_index());
    (void)client.drain_received_chunk_section_data();
  }

  arv::player::PlayerState& player = connected_player(server);
  player.inventory_cursor_stack = arv::player::ItemStack{stone_item, 2};
  ASSERT_TRUE(server.drop_player_inventory_cursor(player.player_id).has_value());

  server.tick();
  client.fixed_tick(server.tick_index());

  ASSERT_EQ(client.dropped_item_count(), 1U);
  const std::vector<arv::client::DroppedItemMirror> drops = client.dropped_item_mirrors();
  ASSERT_EQ(drops.size(), 1U);
  EXPECT_EQ(drops.front().stack, (arv::player::ItemStack{stone_item, 2}));

  const std::vector<arv::entity::DroppedItemEntitySnapshot> server_drops =
      server.voxel_world().collect_dropped_item_snapshots();
  ASSERT_EQ(server_drops.size(), 1U);
  ASSERT_TRUE(server.voxel_world().destroy_entity(server_drops.front().entity_id));
  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(server.dropped_item_stacks().empty());
  EXPECT_EQ(client.dropped_item_count(), 0U);
}

TEST(IntegratedHandshakeTests, DisconnectCleansSessionAndWorldLocalPlayerEntity) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());

  server.tick();
  (void)client_transport->poll_events();
  ASSERT_EQ(server.sessions().size(), 1U);
  EXPECT_EQ(server.voxel_world().player_count(), 1U);
  EXPECT_EQ(server.voxel_world().entity_count(), 1U);

  client_transport->shutdown();
  server.tick();

  EXPECT_EQ(server.sessions().size(), 0U);
  EXPECT_EQ(server.voxel_world().player_count(), 0U);
  EXPECT_EQ(server.voxel_world().entity_count(), 0U);
}

TEST(IntegratedHandshakeTests, ClientMovementPacketUpdatesServerHotbarSlot) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);

  client.submit_input(arv::client::InputFrame{.hotbar_select = 4});
  client.fixed_tick(server.tick_index());

  server.tick();
  EXPECT_EQ(connected_player(server).selected_hotbar_slot, 4U);
}

TEST(IntegratedHandshakeTests, RejectedPlayerMoveSendsReliableCorrection) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> client_transport = std::move(transports.client);
  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry}));

  const auto connect_result = client_transport->connect({"in-process", 0});
  ASSERT_TRUE(connect_result);
  send_raw_client_hello(
      *client_transport,
      connect_result.value(),
      arv::client::make_ephemeral_local_identity());
  server.tick();
  (void)client_transport->poll_events();

  arv::protocol::ServerboundPlayerMove move;
  move.sequence = 1;
  move.client_tick = 1;
  move.position = {1000.0F, 20.0F, 1000.0F};
  move.flags = arv::protocol::PlayerMoveFlags::OnGround;
  const std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion,
       arv::protocol::MessageType::ServerboundPlayerMove,
       0,
       0,
       0,
       0},
      arv::protocol::serialize_serverbound_player_move(move));
  ASSERT_TRUE(client_transport->send(
      {connect_result.value(),
       arv::protocol::ReliabilityClass::UnreliableLatestWins,
       packet,
       arv::protocol::MessageType::ServerboundPlayerMove,
       arv::protocol::NetworkLane::Movement}));

  server.tick();
  const std::vector<arv::net::NetworkEvent> events = client_transport->poll_events();
  EXPECT_EQ(player_correction_count(events), 1U);
}

TEST(IntegratedHandshakeTests, ClientReceivesChunkDataOnlyAfterInterestUpdate) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry,
                            arv::player::default_player_tuning(),
                            arv::voxel::make_water_fluid_definition(),
                            streaming_config}));
  server.voxel_world().configure_height({1, 1});

  const arv::ChunkSectionCoord expected_chunk{{0, 1, -2}};
  (void)server.voxel_world().ensure_chunk(expected_chunk);
  ASSERT_TRUE(server.voxel_world().set_block(
      arv::chunk_local_to_block(expected_chunk, {{1, 2, 3}}),
      stone));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  std::vector<arv::protocol::ClientboundChunkSectionData> chunks =
      client.drain_received_chunk_section_data();
  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
  EXPECT_EQ(chunks.front().coord, expected_chunk);
  EXPECT_EQ(
      chunks.front().blocks[arv::voxel::local_index({{1, 2, 3}})],
      stone);

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());
}

TEST(IntegratedHandshakeTests, ClientReceivesGeneratedChunkAfterAsyncGeneration) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  ASSERT_TRUE(server.start({arv::server::ServerMode::Integrated,
                            arv::DefaultBaseTps,
                            {"in-process", 0},
                            &block_registry,
                            &item_registry,
                            arv::player::default_player_tuning(),
                            arv::voxel::make_water_fluid_definition(),
                            streaming_config,
                            make_generation_settings(block_registry)}));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  server.tick();
  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  std::vector<arv::protocol::ClientboundChunkSectionData> chunks;
  for (int attempt = 0; attempt < 100 && chunks.empty(); ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    chunks = client.drain_received_chunk_section_data();
    if (chunks.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
}

TEST(IntegratedHandshakeTests, IntegratedFramePumpStreamsGeneratedChunkWithoutExtraFixedTick) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;
  streaming_config.integrated_frame_pump_enabled = true;
  streaming_config.max_ready_chunks_sent_per_player_frame = 1;
  streaming_config.max_missing_columns_requested_per_player_tick = 1;
  streaming_config.max_generation_results_per_frame = 1;
  streaming_config.max_generation_schedules_per_frame = 1;

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();
  start_info.chunk_streaming = streaming_config;
  start_info.chunk_generation = make_generation_settings(block_registry);
  start_info.max_pending_generation_jobs = 1;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect_integrated());

  for (int tick = 0; tick < 3; ++tick) {
    server.tick();
    client.fixed_tick(server.tick_index());
    EXPECT_TRUE(client.drain_received_chunk_section_data().empty());
  }

  server.pump_chunk_streaming_frame(false);
  client.poll_network();
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  server.pump_chunk_streaming_frame();
  client.poll_network();
  EXPECT_TRUE(client.drain_received_chunk_section_data().empty());

  std::vector<arv::protocol::ClientboundChunkSectionData> chunks;
  for (int attempt = 0; attempt < 300 && chunks.empty(); ++attempt) {
    server.pump_chunk_streaming_frame();
    client.poll_network();
    chunks = client.drain_received_chunk_section_data();
    if (chunks.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
}

TEST(IntegratedHandshakeTests, RemoteConnectPathReceivesGeneratedChunks) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Dedicated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();
  start_info.chunk_streaming = streaming_config;
  start_info.chunk_generation = make_generation_settings(block_registry);
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_chunk_view_distance(0);
  ASSERT_TRUE(client.connect({"localhost", 42069}));

  std::vector<arv::protocol::ClientboundChunkSectionData> chunks;
  for (int attempt = 0; attempt < 100 && chunks.empty(); ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    chunks = client.drain_received_chunk_section_data();
    if (chunks.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks.front().world_id, arv::server::DefaultWorldID);
}

TEST(IntegratedHandshakeTests, WebSocketLoopbackRunsServerProtocolHandshake) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Dedicated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();

  std::optional<WebSocketRuntimeHarness> harness =
      start_websocket_server_runtime(start_info);
  ASSERT_TRUE(harness.has_value());

  RawWebSocketSocket client;
  ASSERT_TRUE(client.connect(harness->port));
  ASSERT_TRUE(client.send_text(
      "GET /arvane-test HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n"));

  bool upgraded = false;
  for (int attempt = 0; attempt < 100 && !upgraded; ++attempt) {
    harness->server->tick();
    client.read_available();
    upgraded = client.consume_upgrade_response();
    if (!upgraded) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  ASSERT_TRUE(upgraded);

  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity("WebSocketTester");
  ASSERT_TRUE(client.send_binary(raw_client_hello_packet(identity)));

  bool saw_clientbound_hello = false;
  bool sent_auth_response = false;
  bool saw_world_join = false;
  for (int attempt = 0; attempt < 100 && (!saw_clientbound_hello || !saw_world_join);
       ++attempt) {
    harness->server->tick();
    for (const std::vector<arv::u8>& message : client.drain_binary_messages()) {
      const auto decoded = arv::protocol::deserialize_envelope(message);
      ASSERT_TRUE(decoded) << decoded.error().message;
      if (decoded.value().envelope.message_type ==
          arv::protocol::MessageType::ClientboundHello) {
        saw_clientbound_hello = true;
      }
      if (decoded.value().envelope.message_type ==
          arv::protocol::MessageType::ClientboundAuthChallenge) {
        const auto challenge =
            arv::protocol::deserialize_auth_challenge(decoded.value().payload);
        ASSERT_TRUE(challenge) << challenge.error().message;
        if (!sent_auth_response) {
          ASSERT_TRUE(client.send_binary(raw_auth_response_packet(identity, challenge.value())));
          sent_auth_response = true;
        }
      }
      if (decoded.value().envelope.message_type ==
          arv::protocol::MessageType::ClientboundWorldJoinResult) {
        const auto world_join =
            arv::protocol::deserialize_clientbound_world_join_result(decoded.value().payload);
        ASSERT_TRUE(world_join) << world_join.error().message;
        EXPECT_TRUE(world_join.value().accepted);
        saw_world_join = true;
      }
    }
    if (!saw_clientbound_hello || !saw_world_join) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_TRUE(saw_clientbound_hello);
  EXPECT_TRUE(saw_world_join);
  ASSERT_EQ(harness->server->sessions().size(), 1U);
  const arv::server::ServerSession* session = harness->server->sessions().first();
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->connection_state, arv::net::ConnectionState::InGame);
}

TEST(IntegratedHandshakeTests, WebSocketLoopbackCompletesPackSyncStreamsChunksAndRejectsInvalidMovement) {
  ScopedTempDir temp("arvane_ws_pack_sync_");
  const std::filesystem::path packs = temp.path / "packs";
  write_pack_sync_test_manifest(packs, "sync_pack", "sync_pack");
  write_text(
      packs / "sync_pack/data/sync_pack/blocks.json",
      "{\"blocks\":[]}\n");
  write_text(
      packs / "sync_pack/assets/sync_pack/textures/block/test.png",
      "png");
  auto pack_manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(pack_manager) << pack_manager.error().describe();
  constexpr std::string_view RegistrySnapshot = "{\"format_version\":1,\"blocks\":[]}";
  auto bundle = arv::asset::build_client_pack_bundle(
      pack_manager.value(),
      std::string{RegistrySnapshot});
  ASSERT_TRUE(bundle) << bundle.error().message;
  const auto& built_bundle = bundle.value();
  const std::filesystem::path bundle_file =
      temp.path / "server-cache" / (built_bundle.bundle_hash + ".apack");
  auto write_bundle =
      arv::asset::write_client_pack_bundle_file(bundle_file, built_bundle);
  ASSERT_TRUE(write_bundle) << write_bundle.error().message;

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Dedicated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();
  start_info.chunk_streaming = streaming_config;
  start_info.chunk_generation = make_generation_settings(block_registry);
  start_info.max_generation_results_per_tick = 2;
  start_info.max_pending_generation_jobs = 4;
  start_info.pack_sync.enabled = true;
  start_info.pack_sync.registry_hash = built_bundle.registry_hash;
  start_info.pack_sync.registry_snapshot_json = std::string{RegistrySnapshot};
  start_info.pack_sync.max_bundle_size =
      static_cast<arv::u64>(built_bundle.apack_bytes.size());
  start_info.pack_sync.required_bundles.push_back({
      arv::protocol::PackBundleDescriptor{
          "sync_pack",
          "Sync Pack",
          "1.0.0",
          built_bundle.bundle_hash,
          static_cast<arv::u64>(built_bundle.apack_bytes.size()),
          built_bundle.uncompressed_size},
      bundle_file});

  std::optional<WebSocketRuntimeHarness> harness =
      start_websocket_server_runtime(start_info);
  ASSERT_TRUE(harness.has_value());

  RawWebSocketSocket client;
  ASSERT_TRUE(client.connect(harness->port));
  ASSERT_TRUE(client.send_text(
      "GET /arvane-test HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n"));

  bool upgraded = false;
  for (int attempt = 0; attempt < 100 && !upgraded; ++attempt) {
    harness->server->tick();
    client.read_available();
    upgraded = client.consume_upgrade_response();
    if (!upgraded) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  ASSERT_TRUE(upgraded);

  const arv::client::LocalPlayerIdentity identity =
      arv::client::make_ephemeral_local_identity("WebSocketPackTester");
  ASSERT_TRUE(client.send_binary(raw_client_hello_packet(identity)));

  std::vector<arv::u8> downloaded_bundle(built_bundle.apack_bytes.size());
  std::vector<bool> received_bundle_chunks;
  arv::u32 received_bundle_chunk_count = 0;
  arv::u64 received_bundle_bytes = 0;
  bool saw_clientbound_hello = false;
  bool sent_auth_response = false;
  bool saw_pack_manifest = false;
  bool sent_download_request = false;
  bool sent_loaded_status = false;
  bool saw_registry_snapshot = false;
  bool sent_pack_ready = false;
  bool sent_chunk_interest = false;
  bool sent_invalid_move = false;
  bool saw_chunk_data = false;
  bool saw_movement_correction = false;
  std::optional<arv::protocol::ClientboundWorldJoinResult> world_join;

  for (int attempt = 0;
       attempt < 1500 && (!world_join.has_value() || !saw_chunk_data || !saw_movement_correction);
       ++attempt) {
    harness->server->tick();
    for (const std::vector<arv::u8>& message : client.drain_binary_messages()) {
      const auto decoded = arv::protocol::deserialize_envelope(message);
      ASSERT_TRUE(decoded) << decoded.error().message;
      switch (decoded.value().envelope.message_type) {
      case arv::protocol::MessageType::ClientboundHello:
        saw_clientbound_hello = true;
        break;
      case arv::protocol::MessageType::ClientboundAuthChallenge: {
        const auto challenge =
            arv::protocol::deserialize_auth_challenge(decoded.value().payload);
        ASSERT_TRUE(challenge) << challenge.error().message;
        if (!sent_auth_response) {
          ASSERT_TRUE(client.send_binary(raw_auth_response_packet(identity, challenge.value())));
          sent_auth_response = true;
        }
        break;
      }
      case arv::protocol::MessageType::ClientboundPackManifest: {
        const auto manifest =
            arv::protocol::deserialize_clientbound_pack_manifest(decoded.value().payload);
        ASSERT_TRUE(manifest) << manifest.error().message;
        ASSERT_EQ(manifest.value().required_bundles.size(), 1U);
        EXPECT_EQ(manifest.value().registry_hash, built_bundle.registry_hash);
        EXPECT_EQ(manifest.value().required_bundles.front().bundle_hash, built_bundle.bundle_hash);
        saw_pack_manifest = true;
        if (!sent_download_request) {
          arv::protocol::ServerboundPackSyncStatus status;
          status.status = arv::protocol::PackSyncStatus::NeedsDownload;
          status.needed_bundle_hashes.push_back(built_bundle.bundle_hash);
          ASSERT_TRUE(send_raw_protocol_packet(
              client,
              arv::protocol::MessageType::ServerboundPackSyncStatus,
              arv::protocol::serialize_serverbound_pack_sync_status(status)));
          ASSERT_TRUE(send_raw_protocol_packet(
              client,
              arv::protocol::MessageType::ServerboundPackRequest,
              arv::protocol::serialize_serverbound_pack_request({built_bundle.bundle_hash})));
          sent_download_request = true;
        }
        break;
      }
      case arv::protocol::MessageType::ClientboundPackDataChunk: {
        const auto chunk =
            arv::protocol::deserialize_clientbound_pack_data_chunk(decoded.value().payload);
        ASSERT_TRUE(chunk) << chunk.error().message;
        EXPECT_EQ(chunk.value().bundle_hash, built_bundle.bundle_hash);
        ASSERT_NE(chunk.value().chunk_count, 0U);
        ASSERT_LT(chunk.value().chunk_index, chunk.value().chunk_count);
        ASSERT_LE(
            chunk.value().byte_offset + static_cast<arv::u64>(chunk.value().bytes.size()),
            static_cast<arv::u64>(downloaded_bundle.size()));
        if (received_bundle_chunks.empty()) {
          received_bundle_chunks.assign(chunk.value().chunk_count, false);
        }
        ASSERT_EQ(received_bundle_chunks.size(), chunk.value().chunk_count);
        if (!received_bundle_chunks[chunk.value().chunk_index]) {
          std::copy(
              chunk.value().bytes.begin(),
              chunk.value().bytes.end(),
              downloaded_bundle.begin() +
                  static_cast<std::ptrdiff_t>(chunk.value().byte_offset));
          received_bundle_chunks[chunk.value().chunk_index] = true;
          ++received_bundle_chunk_count;
          received_bundle_bytes += chunk.value().bytes.size();
        }
        if (!sent_loaded_status &&
            received_bundle_chunk_count == received_bundle_chunks.size() &&
            received_bundle_bytes == downloaded_bundle.size()) {
          EXPECT_EQ(downloaded_bundle, built_bundle.apack_bytes);
          arv::protocol::ServerboundPackSyncStatus status;
          status.status = arv::protocol::PackSyncStatus::Loaded;
          status.already_cached_bundle_hashes.push_back(built_bundle.bundle_hash);
          ASSERT_TRUE(send_raw_protocol_packet(
              client,
              arv::protocol::MessageType::ServerboundPackSyncStatus,
              arv::protocol::serialize_serverbound_pack_sync_status(status)));
          sent_loaded_status = true;
        }
        break;
      }
      case arv::protocol::MessageType::ClientboundRegistrySnapshot: {
        const auto snapshot =
            arv::protocol::deserialize_clientbound_registry_snapshot(decoded.value().payload);
        ASSERT_TRUE(snapshot) << snapshot.error().message;
        EXPECT_EQ(snapshot.value().registry_hash, built_bundle.registry_hash);
        EXPECT_EQ(snapshot.value().registry_snapshot_payload, RegistrySnapshot);
        saw_registry_snapshot = true;
        if (!sent_pack_ready) {
          arv::protocol::ServerboundPackReady ready;
          ready.registry_hash = snapshot.value().registry_hash;
          ready.loaded_bundle_hashes.push_back(built_bundle.bundle_hash);
          ASSERT_TRUE(send_raw_protocol_packet(
              client,
              arv::protocol::MessageType::ServerboundPackReady,
              arv::protocol::serialize_serverbound_pack_ready(ready)));
          sent_pack_ready = true;
        }
        break;
      }
      case arv::protocol::MessageType::ClientboundWorldJoinResult: {
        const auto result =
            arv::protocol::deserialize_clientbound_world_join_result(decoded.value().payload);
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_TRUE(result.value().accepted);
        world_join = result.value();
        if (!sent_chunk_interest) {
          const arv::ChunkSectionCoord center{{
              arv::floor_div(
                  static_cast<arv::i32>(std::floor(result.value().spawn_position.x)),
                  arv::ChunkSize),
              arv::floor_div(
                  static_cast<arv::i32>(std::floor(result.value().spawn_position.y)),
                  arv::ChunkSize),
              arv::floor_div(
                  static_cast<arv::i32>(std::floor(result.value().spawn_position.z)),
                  arv::ChunkSize),
          }};
          arv::protocol::ServerboundChunkInterestUpdate update;
          update.world_id = result.value().world_id;
          update.center_chunk = center;
          update.client_view_distance = 0;
          update.sequence_id = 1;
          ASSERT_TRUE(send_raw_protocol_packet(
              client,
              arv::protocol::MessageType::ServerboundChunkInterestUpdate,
              arv::protocol::serialize_serverbound_chunk_interest_update(update)));
          sent_chunk_interest = true;
        }
        if (!sent_invalid_move) {
          arv::protocol::ServerboundPlayerMove move;
          move.sequence = 1;
          move.client_tick = 1;
          move.position = {1000.0F, 20.0F, 1000.0F};
          move.flags = arv::protocol::PlayerMoveFlags::OnGround;
          ASSERT_TRUE(send_raw_protocol_packet(
              client,
              arv::protocol::MessageType::ServerboundPlayerMove,
              arv::protocol::serialize_serverbound_player_move(move)));
          sent_invalid_move = true;
        }
        break;
      }
      case arv::protocol::MessageType::ClientboundChunkSectionData: {
        const auto chunk =
            arv::protocol::deserialize_clientbound_chunk_section_data(decoded.value().payload);
        ASSERT_TRUE(chunk) << chunk.error().message;
        ASSERT_TRUE(world_join.has_value());
        EXPECT_EQ(chunk.value().world_id, world_join->world_id);
        saw_chunk_data = true;
        break;
      }
      case arv::protocol::MessageType::ClientboundPlayerMoveCorrection:
        saw_movement_correction = true;
        break;
      default:
        break;
      }
    }
    if (!world_join.has_value() || !saw_chunk_data || !saw_movement_correction) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_TRUE(saw_clientbound_hello);
  EXPECT_TRUE(saw_pack_manifest);
  EXPECT_TRUE(sent_download_request);
  EXPECT_TRUE(sent_loaded_status);
  EXPECT_TRUE(saw_registry_snapshot);
  EXPECT_TRUE(sent_pack_ready);
  EXPECT_TRUE(world_join.has_value());
  EXPECT_TRUE(sent_chunk_interest);
  EXPECT_TRUE(saw_chunk_data);
  EXPECT_TRUE(sent_invalid_move);
  EXPECT_TRUE(saw_movement_correction);
  ASSERT_EQ(harness->server->sessions().size(), 1U);
  const arv::server::ServerSession* session = harness->server->sessions().first();
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->connection_state, arv::net::ConnectionState::InGame);
  EXPECT_GT(harness->server->voxel_world().loaded_chunk_count(), 0U);
}

TEST(IntegratedHandshakeTests, GameNetworkingSocketsLoopbackReceivesGeneratedChunks) {
  arv::net::GameNetworkingSocketsEnvironment environment;
  if (!environment.initialized()) {
    GTEST_SKIP() << environment.error_message();
  }

  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto server_transport = std::make_unique<arv::net::GameNetworkingSocketsTransport>();
  arv::net::GameNetworkingSocketsTransport* server_transport_view = server_transport.get();
  arv::server::ServerRuntime server(std::move(server_transport));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 0;
  streaming_config.max_chunks_sent_per_player_tick = 1;

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Dedicated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();
  start_info.chunk_streaming = streaming_config;
  start_info.chunk_generation = make_generation_settings(block_registry);
  const std::optional<arv::u16> port = start_gns_server_runtime(server, start_info);
  if (!port.has_value()) {
    GTEST_SKIP() << "failed to create GameNetworkingSockets listen socket";
  }
  ASSERT_EQ(server_transport_view->bound_port(), port.value());

  arv::client::ClientRuntime client(
      std::make_unique<arv::net::GameNetworkingSocketsTransport>());
  client.set_chunk_view_distance(0);
  const auto connect_result = client.connect({"localhost", port.value()});
  if (!connect_result) {
    GTEST_SKIP() << connect_result.error().message;
  }

  std::vector<arv::protocol::ClientboundChunkSectionData> chunks;
  for (int attempt = 0; attempt < 300 && chunks.empty(); ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    chunks = client.drain_received_chunk_section_data();
    if (chunks.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(server.sessions().size(), 1U);
  ASSERT_GE(chunks.size(), 1U);
  for (const arv::protocol::ClientboundChunkSectionData& chunk : chunks) {
    EXPECT_EQ(chunk.world_id, arv::server::DefaultWorldID);
  }
  EXPECT_GT(server.voxel_world().loaded_chunk_count(), 0U);

  arv::player::PlayerState& player = connected_player(server);
  const arv::player::ItemID stone_item = item_registry.find_item("arvane:stone").value();
  player.inventory_cursor_stack = arv::player::ItemStack{stone_item, 1};
  ASSERT_TRUE(server.drop_player_inventory_cursor(player.player_id).has_value());

  for (int attempt = 0; attempt < 300 && client.dropped_item_count() == 0U; ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    if (client.dropped_item_count() == 0U) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ASSERT_EQ(client.dropped_item_count(), 1U);
  EXPECT_EQ(client.dropped_item_mirrors().front().stack.item, stone_item);
}

TEST(IntegratedHandshakeTests, GameNetworkingSocketsLoopbackStreamsViewDistanceOne) {
  arv::net::GameNetworkingSocketsEnvironment environment;
  if (!environment.initialized()) {
    GTEST_SKIP() << environment.error_message();
  }

  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto server_transport = std::make_unique<arv::net::GameNetworkingSocketsTransport>();
  arv::server::ServerRuntime server(std::move(server_transport));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.max_view_distance = 1;
  streaming_config.max_chunks_sent_per_player_tick = 2;

  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Dedicated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = arv::voxel::make_water_fluid_definition();
  start_info.chunk_streaming = streaming_config;
  start_info.chunk_generation = make_generation_settings(block_registry);
  start_info.max_generation_results_per_tick = 2;
  start_info.max_pending_generation_jobs = 4;
  const std::optional<arv::u16> port = start_gns_server_runtime(server, start_info);
  if (!port.has_value()) {
    GTEST_SKIP() << "failed to create GameNetworkingSockets listen socket";
  }

  arv::client::ClientRuntime client(
      std::make_unique<arv::net::GameNetworkingSocketsTransport>());
  client.set_chunk_view_distance(1);
  const auto connect_result = client.connect({"localhost", port.value()});
  if (!connect_result) {
    GTEST_SKIP() << connect_result.error().message;
  }

  arv::HashSet<arv::ChunkSectionCoord> received_chunks;
  for (int attempt = 0; attempt < 1500 && received_chunks.size() < 27U; ++attempt) {
    server.tick();
    client.fixed_tick(server.tick_index());
    for (const arv::protocol::ClientboundChunkSectionData& chunk :
         client.drain_received_chunk_section_data()) {
      if (chunk.world_id == arv::server::DefaultWorldID) {
        received_chunks.insert(chunk.coord);
      }
    }
    if (received_chunks.size() < 27U) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(server.sessions().size(), 1U);
  EXPECT_GE(received_chunks.size(), 27U);
  EXPECT_GE(server.voxel_world().loaded_chunk_count(), 27U);
}
