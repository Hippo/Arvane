#include "server_runtime_internal.hpp"

#include <utility>

namespace arv::server::detail {
namespace {

[[nodiscard]] protocol::PlayerCorrection make_player_correction(
    TickIndex server_tick,
    const player::PlayerState& player_state,
    const protocol::BlockBreakProgress* block_break = nullptr) {
  protocol::PlayerCorrection correction;
  correction.server_tick = server_tick;
  correction.player_id = player_state.player_id;
  correction.world_id = player_state.world_id;
  correction.position = player_state.position;
  correction.velocity = player_state.velocity;
  correction.yaw_radians = player_state.yaw_radians;
  correction.pitch_radians = player_state.pitch_radians;
  correction.movement_mode = player_state.movement_mode;
  correction.game_mode_id = player_state.game_mode_id;
  correction.resolved_core_permissions = player_state.resolved_permissions.core;
  correction.grounded = player_state.grounded;
  correction.sneaking = player_state.sneaking;
  correction.swimming = player_state.swimming;
  correction.breath_seconds = player_state.breath_seconds;
  correction.selected_hotbar_slot = player_state.selected_hotbar_slot;
  if (block_break != nullptr) {
    correction.block_break = *block_break;
  }
  return correction;
}

[[nodiscard]] bool same_correction_payload(
    const protocol::PlayerCorrection& lhs,
    const protocol::PlayerCorrection& rhs) {
  return lhs.player_id == rhs.player_id &&
         lhs.world_id == rhs.world_id &&
         lhs.position == rhs.position &&
         lhs.velocity == rhs.velocity &&
         lhs.yaw_radians == rhs.yaw_radians &&
         lhs.pitch_radians == rhs.pitch_radians &&
         lhs.movement_mode == rhs.movement_mode &&
         lhs.game_mode_id == rhs.game_mode_id &&
         lhs.resolved_core_permissions == rhs.resolved_core_permissions &&
         lhs.grounded == rhs.grounded &&
         lhs.sneaking == rhs.sneaking &&
         lhs.swimming == rhs.swimming &&
         lhs.breath_seconds == rhs.breath_seconds &&
         lhs.selected_hotbar_slot == rhs.selected_hotbar_slot &&
         lhs.block_break == rhs.block_break;
}

} // namespace

void ServerRuntimeImpl::send_player_corrections() {
  for (const SessionID session_id : sessions_.session_ids()) {
    ServerSession* session = sessions_.find(session_id);
    if (session == nullptr) {
      continue;
    }

    const player::PlayerState* player_state = default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    protocol::BlockBreakProgress block_break;
    if (const auto found = block_break_attempts_.find(player_state->player_id);
        found != block_break_attempts_.end()) {
      block_break.active = true;
      block_break.block = found->second.block;
      block_break.variant = found->second.variant;
      block_break.progress = found->second.progress;
      block_break.stage = found->second.stage;
    }
    protocol::PlayerCorrection correction = make_player_correction(
        tick_index_,
        *player_state,
        block_break.active ? &block_break : nullptr);
    if (const auto found = last_sent_player_corrections_.find(player_state->player_id);
        found != last_sent_player_corrections_.end() &&
        same_correction_payload(found->second, correction)) {
      continue;
    }

    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        session->connection_id,
        protocol::MessageType::PlayerCorrection,
        protocol::ReliabilityClass::UnreliableLatestWins,
        protocol::NetworkLane::Movement,
        protocol::serialize_player_correction(correction));
    if (send_result) {
      last_sent_player_corrections_[player_state->player_id] = std::move(correction);
    }
  }
}

