#pragma once

#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/world/world_runtime.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace arv::server {

enum class ServerMode {
  Integrated,
  Dedicated,
};

enum class ServerRuntimeState {
  Stopped,
  Running,
  Stopping,
};

struct ServerStartInfo {
  ServerMode mode{ServerMode::Integrated};
  TickRate base_tps{DefaultBaseTps};
  net::ListenAddress listen_address{};
};

class ServerRuntime {
public:
  explicit ServerRuntime(std::unique_ptr<net::ITransport> transport)
      : transport_(std::move(transport)) {}

  net::NetworkResult start(const ServerStartInfo& start_info) {
    if (!transport_) {
      return net::NetworkResult::failure(net::NetworkError{"server has no transport"});
    }

    mode_ = start_info.mode;
    base_tps_ = start_info.base_tps;
    worlds_.ensure_default_world();
    handshake_policy_.server_name =
        mode_ == ServerMode::Dedicated ? "Arvane Dedicated Server" : "Arvane Integrated Server";
    handshake_policy_.initial_world_id = DefaultWorldID;

    auto listen_result = transport_->start_listening(start_info.listen_address);
    if (!listen_result) {
      return listen_result;
    }

    state_ = ServerRuntimeState::Running;
    return net::NetworkResult::success();
  }

  void tick() {
    if (state_ != ServerRuntimeState::Running || !transport_) {
      return;
    }

    inbound_events_ = transport_->poll_events();
    for (net::NetworkEvent& event : inbound_events_) {
      handle_network_event(std::move(event));
    }
    ++tick_index_;
  }

  void shutdown() {
    if (state_ == ServerRuntimeState::Stopped) {
      return;
    }

    state_ = ServerRuntimeState::Stopping;
    if (transport_) {
      transport_->shutdown();
    }
    state_ = ServerRuntimeState::Stopped;
  }

  [[nodiscard]] ServerRuntimeState state() const noexcept {
    return state_;
  }

  [[nodiscard]] TickIndex tick_index() const noexcept {
    return tick_index_;
  }

  [[nodiscard]] TickRate base_tps() const noexcept {
    return base_tps_;
  }

  [[nodiscard]] ServerMode mode() const noexcept {
    return mode_;
  }

  [[nodiscard]] const SessionRegistry& sessions() const noexcept {
    return sessions_;
  }

  [[nodiscard]] const WorldRegistry& worlds() const noexcept {
    return worlds_;
  }

  [[nodiscard]] const world::WorldRuntime& voxel_world() const noexcept {
    return default_world_;
  }

  [[nodiscard]] world::WorldRuntime& voxel_world() noexcept {
    return default_world_;
  }

private:
  void handle_network_event(net::NetworkEvent event) {
    switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
    case net::NetworkEventType::ConnectionAccepted:
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
      sessions_.remove_by_connection(event.connection_id);
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(event.connection_id, event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      return;
    }
  }

  void handle_message(net::ConnectionID connection_id, const std::vector<u8>& packet) {
    auto decoded = protocol::deserialize_envelope(packet);
    if (!decoded || decoded.value().envelope.message_type != protocol::MessageType::ClientHello) {
      return;
    }

    auto client_hello = protocol::deserialize_client_hello(decoded.value().payload);
    if (!client_hello) {
      return;
    }

    protocol::HandshakePolicy policy = handshake_policy_;
    if (ServerSession* existing = sessions_.find_by_connection(connection_id); existing != nullptr) {
      policy.assigned_session_id = existing->session_id;
      policy.assigned_player_id = existing->player_id;
      policy.initial_world_id = existing->world_id;
    } else {
      const ReservedSessionIdentity identity = sessions_.reserve_identity();
      policy.assigned_session_id = identity.session_id;
      policy.assigned_player_id = identity.player_id;
      policy.initial_world_id = DefaultWorldID;
    }

    protocol::ServerHello server_hello = protocol::make_server_hello(client_hello.value(), policy);
    if (server_hello.accepted && sessions_.find_by_connection(connection_id) == nullptr) {
      sessions_.create(
          connection_id,
          {server_hello.assigned_session_id, server_hello.assigned_player_id},
          server_hello.initial_world_id,
          client_hello.value().requested_username_or_profile_name);
    }

    std::vector<u8> payload = protocol::serialize_server_hello(server_hello);
    std::vector<u8> reply = protocol::serialize_envelope(
        {protocol::CurrentProtocolVersion, protocol::MessageType::ServerHello, 0, 0, 0, 0},
        payload);
    (void)transport_->send({connection_id, protocol::ReliabilityClass::ReliableOrdered, std::move(reply)});
  }

  std::unique_ptr<net::ITransport> transport_;
  ServerRuntimeState state_{ServerRuntimeState::Stopped};
  ServerMode mode_{ServerMode::Integrated};
  TickRate base_tps_{DefaultBaseTps};
  TickIndex tick_index_{};
  protocol::HandshakePolicy handshake_policy_{};
  SessionRegistry sessions_;
  WorldRegistry worlds_;
  world::WorldRuntime default_world_{DefaultWorldID};
  std::vector<net::NetworkEvent> inbound_events_;
};

} // namespace arv::server
