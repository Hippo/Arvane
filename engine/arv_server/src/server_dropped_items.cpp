#include "server_runtime_internal.hpp"

#include "arv/physics/collision_resolution.hpp"
#include "arv/world/world_queries.hpp"

#include <array>
#include <utility>

namespace arv::server::detail {
namespace {

[[nodiscard]] Aabb3f dropped_item_bounds(Vec3f position) noexcept {
  constexpr f32 HalfWidth = 0.125F;
  constexpr f32 Height = 0.25F;
  return {
      {position.x - HalfWidth, position.y, position.z - HalfWidth},
      {position.x + HalfWidth, position.y + Height, position.z + HalfWidth}};
}

[[nodiscard]] bool dropped_item_spawn_space_clear(
    const physics::ICollisionQuery& query,
    Vec3f position) {
  return !physics::overlaps_solid_blocks(query, dropped_item_bounds(position));
}

[[nodiscard]] Vec3f thrown_item_view_direction(
    const player::PlayerState& player_state) noexcept {
  const Vec3f look = normalize_or_zero(player_look_direction(player_state));
  if (dot(look, look) > 0.0F) {
    return look;
  }
  return horizontal_forward(player_state.yaw_radians);
}

[[nodiscard]] Vec3f thrown_item_horizontal_direction(
    const player::PlayerState& player_state,
    Vec3f view_direction) noexcept {
  Vec3f horizontal{view_direction.x, 0.0F, view_direction.z};
  horizontal = normalize_or_zero(horizontal);
  if (dot(horizontal, horizontal) > 0.0F) {
    return horizontal;
  }
  return horizontal_forward(player_state.yaw_radians);
}

[[nodiscard]] std::optional<Vec3f> thrown_item_spawn_position(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const player::PlayerState& player_state,
    const player::PlayerTuning& tuning,
    Vec3f view_direction,
    Vec3f horizontal_direction) {
  const world::WorldCollisionQuery query(world, block_registry, true);
  const Vec3f eye = player_eye_position(player_state, tuning);
  const Vec3f lower_eye = eye + Vec3f{0.0F, -0.28F, 0.0F};
  const std::array<Vec3f, 8> candidates{{
      lower_eye + view_direction * 0.48F,
      lower_eye + horizontal_direction * 0.48F,
      lower_eye + view_direction * 0.28F,
      lower_eye + horizontal_direction * 0.28F,
      eye + view_direction * 0.36F,
      eye + horizontal_direction * 0.36F,
      player_state.position + Vec3f{0.0F, 1.0F, 0.0F} + horizontal_direction * 0.42F,
      eye,
  }};

  for (const Vec3f& candidate : candidates) {
    if (dropped_item_spawn_space_clear(query, candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

} // namespace

std::vector<DroppedItemStack> ServerRuntimeImpl::dropped_item_stacks() const {
  std::vector<DroppedItemStack> drops;
  const std::vector<entity::DroppedItemEntitySnapshot> snapshots =
      default_world_.collect_dropped_item_snapshots();
  drops.reserve(snapshots.size());
  for (const entity::DroppedItemEntitySnapshot& snapshot : snapshots) {
    drops.push_back({snapshot.net_id,
                     snapshot.world_id,
                     snapshot.position,
                     snapshot.velocity,
                     snapshot.stack});
  }
  return drops;
}

bool ServerRuntimeImpl::spawn_dropped_item_stack(
    WorldID world_id,
    Vec3f position,
    player::ItemStack stack) {
  return spawn_dropped_item_stack(world_id, position, {}, std::move(stack));
}

bool ServerRuntimeImpl::spawn_dropped_item_stack(
    WorldID world_id,
    Vec3f position,
    Vec3f velocity,
    player::ItemStack stack) {
  player::normalize_item_stack_data(stack);
  if (stack.count == 0U || item_registry_ == nullptr ||
      item_registry_->item(stack.item) == nullptr) {
    return false;
  }
  if (world_id != default_world_.id()) {
    return false;
  }
  const EntityID entity_id = default_world_.create_dropped_item(
      stack,
      position,
      velocity,
      dropped_items_.pickup_delay_seconds,
      true);
  return entity_id.value != 0U;
}

bool ServerRuntimeImpl::spawn_player_thrown_item_stack(
    const player::PlayerState& player_state,
    player::ItemStack stack) {
  const Vec3f view_direction = thrown_item_view_direction(player_state);
  const Vec3f horizontal_direction =
      thrown_item_horizontal_direction(player_state, view_direction);
  const Vec3f fallback_position =
      player_eye_position(player_state, player_tuning_) +
      horizontal_direction * 0.36F +
      Vec3f{0.0F, -0.28F, 0.0F};
  Vec3f position = fallback_position;
  if (block_registry_ != nullptr) {
    const std::optional<Vec3f> clear_position = thrown_item_spawn_position(
        default_world_,
        *block_registry_,
        player_state,
        player_tuning_,
        view_direction,
        horizontal_direction);
    if (!clear_position.has_value()) {
      return false;
    }
    position = clear_position.value();
  }

  constexpr f32 ThrowForwardSpeedBlocksPerSecond = 4.0F;
  constexpr f32 ThrowUpwardSpeedBlocksPerSecond = 1.25F;
  const Vec3f velocity =
      player_state.velocity +
      view_direction * ThrowForwardSpeedBlocksPerSecond +
      Vec3f{0.0F, ThrowUpwardSpeedBlocksPerSecond, 0.0F};
  return spawn_dropped_item_stack(
      player_state.world_id,
      position,
      velocity,
      std::move(stack));
}

void ServerRuntimeImpl::tick_dropped_item_pickups(Seconds tick_seconds) {
  (void)tick_seconds;
  if (item_registry_ == nullptr || dropped_items_.pickup_radius <= 0.0F) {
    return;
  }

  const f32 radius_sq = dropped_items_.pickup_radius * dropped_items_.pickup_radius;
  const std::vector<entity::DroppedItemEntitySnapshot> drops =
      default_world_.collect_dropped_item_snapshots();
  for (const entity::DroppedItemEntitySnapshot& drop : drops) {
    if (drop.pickup_delay_seconds > 0.0F || drop.stack.count == 0U) {
      continue;
    }

    entity::DroppedItem* live_drop = default_world_.find_dropped_item(drop.entity_id);
    if (live_drop == nullptr || live_drop->stack.count == 0U ||
        live_drop->pickup_delay_seconds > 0.0F) {
      continue;
    }

    for (const PlayerID player_id : default_world_.player_ids()) {
      player::PlayerState* player_state = default_world_.find_player(player_id);
      if (player_state == nullptr || player_state->world_id != drop.world_id) {
        continue;
      }

      const Vec3f delta = player_state->position - drop.position;
      if (dot(delta, delta) > radius_sq) {
        continue;
      }

      const player::InventoryInsertResult insert_result =
          player_state->inventory.insert_stack(live_drop->stack, *item_registry_);
      const u16 leftover_count =
          insert_result.leftover.has_value() ? insert_result.leftover->count : 0U;
      if (leftover_count == live_drop->stack.count) {
        continue;
      }

      if (insert_result.complete()) {
        (void)default_world_.destroy_entity(drop.entity_id, world::EntityDespawnReason::Removed);
      } else {
        live_drop->stack = insert_result.leftover.value();
      }
      break;
    }
  }
}

void ServerRuntimeImpl::send_dropped_item_replication() {
  const std::vector<entity::DroppedItemEntitySnapshot> drops =
      default_world_.collect_dropped_item_snapshots();
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || session->world_id != default_world_.id()) {
      continue;
    }

    EntityReplicationState& state = entity_replication_[session_id];
    HashSet<EntityNetID> relevant_now;
    protocol::ClientboundDroppedItemSnapshot snapshot;
    snapshot.world_id = session->world_id;
    snapshot.server_tick = tick_index_;

    for (const entity::DroppedItemEntitySnapshot& dropped : drops) {
      if (dropped.world_id != session->world_id || dropped.stack.count == 0U) {
        continue;
      }

      if (!chunk_streamer_.session_has_sent_chunk(session_id, session->world_id, dropped.chunk)) {
        continue;
      }

      relevant_now.insert(dropped.net_id);
      snapshot.drops.push_back({dropped.net_id,
                                dropped.stack,
                                dropped.position,
                                dropped.velocity});
    }

    if (snapshot.drops.empty() && state.known_dropped_items.empty()) {
      continue;
    }

    state.known_dropped_items = std::move(relevant_now);
    (void)send_protocol_message(
        *transport_,
        session->connection_id,
        protocol::MessageType::ClientboundDroppedItemSnapshot,
        protocol::ReliabilityClass::UnreliableLatestWins,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_clientbound_dropped_item_snapshot(snapshot));
  }
}

} // namespace arv::server::detail
