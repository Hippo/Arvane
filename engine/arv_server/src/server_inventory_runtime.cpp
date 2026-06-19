#include "server_runtime_internal.hpp"

#include <algorithm>
#include <utility>

namespace arv::server::detail {
namespace {

[[nodiscard]] player::InventoryOperationResult shift_transfer_stack_to_range(
    player::Inventory& inventory,
    usize source_slot,
    usize destination_first,
    usize destination_last_exclusive,
    const player::ItemRegistry& item_registry) {
  if (!inventory.is_valid_slot(source_slot) ||
      destination_first >= destination_last_exclusive ||
      destination_first >= inventory.slot_count()) {
    return {player::InventoryOperationResultKind::RejectedInvalidSlot};
  }

  destination_last_exclusive =
      std::min(destination_last_exclusive, inventory.slot_count());
  if (source_slot >= destination_first && source_slot < destination_last_exclusive) {
    return {player::InventoryOperationResultKind::NoOp};
  }

  const std::optional<player::ItemStack> source_stack = inventory.slot(source_slot);
  if (!source_stack.has_value() || source_stack->count == 0U) {
    return {player::InventoryOperationResultKind::NoOp};
  }

  player::ItemStack remaining = source_stack.value();
  (void)inventory.clear_slot(source_slot);

  for (usize slot = destination_first;
       slot < destination_last_exclusive && remaining.count > 0U;
       ++slot) {
    const std::optional<player::ItemStack>& target = inventory.slot(slot);
    if (!target.has_value() ||
        !player::item_stacks_compatible(target.value(), remaining)) {
      continue;
    }
    (void)inventory.merge_stack_into_slot(slot, remaining, item_registry);
  }

  for (usize slot = destination_first;
       slot < destination_last_exclusive && remaining.count > 0U;
       ++slot) {
    if (inventory.slot(slot).has_value()) {
      continue;
    }
    (void)inventory.merge_stack_into_slot(slot, remaining, item_registry);
  }

  if (remaining.count > 0U) {
    (void)inventory.set_slot(source_slot, remaining);
  }

  const u16 moved = static_cast<u16>(source_stack->count - remaining.count);
  if (moved == 0U) {
    return {player::InventoryOperationResultKind::RejectedFullInventory, 0U, source_stack};
  }
  return {
      remaining.count == 0U
          ? player::InventoryOperationResultKind::Success
          : player::InventoryOperationResultKind::PartialSuccess,
      moved,
      source_stack};
}

} // namespace

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
  if (!spawn_player_thrown_item_stack(*player_state, dropped.value())) {
    return std::nullopt;
  }
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

  player::ItemStack dropped =
      player::item_stack_with_count(player_state->inventory_cursor_stack.value(), 1U);
  if (!spawn_player_thrown_item_stack(*player_state, dropped)) {
    return std::nullopt;
  }
  player_state->inventory_cursor_stack->count =
      static_cast<u16>(player_state->inventory_cursor_stack->count - 1U);
  if (player_state->inventory_cursor_stack->count == 0U) {
    player_state->inventory_cursor_stack.reset();
  }
  return dropped;
}

std::optional<player::ItemStack> ServerRuntimeImpl::drop_one_from_player_inventory_slot(
    PlayerID player_id,
    usize slot_index) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr ||
      slot_index >= player::DefaultHotbarSlotCount ||
      !player_state->inventory.is_valid_slot(slot_index)) {
    return std::nullopt;
  }

  const std::optional<player::ItemStack>& slot_stack =
      player_state->inventory.slot(slot_index);
  if (!slot_stack.has_value() || slot_stack->count == 0U) {
    return std::nullopt;
  }

  player::ItemStack dropped = player::item_stack_with_count(slot_stack.value(), 1U);
  if (!spawn_player_thrown_item_stack(*player_state, dropped)) {
    return std::nullopt;
  }
  return player_state->inventory.remove_amount(slot_index, 1U);
}

player::InventoryOperationResult ServerRuntimeImpl::shift_transfer_player_inventory_slot(
    PlayerID player_id,
    usize slot_index) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || item_registry_ == nullptr ||
      !player_state->inventory.is_valid_slot(slot_index)) {
    return {player::InventoryOperationResultKind::RejectedInvalidSlot};
  }

  const usize hotbar_count = std::min<usize>(
      player::DefaultHotbarSlotCount,
      player_state->inventory.slot_count());
  if (slot_index < hotbar_count) {
    return shift_transfer_stack_to_range(
        player_state->inventory,
        slot_index,
        hotbar_count,
        player_state->inventory.slot_count(),
        *item_registry_);
  }

  return shift_transfer_stack_to_range(
      player_state->inventory,
      slot_index,
      0U,
      hotbar_count,
      *item_registry_);
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

protocol::ClientboundInventoryUpdate ServerRuntimeImpl::make_inventory_update(
    const player::PlayerState& player_state,
    u32 revision) const {
  protocol::ClientboundInventoryUpdate update;
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

    const protocol::ClientboundInventoryUpdate update = make_inventory_update(*player_state, state.revision);
    (void)send_protocol_message(
        *transport_,
        session->connection_id,
        protocol::MessageType::ClientboundInventoryUpdate,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_clientbound_inventory_update(update));
  }
}

} // namespace arv::server::detail
