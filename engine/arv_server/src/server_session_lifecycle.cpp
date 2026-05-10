#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/crypto/random.hpp"
#include "arv/server/skin_runtime.hpp"

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

void ServerRuntimeImpl::complete_authenticated_session(ServerSession& session) {
  session.authenticated = true;
  session.pending_auth = false;
  session.challenge.reset();
  player::PlayerState& player_state = default_world_.ensure_player(session.player_id);
  player_state.world_id = session.world_id;
  if (!apply_saved_player_state(session, player_state)) {
    seed_player_inventory(player_state);
  }
  refresh_player_permissions(player_state);
  session.connection_state = net::ConnectionState::WorldJoin;
}

void ServerRuntimeImpl::handle_connection_lost(net::ConnectionID connection_id) {
  if (const ServerSession* session = sessions_.find_by_connection(connection_id);
      session != nullptr) {
    chunk_streamer_.remove_session(session->session_id);
    entity_replication_.erase(session->session_id);
    inventory_replication_.erase(session->player_id);
    pending_player_commands_.erase(session->player_id);
    last_sent_player_corrections_.erase(session->player_id);
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

void ServerRuntimeImpl::seed_player_inventory(player::PlayerState& player_state) {
  if (!inventory_is_empty(player_state.inventory)) {
    return;
  }

  // TODO(arvane): Replace this starter loadout with documented gameplay spawn
  // inventory rules once the built-in game layer is defined.
  for (const auto [item_name, count] :
       {std::pair{std::string_view{"arvane:stone"}, u16{64}},
        std::pair{std::string_view{"arvane:dirt"}, u16{64}},
        std::pair{std::string_view{"arvane:grass_block"}, u16{64}},
        std::pair{std::string_view{"arvane:glass"}, u16{64}},
        std::pair{std::string_view{"arvane:glowstone"}, u16{64}},
        std::pair{std::string_view{"arvane:coal"}, u16{32}},
        std::pair{std::string_view{"arvane:wood_pickaxe"}, u16{1}},
        std::pair{std::string_view{"arvane:bucket"}, u16{1}},
        std::pair{std::string_view{"arvane:water_bucket"}, u16{1}}}) {
    const auto item_id = item_registry_->find_item(item_name);
    if (!item_id.has_value()) {
      continue;
    }
    (void)player_state.inventory.insert_stack({item_id.value(), count}, *item_registry_);
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
