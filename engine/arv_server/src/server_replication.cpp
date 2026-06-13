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

[[nodiscard]] u32 block_particle_seed(BlockPos pos, TickIndex tick_index) noexcept {
  const u64 x = static_cast<u64>(static_cast<i64>(pos.value.x) * 73856093LL);
  const u64 y = static_cast<u64>(static_cast<i64>(pos.value.y) * 19349663LL);
  const u64 z = static_cast<u64>(static_cast<i64>(pos.value.z) * 83492791LL);
  return static_cast<u32>(x ^ y ^ z ^ tick_index);
}

[[nodiscard]] protocol::ClientboundSpawnParticles make_block_break_particles(
    WorldID world_id,
    const world::ReplicatedBlockUpdate& update,
    const voxel::BlockRegistry& block_registry) {
  protocol::ClientboundSpawnParticles particles;
  particles.world_id = world_id;
  particles.particle_type = "arvane:block_dust";
  if (const voxel::BlockVariantRecord* variant = block_registry.variant(update.old_state);
      variant != nullptr) {
    particles.texture_asset = variant->particle_texture_asset;
  }
  particles.position = {
      static_cast<f32>(update.pos.value.x) + 0.5F,
      static_cast<f32>(update.pos.value.y) + 0.5F,
      static_cast<f32>(update.pos.value.z) + 0.5F};
  particles.normal = {0.0F, 1.0F, 0.0F};
  particles.extents = {0.46F, 0.46F, 0.46F};
  particles.radius = 0.0F;
  particles.count = 28U;
  particles.seed = block_particle_seed(update.pos, update.server_tick);
  particles.shape = protocol::ParticleSpawnShape::Box;
  return particles;
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

void ServerRuntimeImpl::send_vehicle_correction(
    const ServerSession& session,
    EntityNetID vehicle_net_id,
    const AcceptedVehicleMovementState& accepted,
    u32 rejected_sequence,
    protocol::VehicleCorrectionReason reason) {
  protocol::ClientboundVehicleCorrection correction;
  correction.vehicle_id = vehicle_net_id;
  correction.rejected_sequence = rejected_sequence;
  correction.position = accepted.position;
  correction.velocity = accepted.velocity;
  correction.rotation = accepted.rotation;
  correction.angular_velocity = accepted.angular_velocity;
  correction.reason = reason;
  (void)send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundVehicleCorrection,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Movement,
      protocol::serialize_clientbound_vehicle_correction(correction));
}

void ServerRuntimeImpl::broadcast_vehicle_state(
    EntityNetID vehicle_net_id,
    const AcceptedVehicleMovementState& accepted) {
  protocol::ClientboundVehicleState state;
  state.world_id = default_world_.id();
  state.vehicle_id = vehicle_net_id;
  state.server_tick = tick_index_;
  state.accepted_sequence = accepted.last_sequence;
  state.position = accepted.position;
  state.velocity = accepted.velocity;
  state.rotation = accepted.rotation;
  state.angular_velocity = accepted.angular_velocity;
  state.flags = accepted.flags;
  const std::vector<u8> payload = protocol::serialize_clientbound_vehicle_state(state);
  sessions_.for_each_session([&](const ServerSession& session) {
    if (!session.authenticated || session.closing ||
        session.world_id != default_world_.id()) {
      return;
    }
    (void)send_protocol_message(
        *transport_,
        session.connection_id,
        protocol::MessageType::ClientboundVehicleState,
        protocol::ReliabilityClass::UnreliableLatestWins,
        protocol::NetworkLane::Movement,
        payload);
  });
}

void ServerRuntimeImpl::broadcast_vehicle_smoke_particles(
    Vec3f position,
    VehicleDamageStage stage,
    u32 seed) {
  if (stage != VehicleDamageStage::Damaged &&
      stage != VehicleDamageStage::Critical) {
    return;
  }
  protocol::ClientboundSpawnParticles particles;
  particles.world_id = default_world_.id();
  particles.particle_type = "arvane:vehicle_smoke";
  particles.position = position;
  particles.normal = {0.0F, 1.0F, 0.0F};
  particles.extents =
      stage == VehicleDamageStage::Critical
          ? Vec3f{0.6F, 0.35F, 0.6F}
          : Vec3f{0.4F, 0.22F, 0.4F};
  particles.radius = stage == VehicleDamageStage::Critical ? 0.75F : 0.5F;
  particles.count = stage == VehicleDamageStage::Critical ? 14U : 7U;
  particles.seed = seed;
  particles.shape = protocol::ParticleSpawnShape::Sphere;
  const std::vector<u8> payload =
      protocol::serialize_clientbound_spawn_particles(particles);
  sessions_.for_each_session([&](const ServerSession& session) {
    if (!session.authenticated || session.closing ||
        session.world_id != default_world_.id()) {
      return;
    }
    (void)send_protocol_message(
        *transport_,
        session.connection_id,
        protocol::MessageType::ClientboundSpawnParticles,
        protocol::ReliabilityClass::UnreliableEvent,
        protocol::NetworkLane::Gameplay,
        payload);
  });
}

