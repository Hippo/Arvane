#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/crypto/random.hpp"

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
      protocol::MessageType::AuthResult,
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
      protocol::MessageType::AuthChallenge,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_auth_challenge(packet));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
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
    block_break_attempts_.erase(session->player_id);
    logged_chunk_interest_sessions_.erase(session->session_id);
    logged_chunk_delivery_sessions_.erase(session->session_id);
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
  protocol::WorldJoinResult result;
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
      protocol::MessageType::WorldJoinResult,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_world_join_result(result));
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
