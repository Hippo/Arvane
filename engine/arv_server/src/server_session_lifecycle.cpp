#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/crypto/random.hpp"
#include "arv/text/language.hpp"
#include "arv/server/skin_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace arv::server::detail {
void ServerRuntimeImpl::send_auth_result(
    const ServerSession& session,
    bool accepted,
    std::string_view reason_if_rejected) {
  protocol::ClientboundAuthResult result;
  result.accepted = accepted;
  result.profile_id = session.profile_id;
  result.reason_if_rejected = accepted ? std::string{} : std::string{reason_if_rejected};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundAuthResult,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_auth_result(result));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::send_auth_challenge(ServerSession& session) {
  PendingAuthChallenge challenge;
  challenge.issued_server_tick = tick_index_;
  challenge.expires_server_tick = tick_index_ + 200U;
  challenge.server_instance_id = server_instance_id_;
  crypto::fill_random(challenge.nonce);
  session.challenge = challenge;

  protocol::ClientboundAuthChallenge packet;
  packet.session_id = session.session_id;
  packet.nonce = challenge.nonce;
  packet.expires_server_tick = challenge.expires_server_tick;
  packet.server_instance_id = challenge.server_instance_id;
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundAuthChallenge,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_auth_challenge(packet));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::send_skin_rules(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  protocol::ClientboundSkinRules rules;
  rules.enabled = skins_.enabled;
  rules.allow_uploads = skins_.enabled && skins_.allow_uploads;
  rules.max_res = max_allowed_skin_resolution(skins_, player_state.resolved_permissions);
  const std::string selected_skin_hash = selected_skin_hash_for_session(session);
  if (skins_.enabled && protocol::is_valid_skin_hash(selected_skin_hash)) {
    auto descriptor = load_stored_skin_descriptor(server_save_root_, selected_skin_hash);
    if (descriptor) {
      rules.selected_skin = std::move(descriptor.value());
      rules.selected_skin.slim_model = selected_skin_slim_model_for_session(session);
      skin_transfers_[session.session_id].advertised_skin_hashes.insert(
          rules.selected_skin.skin_hash);
    }
  }
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundSkinRules,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_rules(rules));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::send_player_profile_info_to(
    const ServerSession& receiver,
    const ServerSession& subject) {
  protocol::ClientboundPlayerProfileInfo info;
  info.player_id = subject.player_id;
  info.profile_id = subject.profile_id.string();
  info.display_name = subject.display_name;
  const std::string skin_hash = selected_skin_hash_for_session(subject);
  if (skins_.enabled && protocol::is_valid_skin_hash(skin_hash)) {
    auto descriptor = load_stored_skin_descriptor(server_save_root_, skin_hash);
    if (descriptor) {
      info.skin = std::move(descriptor.value());
      info.skin.slim_model = selected_skin_slim_model_for_session(subject);
      skin_transfers_[receiver.session_id].advertised_skin_hashes.insert(info.skin.skin_hash);
    }
  }
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      receiver.connection_id,
      protocol::MessageType::ClientboundPlayerProfileInfo,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_player_profile_info(info));
  log_send_failure_if_dedicated(mode_, receiver.connection_id, send_result);
}

void ServerRuntimeImpl::broadcast_player_profile_info(const ServerSession& subject) {
  sessions_.for_each_session([&](const ServerSession& receiver) {
    if (!receiver.authenticated || receiver.world_id != subject.world_id) {
      return;
    }
    send_player_profile_info_to(receiver, subject);
  });
}

bool ServerRuntimeImpl::profile_is_online_for_another_session(
    const ServerSession& session) const {
  if (session.profile_id.empty()) {
    return false;
  }
  const auto found = online_profile_sessions_.find(session.profile_id.string());
  return found != online_profile_sessions_.end() && found->second != session.session_id;
}

void ServerRuntimeImpl::register_online_profile(const ServerSession& session) {
  if (!session.profile_id.empty()) {
    online_profile_sessions_[session.profile_id.string()] = session.session_id;
  }
}

void ServerRuntimeImpl::remove_online_profile_for_session(const ServerSession& session) {
  if (session.profile_id.empty()) {
    return;
  }
  const auto found = online_profile_sessions_.find(session.profile_id.string());
  if (found != online_profile_sessions_.end() && found->second == session.session_id) {
    online_profile_sessions_.erase(found);
  }
}

