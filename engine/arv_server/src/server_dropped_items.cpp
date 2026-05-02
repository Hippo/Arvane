#include "server_runtime_internal.hpp"

#include <utility>

namespace arv::server::detail {

std::vector<DroppedItemStack> ServerRuntimeImpl::dropped_item_stacks() const {
  std::vector<DroppedItemStack> drops;
  const std::vector<entity::DroppedItemEntitySnapshot> snapshots =
      default_world_.collect_dropped_item_snapshots();
  drops.reserve(snapshots.size());
  for (const entity::DroppedItemEntitySnapshot& snapshot : snapshots) {
    drops.push_back({
        snapshot.net_id,
        snapshot.world_id,
        snapshot.position,
        snapshot.velocity,
        snapshot.stack});
  }
  return drops;
}

void ServerRuntimeImpl::spawn_dropped_item_stack(
    WorldID world_id,
    Vec3f position,
    player::ItemStack stack) {
  if (stack.count == 0U || item_registry_ == nullptr ||
      item_registry_->item(stack.item) == nullptr) {
    return;
  }
  if (world_id != default_world_.id()) {
    return;
  }
  (void)default_world_.create_dropped_item(
      stack,
      position,
      {},
      dropped_items_.pickup_delay_seconds,
      true);
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
    protocol::DroppedItemSnapshot snapshot;
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
      snapshot.drops.push_back({
          dropped.net_id,
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
        protocol::MessageType::DroppedItemSnapshot,
        protocol::ReliabilityClass::UnreliableLatestWins,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_dropped_item_snapshot(snapshot));
  }
}

} // namespace arv::server::detail
