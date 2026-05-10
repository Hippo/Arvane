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

[[nodiscard]] f32 distance_squared(Vec3f from, Vec3f to) noexcept {
  const Vec3f delta = to - from;
  return dot(delta, delta);
}

[[nodiscard]] Vec3f lerp(Vec3f from, Vec3f to, f32 alpha) noexcept {
  return {
      from.x + (to.x - from.x) * alpha,
      from.y + (to.y - from.y) * alpha,
      from.z + (to.z - from.z) * alpha};
}

[[nodiscard]] f32 quat_dot(Quatf lhs, Quatf rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

[[nodiscard]] Quatf lerp_quat_shortest(Quatf from, Quatf to, f32 alpha) noexcept {
  if (quat_dot(from, to) < 0.0F) {
    to = {-to.x, -to.y, -to.z, -to.w};
  }
  return normalize_quat({
      from.x + (to.x - from.x) * alpha,
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

} // namespace

void ClientEntityMirrorStore::apply_spawn(
    protocol::ClientboundEntitySpawn spawn,
    WorldID current_world) {
  if (spawn.world_id != current_world) {
    return;
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
}

void ClientEntityMirrorStore::apply_despawn(
    const protocol::ClientboundEntityDespawn& despawn,
    WorldID current_world) {
  if (despawn.world_id != current_world) {
    return;
  }
  entities_.erase(despawn.entity_id);
}

void ClientEntityMirrorStore::apply_snapshot(
    const protocol::ClientboundEntitySnapshot& snapshot,
    WorldID current_world,
    TickRate server_tick_rate) {
  if (snapshot.world_id != current_world) {
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
    mirror.animation_name = entry.animation_name;
    mirror.animation_time_seconds = entry.animation_time_seconds;
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

void ClientEntityMirrorStore::clear() noexcept {
  entities_.clear();
}

} // namespace arv::client
