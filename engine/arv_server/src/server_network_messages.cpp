#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/message_envelope.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace arv::server::detail {

net::NetworkResult send_protocol_message(
    net::ITransport& transport,
    net::ConnectionID connection_id,
    protocol::MessageType message_type,
    protocol::ReliabilityClass reliability,
    protocol::NetworkLane lane,
    std::vector<u8> payload) {
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  return transport.send(
      {connection_id,
       reliability,
       std::move(packet),
       message_type,
       lane});
}

void log_send_failure_if_dedicated(
    ServerMode mode,
    net::ConnectionID connection_id,
    const net::NetworkResult& send_result) {
  if (send_result || mode != ServerMode::Dedicated) {
    return;
  }

  core::warn(
      "dedicated",
      "network send failed: connection={} reason='{}'",
      connection_id.value,
      send_result.error().message);
}

void ServerRuntimeImpl::handle_network_event(net::NetworkEvent event) {
  switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
    case net::NetworkEventType::ConnectionAccepted:
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
      handle_connection_lost(event.connection_id);
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(event.connection_id, event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      if (mode_ == ServerMode::Dedicated) {
        core::warn(
            "dedicated",
            "network send failed: connection={} reason='{}'",
            event.connection_id.value,
            event.reason);
      }
      return;
  }
}

void ServerRuntimeImpl::handle_message(
    net::ConnectionID connection_id,
    const std::vector<u8>& packet) {
  auto decoded = protocol::deserialize_envelope(packet);
  if (!decoded) {
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ClientHello: {
      auto client_hello = protocol::deserialize_client_hello(decoded.value().payload);
      if (!client_hello) {
        return;
      }
      handle_client_hello(connection_id, client_hello.value());
      return;
    }
    case protocol::MessageType::AuthResponse: {
      auto response = protocol::deserialize_auth_response(decoded.value().payload);
      if (!response) {
        return;
      }
      handle_auth_response(connection_id, response.value());
      return;
    }
    case protocol::MessageType::ClientInputBatch: {
      auto batch = protocol::deserialize_client_input_batch(decoded.value().payload);
      if (!batch) {
        return;
      }
      handle_input_batch(connection_id, batch.value());
      return;
    }
    case protocol::MessageType::ChunkInterestUpdate: {
      auto interest = protocol::deserialize_chunk_interest_update(decoded.value().payload);
      if (!interest) {
        return;
      }
      handle_chunk_interest_update(connection_id, interest.value());
      return;
    }
    case protocol::MessageType::InventoryActionRequest: {
      auto request = protocol::deserialize_inventory_action_request(decoded.value().payload);
      if (!request) {
        return;
      }
      handle_inventory_action_request(connection_id, request.value());
      return;
    }
    default:
      return;
  }
}

void ServerRuntimeImpl::handle_client_hello(
    net::ConnectionID connection_id,
    const protocol::ClientHello& client_hello) {
  protocol::HandshakePolicy policy = handshake_policy_;
  if (!client_hello.auth_begin.has_value()) {
    protocol::ServerHello rejected = protocol::make_server_hello(client_hello, policy);
    rejected.accepted = false;
    rejected.reject_reason = "authentication required";
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ServerHello,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_server_hello(rejected));
    return;
  }

  const protocol::ServerboundAuthBegin& auth_begin = client_hello.auth_begin.value();
  PlayerProfileOpenResult profile_result = profiles_.open_or_create(
      auth_begin.public_key,
      auth_begin.display_name,
      auth_begin.claimed_profile_id);
  if (profile_result.status == PlayerProfileOpenStatus::InvalidClaimedProfileID ||
      profile_result.status == PlayerProfileOpenStatus::PublicKeyMismatch ||
      profile_result.profile == nullptr) {
    protocol::ServerHello rejected = protocol::make_server_hello(client_hello, policy);
    rejected.accepted = false;
    rejected.reject_reason = "authentication failed";
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ServerHello,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_server_hello(rejected));
    return;
  }

  SessionProfileIdentity profile_identity;
  profile_identity.profile_id = profile_result.profile->profile_id;
  profile_identity.display_name = profile_result.profile->display_name;
  profile_identity.public_key = profile_result.profile->public_key;
  profile_identity.has_public_key = true;

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

  protocol::ServerHello server_hello = protocol::make_server_hello(client_hello, policy);
  if (server_hello.accepted && sessions_.find_by_connection(connection_id) == nullptr) {
    ServerSession& session = sessions_.create(
        connection_id,
        {server_hello.assigned_session_id, server_hello.assigned_player_id},
        server_hello.initial_world_id,
        std::move(profile_identity),
        mode_ == ServerMode::Integrated);
    if (mode_ == ServerMode::Integrated) {
      complete_authenticated_session(session);
    }
  }

  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ServerHello,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_server_hello(server_hello));
  if (server_hello.accepted) {
    if (ServerSession* session = sessions_.find_by_connection(connection_id);
        session != nullptr) {
      if (mode_ == ServerMode::Integrated) {
        send_auth_result(*session, true);
        if (const player::PlayerState* player_state =
                default_world_.find_player(session->player_id);
            player_state != nullptr) {
          send_world_join_result(*session, *player_state);
          session->connection_state = net::ConnectionState::InGame;
        }
      } else if (!session->authenticated) {
        send_auth_challenge(*session);
      }
    }
  }
}