void ServerRuntimeImpl::send_block_updates() {
  std::vector<world::ReplicatedBlockUpdate> updates =
      default_world_.drain_replicated_block_updates();
  if (updates.empty()) {
    return;
  }

  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || session->world_id != default_world_.id()) {
      continue;
    }

    protocol::BlockUpdateBatch batch;
    batch.world_id = session->world_id;
    batch.server_tick = tick_index_;
    for (const world::ReplicatedBlockUpdate& update : updates) {
      if (!chunk_streamer_.session_has_sent_chunk(
              session_id,
              session->world_id,
              block_to_chunk(update.pos))) {
        continue;
      }
      batch.updates.push_back({
          update.pos,
          update.new_state,
          0U,
          update.server_tick});
    }
    if (batch.updates.empty()) {
      continue;
    }

    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        session->connection_id,
        protocol::MessageType::BlockUpdateBatch,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_block_update_batch(batch));
    if (send_result) {
      for (const protocol::BlockUpdate& update : batch.updates) {
        const ChunkCoord coord = block_to_chunk(update.pos);
        if (const voxel::ChunkSection* chunk = default_world_.find_chunk(coord);
            chunk != nullptr) {
          (void)chunk_streamer_.mark_sent_block_version(
              session_id,
              session->world_id,
              coord,
              chunk->block_version());
        }
      }
    }
  }
}

void ServerRuntimeImpl::send_entity_replication() {
  const std::vector<entity::EntitySnapshotEntry> snapshots =
      default_world_.collect_entity_snapshots();

  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || session->world_id != default_world_.id()) {
      continue;
    }

    EntityReplicationState& state = entity_replication_[session_id];
    HashSet<EntityNetID> relevant_now;
    std::vector<protocol::EntitySnapshotEntry> snapshot_entries;

    for (const entity::EntitySnapshotEntry& snapshot : snapshots) {
      const bool owns_entity =
          snapshot.owning_player.has_value() &&
          snapshot.owning_player.value() == session->player_id;
      const bool chunk_visible = chunk_streamer_.session_has_sent_chunk(
          session_id,
          session->world_id,
          snapshot.chunk);
      if (!owns_entity && !chunk_visible) {
        continue;
      }

      relevant_now.insert(snapshot.net_id);
      if (state.known_entities.find(snapshot.net_id) == state.known_entities.end()) {
        protocol::EntitySpawn spawn;
        spawn.world_id = session->world_id;
        spawn.entity_id = snapshot.net_id;
        spawn.entity_type = snapshot.type_id;
        spawn.entity_type_name = snapshot.type_name;
        spawn.position = snapshot.position;
        spawn.velocity = snapshot.velocity;
        spawn.rotation = snapshot.rotation;
        spawn.owning_player = snapshot.owning_player;
        (void)send_protocol_message(
            *transport_,
            session->connection_id,
            protocol::MessageType::EntitySpawn,
            protocol::ReliabilityClass::ReliableOrdered,
            protocol::NetworkLane::Gameplay,
            protocol::serialize_entity_spawn(spawn));
      }

      snapshot_entries.push_back({
          snapshot.net_id,
          snapshot.position,
          snapshot.velocity,
          snapshot.rotation,
          0U});
    }

    for (const EntityNetID known_entity : state.known_entities) {
      if (relevant_now.find(known_entity) != relevant_now.end()) {
        continue;
      }

      protocol::EntityDespawn despawn{session->world_id, known_entity, 0U};
      (void)send_protocol_message(
          *transport_,
          session->connection_id,
          protocol::MessageType::EntityDespawn,
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Gameplay,
          protocol::serialize_entity_despawn(despawn));
    }

    state.known_entities = std::move(relevant_now);
    if (snapshot_entries.empty()) {
      continue;
    }

    // TODO(arvane): Replace full snapshot entries with per-component delta
    // compression once component replication masks are specified.
    protocol::EntitySnapshot snapshot_message;
    snapshot_message.world_id = session->world_id;
    snapshot_message.server_tick = tick_index_;
    snapshot_message.entities = std::move(snapshot_entries);
    (void)send_protocol_message(
        *transport_,
        session->connection_id,
        protocol::MessageType::EntitySnapshot,
        protocol::ReliabilityClass::UnreliableLatestWins,
        protocol::NetworkLane::Movement,
        protocol::serialize_entity_snapshot(snapshot_message));
  }
}

} // namespace arv::server::detail
