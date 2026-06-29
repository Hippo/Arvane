#include "client_entity_mirror.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::client {
namespace {

inline constexpr Seconds MinEntitySmoothingSeconds = 1.0F / 30.0F;
inline constexpr Seconds MaxEntitySmoothingSeconds = 0.12F;
inline constexpr f32 PositionChangeEpsilonSquared = 0.000001F;
inline constexpr f32 TeleportSnapDistanceSquared = 16.0F * 16.0F;

[[nodiscard]] f32 quat_dot(Quatf lhs, Quatf rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

[[nodiscard]] Quatf lerp_quat_shortest(Quatf from, Quatf to, f32 alpha) noexcept {
  if (quat_dot(from, to) < 0.0F) {
    to = {-to.x, -to.y, -to.z, -to.w};
  }
  return normalize_quat({from.x + (to.x - from.x) * alpha,
                         from.y + (to.y - from.y) * alpha,
                         from.z + (to.z - from.z) * alpha,
                         from.w + (to.w - from.w) * alpha});
}

[[nodiscard]] bool rotation_changed(Quatf from, Quatf to) noexcept {
  return std::abs(quat_dot(normalize_quat(from), normalize_quat(to))) < 0.9999F;
}

[[nodiscard]] Seconds smoothing_duration_seconds(
    u64 tick_delta,
    TickRate tick_rate) noexcept {
  const TickRate safe_tick_rate = tick_rate == 0U ? DefaultBaseTps : tick_rate;
  const Seconds from_ticks =
      static_cast<Seconds>(std::max<u64>(tick_delta, 1U)) *
      tick_duration_seconds(safe_tick_rate);
  return std::clamp(
      from_ticks,
      MinEntitySmoothingSeconds,
      MaxEntitySmoothingSeconds);
}

void begin_entity_smoothing(
    ReplicatedEntityMirror& mirror,
    u64 server_tick,
    TickRate server_tick_rate) noexcept {
  const u64 previous_change_tick = mirror.last_transform_change_server_tick;
  const u64 tick_delta =
      previous_change_tick != 0U && server_tick > previous_change_tick
          ? server_tick - previous_change_tick
          : 1U;
  mirror.interpolation_start_position = mirror.render_position;
  mirror.interpolation_start_rotation = mirror.render_rotation;
  mirror.interpolation_elapsed_seconds = 0.0F;
  mirror.interpolation_duration_seconds =
      smoothing_duration_seconds(tick_delta, server_tick_rate);
  mirror.last_transform_change_server_tick = server_tick;
}

void snap_entity_render_transform(ReplicatedEntityMirror& mirror) noexcept {
  mirror.render_position = mirror.position;
  mirror.render_rotation = mirror.rotation;
  mirror.interpolation_start_position = mirror.position;
  mirror.interpolation_start_rotation = mirror.rotation;
  mirror.interpolation_elapsed_seconds = 0.0F;
  mirror.interpolation_duration_seconds = 0.0F;
}

[[nodiscard]] bool matches_current_or_pending_world(
    WorldID packet_world,
    WorldID current_world) noexcept {
  return current_world.value == 0U || packet_world == current_world;
}

using EntityMirrorMap = HashMap<EntityNetID, ReplicatedEntityMirror>;

[[nodiscard]] bool has_entity(
    const EntityMirrorMap& entities,
    EntityNetID entity_id) {
  return entities.find(entity_id) != entities.end();
}

[[nodiscard]] bool ride_started_ready(
    const EntityMirrorMap& entities,
    const protocol::ClientboundRideStarted& event) {
  return has_entity(entities, event.passenger_id) &&
         has_entity(entities, event.rideable_id);
}

void apply_ride_started_to_existing(
    EntityMirrorMap& entities,
    const protocol::ClientboundRideStarted& event) {
  auto passenger = entities.find(event.passenger_id);
  if (passenger != entities.end()) {
    passenger->second.parent_rideable = event.rideable_id;
    passenger->second.parent_seat_id = event.seat_id;
    passenger->second.controls_parent_vehicle = event.controls_vehicle;
  }
  auto rideable = entities.find(event.rideable_id);
  if (rideable == entities.end()) {
    return;
  }
  for (ReplicatedPassengerMirror& existing : rideable->second.passengers) {
    if (existing.passenger_id == event.passenger_id) {
      existing.seat_id = event.seat_id;
      existing.controls_vehicle = event.controls_vehicle;
      return;
    }
  }
  rideable->second.passengers.push_back(
      {event.passenger_id, event.seat_id, event.controls_vehicle});
}

[[nodiscard]] bool passengers_set_ready(
    const EntityMirrorMap& entities,
    const protocol::ClientboundEntityPassengersSet& event) {
  if (!has_entity(entities, event.rideable_id)) {
    return false;
  }
  return std::all_of(
      event.passengers.begin(),
      event.passengers.end(),
      [&](const protocol::VehiclePassengerEntry& passenger) {
        return has_entity(entities, passenger.passenger_id);
      });
}

void apply_passengers_set_to_existing(
    EntityMirrorMap& entities,
    const protocol::ClientboundEntityPassengersSet& event) {
  auto rideable = entities.find(event.rideable_id);
  if (rideable != entities.end()) {
    rideable->second.passengers.clear();
    rideable->second.passengers.reserve(event.passengers.size());
  }
  for (auto& [entity_id, mirror] : entities) {
    if (mirror.parent_rideable.has_value() &&
        mirror.parent_rideable.value() == event.rideable_id) {
      mirror.parent_rideable.reset();
      mirror.parent_seat_id.clear();
      mirror.controls_parent_vehicle = false;
    }
    (void)entity_id;
  }
  for (const protocol::VehiclePassengerEntry& passenger : event.passengers) {
    if (rideable != entities.end()) {
      rideable->second.passengers.push_back(
          {passenger.passenger_id, passenger.seat_id, passenger.controls_vehicle});
    }
    auto passenger_mirror = entities.find(passenger.passenger_id);
    if (passenger_mirror == entities.end()) {
      continue;
    }
    passenger_mirror->second.parent_rideable = event.rideable_id;
    passenger_mirror->second.parent_seat_id = passenger.seat_id;
    passenger_mirror->second.controls_parent_vehicle =
        passenger.controls_vehicle;
  }
}

void upsert_pending_ride_start(
    std::vector<protocol::ClientboundRideStarted>& pending,
    const protocol::ClientboundRideStarted& event) {
  std::erase_if(
      pending,
      [&](const protocol::ClientboundRideStarted& existing) {
        return existing.passenger_id == event.passenger_id;
      });
  pending.push_back(event);
}

void upsert_pending_passengers_set(
    std::vector<protocol::ClientboundEntityPassengersSet>& pending,
    const protocol::ClientboundEntityPassengersSet& event) {
  std::erase_if(
      pending,
      [&](const protocol::ClientboundEntityPassengersSet& existing) {
        return existing.rideable_id == event.rideable_id;
      });
  pending.push_back(event);
}

} // namespace

void ClientEntityMirrorStore::apply_spawn(
    protocol::ClientboundEntitySpawn spawn,
    WorldID current_world) {
  if (!matches_current_or_pending_world(spawn.world_id, current_world)) {
    return;
  }
  const auto found = entities_.find(spawn.entity_id);
  std::optional<EntityNetID> parent_rideable;
  std::string parent_seat_id;
  bool controls_parent_vehicle = false;
  std::vector<ReplicatedPassengerMirror> passengers;
  if (found != entities_.end()) {
    parent_rideable = found->second.parent_rideable;
    parent_seat_id = found->second.parent_seat_id;
    controls_parent_vehicle = found->second.controls_parent_vehicle;
    passengers = found->second.passengers;
  }
  ReplicatedEntityMirror& mirror = entities_[spawn.entity_id];
  mirror.entity_id = spawn.entity_id;
  mirror.entity_type = spawn.entity_type;
  mirror.entity_type_name = std::move(spawn.entity_type_name);
  mirror.world_id = spawn.world_id;
  mirror.previous_position = spawn.position;
  mirror.position = spawn.position;
  mirror.velocity = spawn.velocity;
  mirror.render_position = spawn.position;
  mirror.interpolation_start_position = spawn.position;
  mirror.previous_rotation = spawn.rotation;
  mirror.rotation = spawn.rotation;
  mirror.render_rotation = spawn.rotation;
  mirror.interpolation_start_rotation = spawn.rotation;
  mirror.last_transform_change_server_tick = 0U;
  mirror.interpolation_elapsed_seconds = 0.0F;
  mirror.interpolation_duration_seconds = 0.0F;
  mirror.owning_player = spawn.owning_player;
  mirror.animations.clear();
  mirror.head_rotation_degrees = {};
  mirror.has_health = false;
  mirror.health_current = 0.0F;
  mirror.health_max = 0.0F;
  mirror.dead = false;
  mirror.invulnerable = false;
  mirror.parent_rideable = parent_rideable;
  mirror.parent_seat_id = std::move(parent_seat_id);
  mirror.controls_parent_vehicle = controls_parent_vehicle;
  mirror.passengers = std::move(passengers);
  apply_pending_relationships();
}

void ClientEntityMirrorStore::apply_despawn(
    const protocol::ClientboundEntityDespawn& despawn,
    WorldID current_world) {
  if (!matches_current_or_pending_world(despawn.world_id, current_world)) {
    return;
  }
  for (auto& [entity_id, mirror] : entities_) {
    (void)entity_id;
    if (mirror.parent_rideable.has_value() &&
        mirror.parent_rideable.value() == despawn.entity_id) {
      mirror.parent_rideable.reset();
      mirror.parent_seat_id.clear();
      mirror.controls_parent_vehicle = false;
    }
    std::erase_if(
        mirror.passengers,
        [&](const ReplicatedPassengerMirror& passenger) {
          return passenger.passenger_id == despawn.entity_id;
        });
  }
  entities_.erase(despawn.entity_id);
  std::erase_if(
      pending_ride_starts_,
      [&](const protocol::ClientboundRideStarted& event) {
        return event.passenger_id == despawn.entity_id ||
               event.rideable_id == despawn.entity_id;
      });
  std::erase_if(
      pending_passenger_sets_,
      [&](protocol::ClientboundEntityPassengersSet& event) {
        if (event.rideable_id == despawn.entity_id) {
          return true;
        }
        std::erase_if(
            event.passengers,
            [&](const protocol::VehiclePassengerEntry& passenger) {
              return passenger.passenger_id == despawn.entity_id;
            });
        return false;
      });
}

void ClientEntityMirrorStore::apply_snapshot(
    const protocol::ClientboundEntitySnapshot& snapshot,
    WorldID current_world,
    TickRate server_tick_rate,
    std::optional<EntityNetID> locally_controlled_vehicle) {
  if (!matches_current_or_pending_world(snapshot.world_id, current_world)) {
    return;
  }
  for (const protocol::EntitySnapshotEntry& entry : snapshot.entities) {
    auto found = entities_.find(entry.entity_id);
    if (found == entities_.end()) {
      continue;
    }
    ReplicatedEntityMirror& mirror = found->second;
    if (snapshot.server_tick < mirror.last_server_tick) {
      continue;
    }
    const bool local_driver_owned_transform =
        locally_controlled_vehicle.has_value() &&
        locally_controlled_vehicle.value() == entry.entity_id;
    if (local_driver_owned_transform) {
      mirror.last_server_tick = snapshot.server_tick;
      mirror.head_rotation_degrees = entry.head_rotation_degrees;
      mirror.has_health =
          (entry.state_flags & protocol::EntityStateFlagHasHealth) != 0U;
      mirror.dead =
          (entry.state_flags & protocol::EntityStateFlagDead) != 0U;
      mirror.invulnerable =
          (entry.state_flags & protocol::EntityStateFlagInvulnerable) != 0U;
      mirror.health_current = entry.health_current;
      mirror.health_max = entry.health_max;
      continue;
    }
    const bool moved =
        distance_squared(entry.position, mirror.position) >
        PositionChangeEpsilonSquared;
    const bool rotated = rotation_changed(entry.rotation, mirror.rotation);
    const bool snap_transform =
        (moved || rotated) &&
        distance_squared(entry.position, mirror.render_position) >
            TeleportSnapDistanceSquared;
    if (!snap_transform && (moved || rotated)) {
      begin_entity_smoothing(mirror, snapshot.server_tick, server_tick_rate);
    } else if (mirror.last_transform_change_server_tick == 0U) {
      mirror.last_transform_change_server_tick = snapshot.server_tick;
    }
    mirror.last_server_tick = snapshot.server_tick;
    mirror.previous_position = mirror.position;
    mirror.previous_rotation = mirror.rotation;
    mirror.position = entry.position;
    mirror.velocity = entry.velocity;
    mirror.rotation = entry.rotation;
    mirror.animations = entry.animations;
    mirror.head_rotation_degrees = entry.head_rotation_degrees;
    mirror.has_health =
        (entry.state_flags & protocol::EntityStateFlagHasHealth) != 0U;
    mirror.dead =
        (entry.state_flags & protocol::EntityStateFlagDead) != 0U;
    mirror.invulnerable =
        (entry.state_flags & protocol::EntityStateFlagInvulnerable) != 0U;
    mirror.health_current = entry.health_current;
    mirror.health_max = entry.health_max;
    if (snap_transform) {
      mirror.last_transform_change_server_tick = snapshot.server_tick;
      snap_entity_render_transform(mirror);
    }
  }
}

void ClientEntityMirrorStore::apply_vehicle_state(
    const protocol::ClientboundVehicleState& state,
    WorldID current_world,
    TickRate server_tick_rate,
    bool local_driver_owns_vehicle) {
  if (state.world_id != current_world) {
    return;
  }
  if (local_driver_owns_vehicle) {
    return;
  }
  auto found = entities_.find(state.vehicle_id);
  if (found == entities_.end()) {
    return;
  }
  ReplicatedEntityMirror& mirror = found->second;
  if (state.server_tick < mirror.last_server_tick) {
    return;
  }
  const bool moved =
      distance_squared(state.position, mirror.position) >
      PositionChangeEpsilonSquared;
  const bool rotated = rotation_changed(state.rotation, mirror.rotation);
  if (moved || rotated) {
    begin_entity_smoothing(mirror, state.server_tick, server_tick_rate);
  }
  mirror.last_server_tick = state.server_tick;
  mirror.previous_position = mirror.position;
  mirror.previous_rotation = mirror.rotation;
  mirror.position = state.position;
  mirror.velocity = state.velocity;
  mirror.rotation = state.rotation;
}

void ClientEntityMirrorStore::apply_vehicle_correction(
    const protocol::ClientboundVehicleCorrection& correction) {
  auto found = entities_.find(correction.vehicle_id);
  if (found == entities_.end()) {
    return;
  }
  ReplicatedEntityMirror& mirror = found->second;
  mirror.previous_position = mirror.position;
  mirror.previous_rotation = mirror.rotation;
  mirror.position = correction.position;
  mirror.velocity = correction.velocity;
  mirror.rotation = correction.rotation;
  mirror.last_transform_change_server_tick = mirror.last_server_tick;
  snap_entity_render_transform(mirror);
}

void ClientEntityMirrorStore::apply_local_vehicle_prediction(
    EntityNetID vehicle_id,
    Vec3f position,
    Vec3f velocity,
    Quatf rotation,
    std::span<const protocol::EntityAnimationPlayback> animations) {
  auto found = entities_.find(vehicle_id);
  if (found == entities_.end()) {
    return;
  }
  ReplicatedEntityMirror& mirror = found->second;
  mirror.previous_position = mirror.position;
  mirror.previous_rotation = mirror.rotation;
  mirror.position = position;
  mirror.velocity = velocity;
  mirror.rotation = normalize_quat(rotation);
  mirror.animations.assign(animations.begin(), animations.end());
  snap_entity_render_transform(mirror);
}

void ClientEntityMirrorStore::apply_ride_started(
    const protocol::ClientboundRideStarted& event) {
  apply_ride_started_to_existing(entities_, event);
  if (ride_started_ready(entities_, event)) {
    std::erase_if(
        pending_ride_starts_,
        [&](const protocol::ClientboundRideStarted& existing) {
          return existing.passenger_id == event.passenger_id;
        });
  } else {
    upsert_pending_ride_start(pending_ride_starts_, event);
  }
}

void ClientEntityMirrorStore::apply_ride_ended(
    const protocol::ClientboundRideEnded& event) {
  std::erase_if(
      pending_ride_starts_,
      [&](const protocol::ClientboundRideStarted& existing) {
        return existing.passenger_id == event.passenger_id;
      });
  for (protocol::ClientboundEntityPassengersSet& pending :
       pending_passenger_sets_) {
    std::erase_if(
        pending.passengers,
        [&](const protocol::VehiclePassengerEntry& passenger) {
          return passenger.passenger_id == event.passenger_id;
        });
  }
  auto passenger = entities_.find(event.passenger_id);
  if (passenger != entities_.end()) {
    passenger->second.parent_rideable.reset();
    passenger->second.parent_seat_id.clear();
    passenger->second.controls_parent_vehicle = false;
    passenger->second.position = event.position;
    passenger->second.velocity = {};
    snap_entity_render_transform(passenger->second);
  }
  auto rideable = entities_.find(event.rideable_id);
  if (rideable == entities_.end()) {
    return;
  }
  auto& passengers = rideable->second.passengers;
  passengers.erase(
      std::remove_if(
          passengers.begin(),
          passengers.end(),
          [&](const ReplicatedPassengerMirror& entry) {
            return entry.passenger_id == event.passenger_id;
          }),
      passengers.end());
}

void ClientEntityMirrorStore::apply_passengers_set(
    const protocol::ClientboundEntityPassengersSet& event) {
  apply_passengers_set_to_existing(entities_, event);
  if (passengers_set_ready(entities_, event)) {
    std::erase_if(
        pending_passenger_sets_,
        [&](const protocol::ClientboundEntityPassengersSet& existing) {
          return existing.rideable_id == event.rideable_id;
        });
  } else {
    upsert_pending_passengers_set(pending_passenger_sets_, event);
  }
}

void ClientEntityMirrorStore::advance_interpolation(Seconds delta_seconds) noexcept {
  if (delta_seconds <= 0.0F) {
    return;
  }
  for (auto& [entity_id, mirror] : entities_) {
    (void)entity_id;
    if (mirror.interpolation_duration_seconds <= 0.0F) {
      mirror.render_position = mirror.position;
      mirror.render_rotation = mirror.rotation;
      continue;
    }
    mirror.interpolation_elapsed_seconds =
        std::min(
            mirror.interpolation_elapsed_seconds + delta_seconds,
            mirror.interpolation_duration_seconds);
    const f32 alpha = std::clamp(
        mirror.interpolation_elapsed_seconds /
            mirror.interpolation_duration_seconds,
        0.0F,
        1.0F);
    mirror.render_position = lerp(
        mirror.interpolation_start_position,
        mirror.position,
        alpha);
    mirror.render_rotation = lerp_quat_shortest(
        mirror.interpolation_start_rotation,
        mirror.rotation,
        alpha);
    if (alpha >= 1.0F) {
      mirror.interpolation_duration_seconds = 0.0F;
    }
  }
}

usize ClientEntityMirrorStore::count() const noexcept {
  return entities_.size();
}

const ReplicatedEntityMirror* ClientEntityMirrorStore::find(
    EntityNetID entity_id) const noexcept {
  const auto found = entities_.find(entity_id);
  return found == entities_.end() ? nullptr : &found->second;
}

std::vector<ReplicatedEntityMirror> ClientEntityMirrorStore::mirrors() const {
  std::vector<ReplicatedEntityMirror> mirrors;
  mirrors.reserve(entities_.size());
  for (const auto& [entity_id, mirror] : entities_) {
    (void)entity_id;
    mirrors.push_back(mirror);
  }
  return mirrors;
}

void ClientEntityMirrorStore::prune_except_world(WorldID world_id) {
  std::erase_if(
      entities_,
      [&](const auto& entry) {
        return entry.second.world_id != world_id;
      });
  pending_ride_starts_.clear();
  pending_passenger_sets_.clear();
}

void ClientEntityMirrorStore::clear() noexcept {
  entities_.clear();
  pending_ride_starts_.clear();
  pending_passenger_sets_.clear();
}

void ClientEntityMirrorStore::apply_pending_relationships() {
  bool applied = true;
  while (applied) {
    applied = false;
    for (auto it = pending_ride_starts_.begin();
         it != pending_ride_starts_.end();) {
      if (!ride_started_ready(entities_, *it)) {
        ++it;
        continue;
      }
      apply_ride_started_to_existing(entities_, *it);
      it = pending_ride_starts_.erase(it);
      applied = true;
    }
    for (auto it = pending_passenger_sets_.begin();
         it != pending_passenger_sets_.end();) {
      if (!has_entity(entities_, it->rideable_id)) {
        ++it;
        continue;
      }
      apply_passengers_set_to_existing(entities_, *it);
      if (!passengers_set_ready(entities_, *it)) {
        ++it;
        continue;
      }
      it = pending_passenger_sets_.erase(it);
      applied = true;
    }
  }
}

} // namespace arv::client