void ServerRuntimeImpl::handle_auth_response(
    net::ConnectionID connection_id,
    const protocol::ServerboundAuthResponse& response) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || session->authenticated || !session->challenge.has_value() ||
      !session->has_public_key) {
    return;
  }

  const PendingAuthChallenge challenge = session->challenge.value();
  session->challenge.reset();
  if (tick_index_ > challenge.expires_server_tick) {
    send_auth_result(*session, false, "authentication failed");
    return;
  }

  const std::vector<u8> payload_bytes = protocol::build_auth_signed_payload(
      {protocol::CurrentProtocolVersion,
       challenge.server_instance_id,
       session->session_id,
       session->profile_id,
       session->public_key,
       challenge.nonce,
       challenge.expires_server_tick});
  const auto* first = reinterpret_cast<const std::byte*>(payload_bytes.data());
  const bool verified = crypto::verify_ed25519(
      std::span<const std::byte>{first, payload_bytes.size()},
      response.signature,
      session->public_key);
  if (!verified) {
    send_auth_result(*session, false, "authentication failed");
    return;
  }

  complete_authenticated_session(*session);
  send_auth_result(*session, true);
  if (const player::PlayerState* player_state = default_world_.find_player(session->player_id);
      player_state != nullptr) {
    send_world_join_result(*session, *player_state);
    session->connection_state = net::ConnectionState::InGame;
  }
}

void ServerRuntimeImpl::handle_input_batch(
    net::ConnectionID connection_id,
    const protocol::ClientInputBatch& batch) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }

  for (usize index = 0; index < batch.inputs.size(); ++index) {
    const TickIndex input_tick = batch.first_input_tick + index;
    player::PlayerCommand command =
        protocol::input_sample_to_command(batch.inputs[index], input_tick);
    player::PlayerCommand& pending = pending_player_commands_[session->player_id];
    if (command.client_tick >= pending.client_tick) {
      pending = std::move(command);
    }
  }
}

void ServerRuntimeImpl::handle_chunk_interest_update(
    net::ConnectionID connection_id,
    const protocol::ChunkInterestUpdate& interest) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  const player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    return;
  }

  const ChunkSectionCoord authoritative_center = player_chunk_coord(*player_state);
  chunk_streamer_.update_player_interest(
      *session,
      authoritative_center,
      default_world_.height_settings(),
      interest);
  if (mode_ == ServerMode::Dedicated &&
      logged_chunk_interest_sessions_.insert(session->session_id).second) {
    core::info(
        "dedicated",
        "client '{}' requested chunk stream: session={} player={} center=({}, {}, {}) requested_view={} server_max_view={}",
        session->profile_name,
        session->session_id.value,
        session->player_id.value,
        authoritative_center.value.x,
        authoritative_center.value.y,
        authoritative_center.value.z,
        interest.client_view_distance,
        max_view_distance_);
  }
}

void ServerRuntimeImpl::handle_inventory_action_request(
    net::ConnectionID connection_id,
    const protocol::InventoryActionRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated ||
      request.inventory_id != protocol::PlayerInventoryID) {
    return;
  }

  switch (request.kind) {
    case protocol::InventoryActionKind::SlotClick:
      (void)apply_player_inventory_click(
          session->player_id,
          request.slot_index,
          request.button);
      return;
    case protocol::InventoryActionKind::DropCursor:
      (void)drop_player_inventory_cursor(session->player_id);
      return;
    case protocol::InventoryActionKind::DropOneCursor:
      (void)drop_one_from_player_inventory_cursor(session->player_id);
      return;
    case protocol::InventoryActionKind::CloseMenu:
      (void)close_player_inventory_menu(session->player_id);
      return;
  }
}

} // namespace arv::server::detail
