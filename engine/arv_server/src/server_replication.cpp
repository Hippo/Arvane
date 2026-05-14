#include "server_runtime_internal.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace arv::server::detail {
namespace {

[[nodiscard]] protocol::ClientboundPlayerMoveCorrection make_player_move_correction(
    u32 rejected_sequence,
    const player::PlayerState& player_state,
    protocol::PlayerMoveCorrectionReason reason) {
  protocol::ClientboundPlayerMoveCorrection correction;
  correction.rejected_sequence = rejected_sequence;
  correction.position = player_state.position;
  correction.velocity = player_state.velocity;
  correction.yaw_radians = player_state.yaw_radians;
  correction.pitch_radians = player_state.pitch_radians;
  correction.flags = player_move_flags_from_state(player_state);
  correction.reason = reason;
  return correction;
}

} // namespace

void ServerRuntimeImpl::send_player_move_correction(
    const ServerSession& session,
    const player::PlayerState& player_state,
    u32 rejected_sequence,
    protocol::PlayerMoveCorrectionReason reason) {
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(base_tps_, 1U));
  const double cooldown_seconds =
      static_cast<double>(movement_.correction_cooldown_ms) / 1000.0;
  if (const auto found = last_player_move_correction_seconds_.find(player_state.player_id);
      found != last_player_move_correction_seconds_.end() &&
      now_seconds - found->second < cooldown_seconds) {
    return;
  }

  protocol::ClientboundPlayerMoveCorrection correction =
      make_player_move_correction(rejected_sequence, player_state, reason);
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundPlayerMoveCorrection,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Movement,
      protocol::serialize_clientbound_player_move_correction(correction));
  if (send_result) {
    last_player_move_correction_seconds_[player_state.player_id] = now_seconds;
  }
}

void ServerRuntimeImpl::send_player_velocity(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  protocol::ClientboundPlayerVelocity velocity;
  velocity.server_tick = tick_index_;
  velocity.player_id = player_state.player_id;
  velocity.velocity = player_state.velocity;
  velocity.knockback_control_suppression_ticks =
      player_state.knockback_control_suppression_ticks;
  (void)send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundPlayerVelocity,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Movement,
      protocol::serialize_clientbound_player_velocity(velocity));
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

    protocol::ClientboundBlockUpdateBatch batch;
    batch.world_id = session->world_id;
    batch.server_tick = tick_index_;
    for (const world::ReplicatedBlockUpdate& update : updates) {
      if (!chunk_streamer_.session_has_sent_chunk(
              session_id,
              session->world_id,
              block_to_chunk(update.pos))) {
        continue;
      }
      batch.updates.push_back({update.pos,
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
        protocol::MessageType::ClientboundBlockUpdateBatch,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_clientbound_block_update_batch(batch));
    if (send_result) {
      for (const protocol::BlockUpdate& update : batch.updates) {
        const ChunkSectionCoord coord = block_to_chunk(update.pos);
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

void ServerRuntimeImpl::send_player_block_break_progress() {
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || session->world_id != default_world_.id()) {
      continue;
    }

    const player::PlayerState* player_state = default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    protocol::BlockBreakProgress progress;
    if (const auto found = block_break_attempts_.find(player_state->player_id);
        found != block_break_attempts_.end()) {
      progress.active = true;
      progress.block = found->second.block;
      progress.variant = found->second.variant;
      progress.progress = found->second.progress;
      progress.stage = found->second.stage;
    }

    const auto last = last_sent_block_break_progress_.find(player_state->player_id);
    if (last != last_sent_block_break_progress_.end() && last->second == progress) {
      continue;
    }
    if (last == last_sent_block_break_progress_.end() && !progress.active) {
      continue;
    }

    protocol::ClientboundPlayerBlockBreakProgress packet;
    packet.server_tick = tick_index_;
    packet.player_id = player_state->player_id;
    packet.block_break = progress;
    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        session->connection_id,
        protocol::MessageType::ClientboundPlayerBlockBreakProgress,
        protocol::ReliabilityClass::UnreliableLatestWins,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_clientbound_player_block_break_progress(packet));
    if (!send_result) {
      continue;
    }
    if (progress.active) {
      last_sent_block_break_progress_[player_state->player_id] = progress;
    } else {
      last_sent_block_break_progress_.erase(player_state->player_id);
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
        protocol::ClientboundEntitySpawn spawn;
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
            protocol::MessageType::ClientboundEntitySpawn,
            protocol::ReliabilityClass::ReliableOrdered,
            protocol::NetworkLane::Gameplay,
            protocol::serialize_clientbound_entity_spawn(spawn));
      }

      protocol::EntitySnapshotEntry entry;
      entry.entity_id = snapshot.net_id;
      entry.position = snapshot.position;
      entry.velocity = snapshot.velocity;
      entry.rotation = snapshot.rotation;
      entry.state_flags = 0U;
      if (snapshot.has_health) {
        entry.state_flags |= protocol::EntityStateFlagHasHealth;
        if (snapshot.health_current <= 0.0F) {
          entry.state_flags |= protocol::EntityStateFlagDead;
        }
        if (snapshot.health_invulnerable) {
          entry.state_flags |= protocol::EntityStateFlagInvulnerable;
        }
        entry.health_current = snapshot.health_current;
        entry.health_max = snapshot.health_max;
      }
      entry.animations.reserve(snapshot.animations.size());
      for (const entity::EntityAnimationPlayback& animation : snapshot.animations) {
        entry.animations.push_back({animation.name, animation.time_seconds});
      }
      entry.head_rotation_degrees = snapshot.head_rotation_degrees;
      snapshot_entries.push_back(std::move(entry));
    }

    for (const EntityNetID known_entity : state.known_entities) {
      if (relevant_now.find(known_entity) != relevant_now.end()) {
        continue;
      }

      protocol::ClientboundEntityDespawn despawn{session->world_id, known_entity, 0U};
      (void)send_protocol_message(
          *transport_,
          session->connection_id,
          protocol::MessageType::ClientboundEntityDespawn,
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Gameplay,
          protocol::serialize_clientbound_entity_despawn(despawn));
    }

    state.known_entities = std::move(relevant_now);
    if (snapshot_entries.empty()) {
      continue;
    }

    // TODO(arvane): Replace full snapshot entries with per-component delta
    // compression once component replication masks are specified.
    protocol::ClientboundEntitySnapshot snapshot_message;
    snapshot_message.world_id = session->world_id;
    snapshot_message.server_tick = tick_index_;
    snapshot_message.entities = std::move(snapshot_entries);
    (void)send_protocol_message(
        *transport_,
        session->connection_id,
        protocol::MessageType::ClientboundEntitySnapshot,
        protocol::ReliabilityClass::UnreliableLatestWins,
        protocol::NetworkLane::Movement,
        protocol::serialize_clientbound_entity_snapshot(snapshot_message));
  }
}

} // namespace arv::server::detail