void ServerRuntimeImpl::kick_client(
    net::ConnectionID connection_id,
    std::string_view reason) {
  if (!transport_) {
    cleanup_connection(connection_id);
    return;
  }

  if (ServerSession* session = sessions_.find_by_connection(connection_id);
      session != nullptr) {
    if (session->closing) {
      return;
    }
    session->closing = true;
    session->connection_state = net::ConnectionState::Disconnecting;
  }

  const protocol::ClientboundKickPlayer kick{
      protocol::clamp_kick_reason(
          reason.empty() ? arv::text::text_or_fallback(language_, "server.disconnect.default", "Disconnected.")
                         : std::string{reason})};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundKickPlayer,
      protocol::ReliabilityClass::ReliableImmediate,
      protocol::NetworkLane::Session,
      protocol::serialize_clientbound_kick_player(kick));
  log_send_failure_if_dedicated(mode_, connection_id, send_result);
  (void)transport_->close_connection(connection_id, kick.reason);
  cleanup_connection(connection_id);
}

bool ServerRuntimeImpl::complete_authenticated_session(ServerSession& session) {
  if (profile_is_online_for_another_session(session)) {
    core::warn(
        "server",
        "rejecting duplicate login for profile '{}'",
        session.profile_id.string());
    kick_client(
        session.connection_id,
        arv::text::text_or_fallback(language_,
            "server.disconnect.duplicate_profile",
            "This Arvane profile is already connected to this server."));
    return false;
  }

  session.authenticated = true;
  session.pending_auth = false;
  session.challenge.reset();
  const auto now = std::chrono::steady_clock::now();
  session.last_keep_alive_sent_time = {};
  session.last_keep_alive_received_time = now;
  session.last_keep_alive_id = 0U;
  register_online_profile(session);
  player::PlayerState& player_state = default_world_.ensure_player(session.player_id);
  player_state.world_id = session.world_id;
  player_state.fly_speed = player_tuning_.fly_speed;
  if (!apply_saved_player_state(session, player_state)) {
    seed_player_inventory(player_state);
  }
  refresh_player_permissions(player_state);
  accepted_player_movement_[session.player_id] = {
      player_state.position,
      player_state.velocity,
      player_state.yaw_radians,
      player_state.pitch_radians,
      protocol::PlayerMoveFlags::None,
      0U,
      tick_index_,
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(base_tps_, 1U)),
      true};
  session.connection_state = net::ConnectionState::WorldJoin;
  return true;
}

void ServerRuntimeImpl::handle_connection_lost(net::ConnectionID connection_id) {
  cleanup_connection(connection_id);
}

void ServerRuntimeImpl::cleanup_connection(net::ConnectionID connection_id) {
  if (const ServerSession* session = sessions_.find_by_connection(connection_id);
      session != nullptr) {
    remove_online_profile_for_session(*session);
    chunk_streamer_.remove_session(session->session_id);
    entity_replication_.erase(session->session_id);
    inventory_replication_.erase(session->player_id);
    pending_player_commands_.erase(session->player_id);
    accepted_player_movement_.erase(session->player_id);
    last_player_move_correction_seconds_.erase(session->player_id);
    last_sent_block_break_progress_.erase(session->player_id);
    text_variables_.clear_player(session->player_id);
    permission_caches_.erase(session->player_id);
    block_break_attempts_.erase(session->player_id);
    combat_states_.erase(session->session_id);
    logged_chunk_interest_sessions_.erase(session->session_id);
    logged_chunk_delivery_sessions_.erase(session->session_id);
    if (auto transfer = skin_transfers_.find(session->session_id);
        transfer != skin_transfers_.end() && transfer->second.active_upload.has_value() &&
        active_skin_uploads_global_ > 0U) {
      --active_skin_uploads_global_;
    }
    skin_transfers_.erase(session->session_id);
    chat_rate_limits_.erase(session->session_id);
    if (const player::PlayerState* player_state =
            default_world_.find_player(session->player_id);
        player_state != nullptr) {
      record_player_save_state(*session, *player_state);
    }
    if (const std::optional<EntityID> entity_id =
            default_world_.entity_for_player(session->player_id);
        entity_id.has_value()) {
      (void)default_world_.destroy_entity(entity_id.value(), world::EntityDespawnReason::Removed);
      default_world_.flush_entity_despawns();
    }
  }
  sessions_.remove_by_connection(connection_id);
}

