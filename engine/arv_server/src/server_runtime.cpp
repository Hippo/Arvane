#include "arv/server/server_runtime.hpp"

#include "arv/protocol/message_envelope.hpp"

#include <utility>

namespace arv::server {

ServerRuntime::ServerRuntime(std::unique_ptr<net::ITransport> transport) : transport_(std::move(transport)) {
}

net::NetworkResult ServerRuntime::start(const ServerStartInfo& start_info) {
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

void ServerRuntime::tick() {
  if (state_ != ServerRuntimeState::Running || !transport_) {
    return;
  }

  inbound_events_ = transport_->poll_events();
  for (net::NetworkEvent& event : inbound_events_) {
    handle_network_event(std::move(event));
  }
  ++tick_index_;
}

void ServerRuntime::shutdown() {
  if (state_ == ServerRuntimeState::Stopped) {
    return;
  }

  state_ = ServerRuntimeState::Stopping;
  if (transport_) {
    transport_->shutdown();
  }
  state_ = ServerRuntimeState::Stopped;
}

void ServerRuntime::handle_network_event(net::NetworkEvent event) {
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

void ServerRuntime::handle_message(net::ConnectionID connection_id, const std::vector<u8>& packet) {
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

} // namespace arv::server
