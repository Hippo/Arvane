#include "server_runtime_internal.hpp"

#include <utility>

namespace arv::server::detail {

player::InventoryInsertResult ServerRuntimeImpl::insert_player_inventory_stack(
    PlayerID player_id,
    player::ItemStack stack,
    std::optional<usize> preferred_slot,
    bool replace_preferred_slot) {
  player::InventoryInsertResult result;
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || item_registry_ == nullptr || stack.count == 0U ||
      item_registry_->item(stack.item) == nullptr) {
    result.leftover = stack;
    return result;
  }

  if (preferred_slot.has_value()) {
    if (!player_state->inventory.is_valid_slot(preferred_slot.value())) {
      result.leftover = stack;
      return result;
    }

    if (replace_preferred_slot) {
      player_state->inventory.set_slot(preferred_slot.value(), stack);
      return result;
    }

    player::ItemStack remaining = stack;
    (void)player_state->inventory.merge_stack_into_slot(
        preferred_slot.value(),
        remaining,
        *item_registry_);
    if (remaining.count == 0U) {
      return result;
    }
    stack = remaining;
  }

  return player_state->inventory.insert_stack(stack, *item_registry_);
}

player::InventoryOperationResult ServerRuntimeImpl::apply_player_inventory_click(
    PlayerID player_id,
    usize slot_index,
    player::InventoryClickButton button) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || item_registry_ == nullptr) {
    return {player::InventoryOperationResultKind::RejectedInvalidSlot};
  }

  return player::apply_inventory_slot_click(
      player_state->inventory,
      slot_index,
      button,
      player_state->inventory_cursor_stack,
      *item_registry_);
}

std::optional<player::ItemStack> ServerRuntimeImpl::drop_player_inventory_cursor(PlayerID player_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || !player_state->inventory_cursor_stack.has_value()) {
    return std::nullopt;
  }

  std::optional<player::ItemStack> dropped = player_state->inventory_cursor_stack;
  spawn_dropped_item_stack(
      player_state->world_id,
      player_state->position,
      dropped.value());
  player_state->inventory_cursor_stack.reset();
  return dropped;
}

std::optional<player::ItemStack> ServerRuntimeImpl::drop_one_from_player_inventory_cursor(
    PlayerID player_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || !player_state->inventory_cursor_stack.has_value() ||
      player_state->inventory_cursor_stack->count == 0U) {
    return std::nullopt;
  }

  player::ItemStack dropped{
      player_state->inventory_cursor_stack->item,
      1U};
  spawn_dropped_item_stack(
      player_state->world_id,
      player_state->position,
      dropped);
  player_state->inventory_cursor_stack->count =
      static_cast<u16>(player_state->inventory_cursor_stack->count - 1U);
  if (player_state->inventory_cursor_stack->count == 0U) {
    player_state->inventory_cursor_stack.reset();
  }
  return dropped;
}

player::InventoryOperationResult ServerRuntimeImpl::close_player_inventory_menu(PlayerID player_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || item_registry_ == nullptr) {
    return {player::InventoryOperationResultKind::RejectedInvalidSlot};
  }
  if (!player_state->inventory_cursor_stack.has_value()) {
    return {player::InventoryOperationResultKind::NoOp};
  }

  player::ItemStack carried = player_state->inventory_cursor_stack.value();
  const player::InventoryInsertResult insert_result =
      player_state->inventory.insert_stack(carried, *item_registry_);
  if (insert_result.complete()) {
    player_state->inventory_cursor_stack.reset();
    return {player::InventoryOperationResultKind::Success, carried.count, carried};
  }

  player_state->inventory_cursor_stack = insert_result.leftover;
  return {
      insert_result.leftover->count == carried.count
          ? player::InventoryOperationResultKind::RejectedFullInventory
          : player::InventoryOperationResultKind::PartialSuccess,
      static_cast<u16>(carried.count - insert_result.leftover->count),
      insert_result.leftover};
}

protocol::InventoryUpdate ServerRuntimeImpl::make_inventory_update(
    const player::PlayerState& player_state,
    u32 revision) const {
  protocol::InventoryUpdate update;
  update.inventory_id = protocol::PlayerInventoryID;
  update.revision = revision;
  update.selected_hotbar_slot = player_state.selected_hotbar_slot;
  update.slots = player_state.inventory.slots();
  update.carried_stack = player_state.inventory_cursor_stack;
  return update;
}

void ServerRuntimeImpl::send_inventory_updates() {
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr) {
      continue;
    }
    const player::PlayerState* player_state = default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    InventoryReplicationState& state = inventory_replication_[session->player_id];
    const bool changed =
        !state.initialized ||
        state.slots != player_state->inventory.slots() ||
        state.carried_stack != player_state->inventory_cursor_stack ||
        state.selected_hotbar_slot != player_state->selected_hotbar_slot;
    if (!changed) {
      continue;
    }

    ++state.revision;
    state.initialized = true;
    state.slots = player_state->inventory.slots();
    state.carried_stack = player_state->inventory_cursor_stack;
    state.selected_hotbar_slot = player_state->selected_hotbar_slot;

    const protocol::InventoryUpdate update = make_inventory_update(*player_state, state.revision);
    (void)send_protocol_message(
        *transport_,
        session->connection_id,
        protocol::MessageType::InventoryUpdate,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_inventory_update(update));
  }
}

} // namespace arv::server::detail