void ServerRuntimeImpl::send_keep_alive(
    ServerSession& session,
    std::chrono::steady_clock::time_point now) {
  if (!transport_ || session.closing || !session.authenticated) {
    return;
  }
  ++session.last_keep_alive_id;
  session.last_keep_alive_sent_time = now;
  const protocol::ClientboundKeepAlive keep_alive{session.last_keep_alive_id};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundKeepAlive,
      protocol::ReliabilityClass::UnreliableEvent,
      protocol::NetworkLane::Session,
      protocol::serialize_clientbound_keep_alive(keep_alive));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::tick_keep_alive() {
  if (!transport_ || !keep_alive_enabled_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto interval = std::chrono::seconds(keep_alive_.interval_seconds);
  const auto timeout = std::chrono::seconds(keep_alive_.client_timeout_seconds);
  std::vector<net::ConnectionID> timed_out_connections;
  std::vector<SessionID> sessions_to_probe = sessions_.session_ids();
  for (SessionID session_id : sessions_to_probe) {
    ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || !session->authenticated || session->closing) {
      continue;
    }
    if (session->last_keep_alive_received_time.time_since_epoch().count() != 0 &&
        now - session->last_keep_alive_received_time > timeout) {
      timed_out_connections.push_back(session->connection_id);
      continue;
    }
    if (session->last_keep_alive_sent_time.time_since_epoch().count() == 0 ||
        now - session->last_keep_alive_sent_time >= interval) {
      send_keep_alive(*session, now);
    }
  }

  for (net::ConnectionID connection_id : timed_out_connections) {
    kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.timed_out", "Timed out."));
  }
}

void ServerRuntimeImpl::send_world_join_result(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  protocol::ClientboundWorldJoinResult result;
  result.accepted = true;
  result.world_id = session.world_id;
  result.player_id = session.player_id;
  result.spawn_position = player_state.position;
  result.initial_tick = tick_index_;
  result.tick_rate = base_tps_;
  result.effective_view_distance = max_view_distance_;
  if (const std::optional<EntityID> entity_id =
          default_world_.entity_for_player(session.player_id);
      entity_id.has_value()) {
    result.local_entity_id =
        default_world_.network_id_for_entity(entity_id.value()).value_or(EntityNetID{});
  }

  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundWorldJoinResult,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_clientbound_world_join_result(result));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
  if (send_result) {
    send_player_permissions(session, player_state);
    send_player_movement_tuning(session, player_state);
    send_movement_hz_if_non_default(session);
  }
  if (send_result && mode_ == ServerMode::Dedicated) {
    core::info(
        "dedicated",
        "client '{}' joined world '{}': session={} player={} spawn=({}, {}, {}) tick={} view_limit={}",
        session.profile_name,
        world_name_,
        session.session_id.value,
        session.player_id.value,
        player_state.position.x,
        player_state.position.y,
        player_state.position.z,
        tick_index_,
        max_view_distance_);
  }
}

void ServerRuntimeImpl::send_player_permissions(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  protocol::ClientboundPlayerPermissions permissions;
  permissions.game_mode_id = player_state.game_mode_id;
  permissions.core = player_state.resolved_permissions.core;
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundPlayerPermissions,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_clientbound_player_permissions(permissions));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::send_player_movement_tuning(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  protocol::ClientboundPlayerMovementTuning tuning;
  tuning.fly_speed = player_state.fly_speed;
  tuning.fly_vertical_speed = player_tuning_.fly_vertical_speed;
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundPlayerMovementTuning,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_clientbound_player_movement_tuning(tuning));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::send_movement_hz_if_non_default(const ServerSession& session) {
  if (std::abs(movement_.send_rate_hz - protocol::DefaultClientMovementSendRateHz) <=
      0.0001F) {
    return;
  }

  const protocol::ClientboundSetMovementHz settings{movement_.send_rate_hz};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundSetMovementHz,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_clientbound_set_movement_hz(settings));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::seed_player_inventory(player::PlayerState& player_state) {
  if (!inventory_is_empty(player_state.inventory)) {
    return;
  }

  for (const StarterInventoryItem& item : starter_inventory_) {
    const auto item_id = item_registry_->find_item(item.item_name);
    if (!item_id.has_value()) {
      continue;
    }
    (void)player_state.inventory.insert_stack(
        {item_id.value(), item.count},
        *item_registry_);
  }
}

bool ServerRuntimeImpl::inventory_is_empty(const player::Inventory& inventory) const noexcept {
  for (usize slot_index = 0; slot_index < inventory.slot_count(); ++slot_index) {
    if (inventory.slot(slot_index).has_value()) {
      return false;
    }
  }
  return true;
}

} // namespace arv::server::detail
