#include "client_network_messages.hpp"

#include "arv/crypto/hash.hpp"
#include "arv/text/language.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/protocol/world_join.hpp"

#include "client_runtime_internal.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

namespace arv::client::detail {
namespace {

[[nodiscard]] std::string tr(
    const arv::text::LanguageCatalog* language,
    std::string_view key,
    std::string_view fallback) {
  return language == nullptr ? std::string{fallback} : language->text(key, fallback);
}

void apply_world_join_result(
    ClientRuntimeContext& runtime,
    const protocol::ClientboundWorldJoinResult& result) {
  if (!result.accepted) {
    runtime.connection.disconnect_local(tr(
        runtime.language,
        "ui.connection.rejected_world_join",
        "Server rejected world join."));
    return;
  }
  runtime.player_controller.enter_world(
      result.world_id,
      result.player_id,
      result.local_entity_id,
      result.spawn_position,
      result.initial_tick,
      result.tick_rate);
  runtime.chunk_interest.set_world_id(result.world_id);
  runtime.connection.set_state(net::ConnectionState::InGame);
}

void apply_world_leave(
    ClientRuntimeContext& runtime,
    const protocol::ClientboundWorldLeave& leave) {
  if (leave.world_id != runtime.player_controller.camera().world_id()) {
    return;
  }
  runtime.entities.clear();
  runtime.dropped_items.clear();
  runtime.chunks.clear();
  runtime.inventory.reset();
  runtime.player_controller.leave_world();
  runtime.chunk_interest.set_world_id(WorldID{});
  runtime.connection.set_state(net::ConnectionState::WorldJoin);
}

void clear_multiplayer_runtime(ClientRuntimeContext& runtime) {
  runtime.entities.clear();
  runtime.dropped_items.clear();
  runtime.chunks.clear();
  runtime.inventory.reset();
  runtime.player_controller.leave_world();
  runtime.chunk_interest.set_world_id(WorldID{});
  runtime.ui.close_ui();
}

void disconnect_and_clear(ClientRuntimeContext& runtime, std::string_view reason) {
  runtime.connection.disconnect_local(reason);
  clear_multiplayer_runtime(runtime);
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
      if (runtime.connection.disconnect_reason().has_value()) {
        clear_multiplayer_runtime(runtime);
      }
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(runtime, event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      runtime.connection.handle_message_send_failed();
      if (runtime.connection.disconnect_reason().has_value()) {
        clear_multiplayer_runtime(runtime);
      }
      return;
  }
}

void ClientNetworkRouter::handle_message(
    ClientRuntimeContext& runtime,
    const std::vector<u8>& packet) {
  auto decoded = protocol::deserialize_envelope(packet);
  if (!decoded) {
    disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.invalid_protocol_packet", "Invalid protocol packet."));
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ClientboundKickPlayer: {
      auto kick = protocol::deserialize_clientbound_kick_player(decoded.value().payload);
      if (!kick) {
        disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      disconnect_and_clear(runtime, kick.value().reason);
      return;
    }
    case protocol::MessageType::ClientboundKeepAlive: {
      auto keep_alive = protocol::deserialize_clientbound_keep_alive(decoded.value().payload);
      if (!keep_alive) {
        disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      runtime.connection.record_server_keep_alive();
      if (!runtime.sender.send_keep_alive({keep_alive.value().id})) {
        disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.lost", "Lost connection to the server."));
      }
      return;
    }
    case protocol::MessageType::ClientboundHello: {
      auto server_hello = protocol::deserialize_clientbound_hello(decoded.value().payload);
      if (!server_hello) {
        disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }

      if (!server_hello.value().accepted) {
        const std::string& reason = server_hello.value().reject_reason;
        if (reason.empty()) {
          disconnect_and_clear(
              runtime,
              tr(
                  runtime.language,
                  "ui.connection.rejected_connection",
                  "Server rejected connection."));
        } else {
          disconnect_and_clear(runtime, reason);
        }
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
    case protocol::MessageType::ClientboundAuthChallenge: {
      auto challenge = protocol::deserialize_auth_challenge(decoded.value().payload);
      if (!challenge) {
        disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.invalid_protocol_packet", "Invalid protocol packet."));
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
        disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.lost", "Lost connection to the server."));
      }
      return;
    }
    case protocol::MessageType::ClientboundAuthResult: {
      auto result = protocol::deserialize_auth_result(decoded.value().payload);
      if (!result || !result.value().accepted) {
        if (!result) {
          disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.invalid_protocol_packet", "Invalid protocol packet."));
        } else {
          const std::string& reason = result.value().reason_if_rejected;
          if (reason.empty()) {
            disconnect_and_clear(
                runtime,
                tr(
                    runtime.language,
                    "ui.connection.authentication_failed",
                    "authentication failed"));
          } else {
            disconnect_and_clear(runtime, reason);
          }
        }
      }
      return;
    }
    case protocol::MessageType::ClientboundSkinRules: {
      auto rules = protocol::deserialize_skin_rules(decoded.value().payload);
      if (!rules) {
        return;
      }
      runtime.skins.handle_skin_rules(
          runtime.identity,
          runtime.sender,
          std::move(rules.value()));
      return;
    }
    case protocol::MessageType::ClientboundPlayerProfileInfo: {
      auto info = protocol::deserialize_player_profile_info(decoded.value().payload);
      if (!info) {
        return;
      }
      runtime.skins.handle_profile_info(
          runtime.identity,
          runtime.sender,
          std::move(info.value()));
      return;
    }
    case protocol::MessageType::ClientboundPlayerProfileInfoBatch: {
      auto batch = protocol::deserialize_player_profile_info_batch(decoded.value().payload);
      if (!batch) {
        return;
      }
      runtime.skins.handle_profile_info_batch(
          runtime.identity,
          runtime.sender,
          std::move(batch.value()));
      return;
    }
    case protocol::MessageType::ClientboundSkinData: {
      auto data = protocol::deserialize_skin_data(decoded.value().payload);
      if (!data) {
        return;
      }
      runtime.skins.handle_skin_data(runtime.identity, std::move(data.value()));
      return;
    }
    case protocol::MessageType::ClientboundSkinDataBeginChunked: {
      auto begin = protocol::deserialize_skin_data_begin_chunked(decoded.value().payload);
      if (!begin) {
        return;
      }
      runtime.skins.handle_skin_data_begin_chunked(std::move(begin.value()));
      return;
    }
    case protocol::MessageType::ClientboundSkinDataChunk: {
      auto chunk = protocol::deserialize_skin_data_chunk(decoded.value().payload);
      if (!chunk) {
        return;
      }
      runtime.skins.handle_skin_data_chunk(std::move(chunk.value()));
      return;
    }
    case protocol::MessageType::ClientboundSkinDataEndChunked: {
      auto end = protocol::deserialize_skin_data_end_chunked(decoded.value().payload);
      if (!end) {
        return;
      }
      runtime.skins.handle_skin_data_end_chunked(runtime.identity, end.value());
      return;
    }
    case protocol::MessageType::ClientboundSkinRequestDeny: {
      auto deny = protocol::deserialize_skin_request_deny(decoded.value().payload);
      if (!deny) {
        return;
      }
      runtime.skins.handle_skin_request_deny(std::move(deny.value()));
      return;
    }
    case protocol::MessageType::ClientboundSkinUploadAccept: {
      auto accept = protocol::deserialize_skin_upload_accept(decoded.value().payload);
      if (!accept) {
        return;
      }
      runtime.skins.handle_skin_upload_accept(runtime.sender, accept.value());
      return;
    }
    case protocol::MessageType::ClientboundSkinUploadDeny: {
      auto deny = protocol::deserialize_skin_upload_deny(decoded.value().payload);
      if (!deny) {
        return;
      }
      runtime.skins.handle_skin_upload_deny(deny.value());
      return;
    }
    case protocol::MessageType::ClientboundSkinUploadComplete: {
      auto complete = protocol::deserialize_skin_upload_complete(decoded.value().payload);
      if (!complete) {
        return;
      }
      runtime.skins.handle_skin_upload_complete(std::move(complete.value()));
      return;
    }
    case protocol::MessageType::ClientboundSkinUploadFailed: {
      auto failed = protocol::deserialize_skin_upload_failed(decoded.value().payload);
      if (!failed) {
        return;
      }
      runtime.skins.handle_skin_upload_failed(failed.value());
      return;
    }
    case protocol::MessageType::ClientboundChatMessage: {
      auto chat = protocol::deserialize_clientbound_chat_message(decoded.value().payload);
      if (!chat) {
        return;
      }
      runtime.ui.add_chat_message(std::move(chat.value()));
      return;
    }
    case protocol::MessageType::ClientboundWorldJoinResult: {
      auto result = protocol::deserialize_clientbound_world_join_result(decoded.value().payload);
      if (!result) {
        disconnect_and_clear(runtime, tr(runtime.language, "ui.connection.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      apply_world_join_result(runtime, result.value());
      return;
    }
    case protocol::MessageType::ClientboundWorldLeave: {
      auto leave = protocol::deserialize_clientbound_world_leave(decoded.value().payload);
      if (!leave) {
        return;
      }
      apply_world_leave(runtime, leave.value());
      return;
    }
    case protocol::MessageType::ClientboundPlayerMoveCorrection: {
      auto correction = protocol::deserialize_clientbound_player_move_correction(decoded.value().payload);
      if (!correction) {
        return;
      }
      runtime.player_controller.apply_move_correction(correction.value());
      return;
    }
    case protocol::MessageType::ClientboundPlayerVelocity: {
      auto velocity = protocol::deserialize_clientbound_player_velocity(decoded.value().payload);
      if (!velocity) {
        return;
      }
      runtime.player_controller.apply_player_velocity(velocity.value());
      return;
    }
    case protocol::MessageType::ClientboundSetMovementHz: {
      auto settings = protocol::deserialize_clientbound_set_movement_hz(decoded.value().payload);
      if (!settings) {
        return;
      }
      runtime.input.set_movement_send_rate(settings.value().movement_hz);
      return;
    }
    case protocol::MessageType::ClientboundPlayerBlockBreakProgress: {
      auto progress =
          protocol::deserialize_clientbound_player_block_break_progress(decoded.value().payload);
      if (!progress) {
        return;
      }
      runtime.player_controller.apply_block_break_progress(progress.value());
      return;
    }
    case protocol::MessageType::ClientboundChunkSectionData: {
      auto chunk_data = protocol::deserialize_clientbound_chunk_section_data(decoded.value().payload);
      if (!chunk_data) {
        return;
      }
      runtime.chunks.push_chunk_section_data(std::move(chunk_data.value()));
      return;
    }
    case protocol::MessageType::ClientboundChunkSectionUnload: {
      auto unload = protocol::deserialize_clientbound_chunk_section_unload(decoded.value().payload);
      if (!unload) {
        return;
      }
      runtime.chunks.push_chunk_section_unload(unload.value());
      return;
    }
    case protocol::MessageType::ClientboundBlockUpdateBatch: {
      auto updates = protocol::deserialize_clientbound_block_update_batch(decoded.value().payload);
      if (!updates) {
        return;
      }
      if (updates.value().world_id == runtime.player_controller.camera().world_id()) {
        runtime.chunks.push_block_updates(std::move(updates.value()));
      }
      return;
    }
    case protocol::MessageType::ClientboundEntitySpawn: {
      auto spawn = protocol::deserialize_clientbound_entity_spawn(decoded.value().payload);
      if (!spawn) {
        return;
      }
      runtime.entities.apply_spawn(
          std::move(spawn.value()),
          runtime.player_controller.camera().world_id());
      return;
    }
    case protocol::MessageType::ClientboundEntityDespawn: {
      auto despawn = protocol::deserialize_clientbound_entity_despawn(decoded.value().payload);
      if (!despawn) {
        return;
      }
      runtime.entities.apply_despawn(
          despawn.value(),
          runtime.player_controller.camera().world_id());
      return;
    }
    case protocol::MessageType::ClientboundEntitySnapshot: {
      auto snapshot = protocol::deserialize_clientbound_entity_snapshot(decoded.value().payload);
      if (!snapshot) {
        return;
      }
      runtime.entities.apply_snapshot(
          snapshot.value(),
          runtime.player_controller.camera().world_id(),
          runtime.player_controller.server_tick_rate());
      return;
    }
    case protocol::MessageType::ClientboundDroppedItemSnapshot: {
      auto snapshot = protocol::deserialize_clientbound_dropped_item_snapshot(decoded.value().payload);
      if (!snapshot) {
        return;
      }
      runtime.dropped_items.apply_snapshot(
          snapshot.value(),
          runtime.player_controller.camera().world_id(),
          runtime.player_controller.server_tick_rate());
      return;
    }
    case protocol::MessageType::ClientboundInventoryUpdate: {
      auto update = protocol::deserialize_clientbound_inventory_update(decoded.value().payload);
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
