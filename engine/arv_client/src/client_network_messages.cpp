#include "client_network_messages.hpp"

#include "arv/crypto/hash.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/world_join.hpp"

#include "client_runtime_internal.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace arv::client::detail {
namespace {

void apply_world_join_result(
    ClientRuntimeContext& runtime,
    const protocol::WorldJoinResult& result) {
  if (!result.accepted) {
    runtime.connection.set_state(net::ConnectionState::Disconnecting);
    return;
  }
  runtime.player_controller.enter_world(result.world_id, result.spawn_position);
  runtime.chunk_interest.set_world_id(result.world_id);
  runtime.connection.set_state(net::ConnectionState::InGame);
}

void apply_world_leave(
    ClientRuntimeContext& runtime,
    const protocol::WorldLeave& leave) {
  if (leave.world_id != runtime.player_controller.camera().world_id()) {
    return;
  }
  runtime.entities.clear();
  runtime.dropped_items.clear();
  runtime.chunks.clear();
  runtime.inventory.reset();
  runtime.player_controller.camera().set_world_id(WorldID{});
  runtime.chunk_interest.set_world_id(WorldID{});
  runtime.connection.set_state(net::ConnectionState::WorldJoin);
}

} // namespace

void ClientNetworkRouter::process_events(ClientRuntimeContext& runtime) {
  std::vector<net::NetworkEvent> inbound_events = runtime.connection.poll_events();
  for (net::NetworkEvent& event : inbound_events) {
    handle_event(runtime, std::move(event));
  }
}

void ClientNetworkRouter::handle_event(
    ClientRuntimeContext& runtime,
    net::NetworkEvent event) {
  switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
      return;
    case net::NetworkEventType::ConnectionAccepted:
      runtime.connection.handle_connection_accepted(runtime.sender, runtime.identity);
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
      runtime.connection.handle_connection_closed_or_failed(event);
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(runtime, event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      runtime.connection.handle_message_send_failed();
      return;
  }
}

void ClientNetworkRouter::handle_message(
    ClientRuntimeContext& runtime,
    const std::vector<u8>& packet) {
  auto decoded = protocol::deserialize_envelope(packet);
  if (!decoded) {
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ServerHello: {
      auto server_hello = protocol::deserialize_server_hello(decoded.value().payload);
      if (!server_hello) {
        runtime.connection.set_state(net::ConnectionState::Disconnecting);
        return;
      }

      if (!server_hello.value().accepted) {
        runtime.connection.set_state(net::ConnectionState::Disconnecting);
        return;
      }

      runtime.player_controller.camera().set_world_id(server_hello.value().initial_world_id);
      runtime.chunk_interest.set_world_id(server_hello.value().initial_world_id);
      runtime.connection.set_state(
          server_hello.value().required_followup_sync
              ? net::ConnectionState::RegistrySync
              : net::ConnectionState::WorldJoin);
      return;
    }
    case protocol::MessageType::AuthChallenge: {
      auto challenge = protocol::deserialize_auth_challenge(decoded.value().payload);
      if (!challenge) {
        runtime.connection.set_state(net::ConnectionState::Disconnecting);
        return;
      }

      const crypto::PlayerProfileID profile_id =
          runtime.identity.profile_id.empty()
              ? crypto::derive_profile_id_from_public_key(runtime.identity.key_pair.public_key)
              : runtime.identity.profile_id;
      const std::vector<u8> payload_bytes = protocol::build_auth_signed_payload(
          {protocol::CurrentProtocolVersion,
           challenge.value().server_instance_id,
           challenge.value().session_id,
           profile_id,
           runtime.identity.key_pair.public_key,
           challenge.value().nonce,
           challenge.value().expires_server_tick});
      const auto* first = reinterpret_cast<const std::byte*>(payload_bytes.data());
      protocol::ServerboundAuthResponse response;
      response.signature = crypto::sign_ed25519(
          std::span<const std::byte>{first, payload_bytes.size()},
          runtime.identity.key_pair.private_key);
      if (!runtime.sender.send_auth_response(response)) {
        runtime.connection.set_state(net::ConnectionState::Disconnecting);
      }
      return;
    }
    case protocol::MessageType::AuthResult: {
      auto result = protocol::deserialize_auth_result(decoded.value().payload);
      if (!result || !result.value().accepted) {
        runtime.connection.set_state(net::ConnectionState::Disconnecting);
      }
      return;
    }
    case protocol::MessageType::WorldJoinResult: {
      auto result = protocol::deserialize_world_join_result(decoded.value().payload);
      if (!result) {
        runtime.connection.set_state(net::ConnectionState::Disconnecting);
        return;
      }
      apply_world_join_result(runtime, result.value());
      return;
    }
    case protocol::MessageType::WorldLeave: {
      auto leave = protocol::deserialize_world_leave(decoded.value().payload);
      if (!leave) {
        return;
      }
      apply_world_leave(runtime, leave.value());
      return;
    }
    case protocol::MessageType::PlayerCorrection: {
      auto correction = protocol::deserialize_player_correction(decoded.value().payload);
      if (!correction) {
        return;
      }
      runtime.player_controller.apply_authoritative_state(correction.value());
      return;
    }
    case protocol::MessageType::ChunkData: {
      auto chunk_data = protocol::deserialize_chunk_data(decoded.value().payload);
      if (!chunk_data) {
        return;
      }
      runtime.chunks.push_chunk_data(std::move(chunk_data.value()));
      return;
    }
    case protocol::MessageType::ChunkUnload: {
      auto unload = protocol::deserialize_chunk_unload(decoded.value().payload);
      if (!unload) {
        return;
      }
      runtime.chunks.push_chunk_unload(unload.value());
      return;
    }
    case protocol::MessageType::BlockUpdateBatch: {
      auto updates = protocol::deserialize_block_update_batch(decoded.value().payload);
      if (!updates) {
        return;
      }
      if (updates.value().world_id == runtime.player_controller.camera().world_id()) {
        runtime.chunks.push_block_updates(std::move(updates.value()));
      }
      return;
    }
    case protocol::MessageType::EntitySpawn: {
      auto spawn = protocol::deserialize_entity_spawn(decoded.value().payload);
      if (!spawn) {
        return;
      }
      runtime.entities.apply_spawn(
          std::move(spawn.value()),
          runtime.player_controller.camera().world_id());
      return;
    }
    case protocol::MessageType::EntityDespawn: {
      auto despawn = protocol::deserialize_entity_despawn(decoded.value().payload);
      if (!despawn) {
        return;
      }
      runtime.entities.apply_despawn(
          despawn.value(),
          runtime.player_controller.camera().world_id());
      return;
    }
    case protocol::MessageType::EntitySnapshot: {
      auto snapshot = protocol::deserialize_entity_snapshot(decoded.value().payload);
      if (!snapshot) {
        return;
      }
      runtime.entities.apply_snapshot(
          snapshot.value(),
          runtime.player_controller.camera().world_id());
      return;
    }
    case protocol::MessageType::DroppedItemSnapshot: {
      auto snapshot = protocol::deserialize_dropped_item_snapshot(decoded.value().payload);
      if (!snapshot) {
        return;
      }
      runtime.dropped_items.apply_snapshot(
          snapshot.value(),
          runtime.player_controller.camera().world_id());
      return;
    }
    case protocol::MessageType::InventoryUpdate: {
      auto update = protocol::deserialize_inventory_update(decoded.value().payload);
      if (!update) {
        return;
      }
      runtime.inventory.apply_update(std::move(update.value()));
      return;
    }
    default:
      return;
  }
}

} // namespace arv::client::detail