void ServerRuntimeImpl::broadcast_entity_passengers(EntityID rideable_id) {
  const entity::RideableComponent* rideable =
      default_world_.find_entity_rideable(rideable_id);
  const std::optional<EntityNetID> rideable_net =
      default_world_.network_id_for_entity(rideable_id);
  if (rideable == nullptr || !rideable_net.has_value()) {
    return;
  }

  protocol::ClientboundEntityPassengersSet packet;
  packet.rideable_id = rideable_net.value();
  for (const entity::Seat& seat : rideable->seats) {
    if (seat.occupant.value == 0U) {
      continue;
    }
    const std::optional<EntityNetID> passenger_net =
        default_world_.network_id_for_entity(seat.occupant);
    if (!passenger_net.has_value()) {
      continue;
    }
    packet.passengers.push_back(
        {passenger_net.value(), seat.id, seat.controls_vehicle});
  }
  const std::vector<u8> payload =
      protocol::serialize_clientbound_entity_passengers_set(packet);
  sessions_.for_each_session([&](const ServerSession& session) {
    if (!session.authenticated || session.closing ||
        session.world_id != default_world_.id()) {
      return;
    }
    (void)send_protocol_message(
        *transport_,
        session.connection_id,
        protocol::MessageType::ClientboundEntityPassengersSet,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        payload);
  });
}

void ServerRuntimeImpl::broadcast_ride_started(
    EntityID passenger_id,
    EntityID rideable_id,
    std::string_view seat_id,
    bool controls_vehicle) {
  const std::optional<EntityNetID> passenger_net =
      default_world_.network_id_for_entity(passenger_id);
  const std::optional<EntityNetID> rideable_net =
      default_world_.network_id_for_entity(rideable_id);
  if (!passenger_net.has_value() || !rideable_net.has_value()) {
    return;
  }
  protocol::ClientboundRideStarted packet;
  packet.passenger_id = passenger_net.value();
  packet.rideable_id = rideable_net.value();
  packet.seat_id = std::string{seat_id};
  packet.controls_vehicle = controls_vehicle;
  const std::vector<u8> payload = protocol::serialize_clientbound_ride_started(packet);
  sessions_.for_each_session([&](const ServerSession& session) {
    if (!session.authenticated || session.closing ||
        session.world_id != default_world_.id()) {
      return;
    }
    (void)send_protocol_message(
        *transport_,
        session.connection_id,
        protocol::MessageType::ClientboundRideStarted,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        payload);
  });
  broadcast_entity_passengers(rideable_id);
}

void ServerRuntimeImpl::broadcast_ride_ended(
    EntityID passenger_id,
    EntityID rideable_id,
    Vec3f position) {
  const std::optional<EntityNetID> passenger_net =
      default_world_.network_id_for_entity(passenger_id);
  const std::optional<EntityNetID> rideable_net =
      default_world_.network_id_for_entity(rideable_id);
  if (!passenger_net.has_value() || !rideable_net.has_value()) {
    return;
  }
  broadcast_ride_ended(passenger_net.value(), rideable_net.value(), position);
  broadcast_entity_passengers(rideable_id);
}

void ServerRuntimeImpl::broadcast_ride_ended(
    EntityNetID passenger_net_id,
    EntityNetID rideable_net_id,
    Vec3f position) {
  protocol::ClientboundRideEnded packet;
  packet.passenger_id = passenger_net_id;
  packet.rideable_id = rideable_net_id;
  packet.position = position;
  const std::vector<u8> payload = protocol::serialize_clientbound_ride_ended(packet);
  sessions_.for_each_session([&](const ServerSession& session) {
    if (!session.authenticated || session.closing ||
        session.world_id != default_world_.id()) {
      return;
    }
    (void)send_protocol_message(
        *transport_,
        session.connection_id,
        protocol::MessageType::ClientboundRideEnded,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        payload);
  });
}

void ServerRuntimeImpl::broadcast_pending_ride_ended_events() {
  for (const world::RideEndedEvent& event : default_world_.drain_ride_ended_events()) {
    if (!event.passenger_net_id.has_value() || !event.rideable_net_id.has_value()) {
      continue;
    }
    broadcast_ride_ended(
        event.passenger_net_id.value(),
        event.rideable_net_id.value(),
        event.position);
    accepted_vehicle_movement_.erase(event.rideable_net_id.value());
    if (default_world_.entity_exists(event.rideable)) {
      broadcast_entity_passengers(event.rideable);
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
      protocol::BlockUpdate packet_update;
      packet_update.pos = update.pos;
      packet_update.new_state = update.new_state;
      packet_update.update_flags = 0U;
      packet_update.server_tick = update.server_tick;
      if (update.new_fluid.has_value()) {
        packet_update.has_fluid = true;
        packet_update.new_fluid = update.new_fluid.value();
      }
      batch.updates.push_back(packet_update);
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
      for (const world::ReplicatedBlockUpdate& update : updates) {
        if (!chunk_streamer_.session_has_sent_chunk(
                session_id,
                session->world_id,
                block_to_chunk(update.pos))) {
          continue;
        }
        const ChunkSectionCoord coord = block_to_chunk(update.pos);
        if (const voxel::ChunkSection* chunk = default_world_.find_chunk(coord);
            chunk != nullptr) {
          (void)chunk_streamer_.mark_sent_block_version(
              session_id,
              session->world_id,
              coord,
              chunk->block_version());
        }
        if (block_registry_ != nullptr &&
            update.new_state == voxel::AirBlockVariantID &&
            update.old_state != voxel::AirBlockVariantID) {
          const protocol::ClientboundSpawnParticles particles =
              make_block_break_particles(session->world_id, update, *block_registry_);
          (void)send_protocol_message(
              *transport_,
              session->connection_id,
              protocol::MessageType::ClientboundSpawnParticles,
              protocol::ReliabilityClass::UnreliableEvent,
              protocol::NetworkLane::Gameplay,
              protocol::serialize_clientbound_spawn_particles(particles));
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
    if (session == nullptr || !session->authenticated || session->closing ||
        session->connection_state != net::ConnectionState::InGame ||
        session->world_id != default_world_.id()) {
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
