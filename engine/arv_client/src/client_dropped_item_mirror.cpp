#include "client_dropped_item_mirror.hpp"

#include <algorithm>

namespace arv::client {
namespace {

inline constexpr Seconds MinDroppedItemSmoothingSeconds = 1.0F / 30.0F;
inline constexpr Seconds MaxDroppedItemSmoothingSeconds = 0.12F;
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

[[nodiscard]] Seconds smoothing_duration_seconds(
    u64 tick_delta,
    TickRate tick_rate) noexcept {
  const TickRate safe_tick_rate = tick_rate == 0U ? DefaultBaseTps : tick_rate;
  const Seconds from_ticks =
      static_cast<Seconds>(std::max<u64>(tick_delta, 1U)) *
      tick_duration_seconds(safe_tick_rate);
  return std::clamp(
      from_ticks,
      MinDroppedItemSmoothingSeconds,
      MaxDroppedItemSmoothingSeconds);
}

void begin_dropped_item_smoothing(
    DroppedItemMirror& mirror,
    u64 server_tick,
    TickRate server_tick_rate) noexcept {
  const u64 previous_change_tick = mirror.last_position_change_server_tick;
  const u64 tick_delta =
      previous_change_tick != 0U && server_tick > previous_change_tick
          ? server_tick - previous_change_tick
          : 1U;
  mirror.interpolation_start_position = mirror.render_position;
  mirror.interpolation_elapsed_seconds = 0.0F;
  mirror.interpolation_duration_seconds =
      smoothing_duration_seconds(tick_delta, server_tick_rate);
  mirror.last_position_change_server_tick = server_tick;
}

void snap_dropped_item_render_position(DroppedItemMirror& mirror) noexcept {
  mirror.render_position = mirror.position;
  mirror.interpolation_start_position = mirror.position;
  mirror.interpolation_elapsed_seconds = 0.0F;
  mirror.interpolation_duration_seconds = 0.0F;
}

} // namespace

void ClientDroppedItemMirrorStore::apply_snapshot(
    const protocol::ClientboundDroppedItemSnapshot& snapshot,
    WorldID current_world,
    TickRate server_tick_rate) {
  if (snapshot.world_id != current_world) {
    return;
  }

  HashSet<EntityNetID> seen;
  seen.reserve(snapshot.drops.size());
  for (const protocol::DroppedItemSnapshotEntry& entry : snapshot.drops) {
    if (entry.stack.count == 0U) {
      continue;
    }
    seen.insert(entry.drop_id);
    DroppedItemMirror& mirror = dropped_items_[entry.drop_id];
    if (snapshot.server_tick < mirror.last_server_tick) {
      continue;
    }
    const bool first_update = mirror.drop_id.value == 0U;
    const bool moved =
        !first_update &&
        distance_squared(entry.position, mirror.position) >
            PositionChangeEpsilonSquared;
    const bool snap_position =
        moved &&
        distance_squared(entry.position, mirror.render_position) >
            TeleportSnapDistanceSquared;
    if (!first_update && moved && !snap_position) {
      begin_dropped_item_smoothing(mirror, snapshot.server_tick, server_tick_rate);
    } else if (!first_update && mirror.last_position_change_server_tick == 0U) {
      mirror.last_position_change_server_tick = snapshot.server_tick;
    }
    mirror.drop_id = entry.drop_id;
    mirror.world_id = snapshot.world_id;
    mirror.previous_position = first_update ? entry.position : mirror.position;
    mirror.position = entry.position;
    mirror.velocity = entry.velocity;
    mirror.stack = entry.stack;
    mirror.last_server_tick = snapshot.server_tick;
    if (first_update || snap_position) {
      mirror.last_position_change_server_tick = snapshot.server_tick;
      snap_dropped_item_render_position(mirror);
    }
  }

  for (auto iter = dropped_items_.begin(); iter != dropped_items_.end();) {
    if (iter->second.world_id == snapshot.world_id &&
        snapshot.server_tick >= iter->second.last_server_tick &&
        seen.find(iter->first) == seen.end()) {
      iter = dropped_items_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void ClientDroppedItemMirrorStore::advance_interpolation(Seconds delta_seconds) noexcept {
  if (delta_seconds <= 0.0F) {
    return;
  }
  for (auto& [drop_id, mirror] : dropped_items_) {
    (void)drop_id;
    if (mirror.interpolation_duration_seconds <= 0.0F) {
      mirror.render_position = mirror.position;
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
    if (alpha >= 1.0F) {
      mirror.interpolation_duration_seconds = 0.0F;
    }
  }
}

usize ClientDroppedItemMirrorStore::count() const noexcept {
  return dropped_items_.size();
}

const DroppedItemMirror* ClientDroppedItemMirrorStore::find(
    EntityNetID drop_id) const noexcept {
  const auto found = dropped_items_.find(drop_id);
  return found == dropped_items_.end() ? nullptr : &found->second;
}

std::vector<DroppedItemMirror> ClientDroppedItemMirrorStore::mirrors() const {
  std::vector<DroppedItemMirror> mirrors;
  mirrors.reserve(dropped_items_.size());
  for (const auto& [drop_id, mirror] : dropped_items_) {
    (void)drop_id;
    mirrors.push_back(mirror);
  }
  return mirrors;
}

void ClientDroppedItemMirrorStore::clear() noexcept {
  dropped_items_.clear();
}

} // namespace arv::client
