#include "arv/player/inventory.hpp"

#include <algorithm>
#include <utility>

namespace arv::player {
namespace {

[[nodiscard]] u16 max_stack_size_for(const ItemRegistry& registry, ItemID item_id) noexcept {
  const ItemRecord* item = registry.item(item_id);
  return item == nullptr ? 0U : item->max_stack_size;
}

[[nodiscard]] bool can_merge_into(
    const std::optional<ItemStack>& target,
    const ItemStack& source,
    const ItemRegistry& registry) noexcept {
  if (!target.has_value() || !item_stacks_compatible(target.value(), source)) {
    return false;
  }
  return target->count < max_stack_size_for(registry, source.item);
}

} // namespace

Inventory::Inventory(usize slot_count) : slots_(slot_count) {
}

const std::optional<ItemStack>& Inventory::slot(usize slot_index) const {
  static const std::optional<ItemStack> EmptySlot;
  return is_valid_slot(slot_index) ? slots_[slot_index] : EmptySlot;
}

bool Inventory::set_slot(usize slot_index, std::optional<ItemStack> stack) {
  if (!is_valid_slot(slot_index)) {
    return false;
  }
  slots_[slot_index] = std::move(stack);
  return true;
}

bool Inventory::clear_slot(usize slot_index) {
  if (!is_valid_slot(slot_index)) {
    return false;
  }
  slots_[slot_index].reset();
  return true;
}

InventoryInsertResult Inventory::insert_stack(ItemStack stack, const ItemRegistry& registry) {
  InventoryInsertResult result;
  if (stack.count == 0U) {
    return result;
  }

  const ItemRecord* item = registry.item(stack.item);
  if (item == nullptr) {
    result.leftover = stack;
    return result;
  }

  for (std::optional<ItemStack>& slot_value : slots_) {
    if (!slot_value.has_value() || slot_value->item != stack.item || slot_value->count >= item->max_stack_size) {
      continue;
    }

    const u16 free_space = static_cast<u16>(item->max_stack_size - slot_value->count);
    const u16 moved = std::min(free_space, stack.count);
    slot_value->count = static_cast<u16>(slot_value->count + moved);
    stack.count = static_cast<u16>(stack.count - moved);
    if (stack.count == 0U) {
      return result;
    }
  }

  for (std::optional<ItemStack>& slot_value : slots_) {
    if (slot_value.has_value()) {
      continue;
    }

    const u16 moved = std::min(item->max_stack_size, stack.count);
    slot_value = ItemStack{stack.item, moved};
    stack.count = static_cast<u16>(stack.count - moved);
    if (stack.count == 0U) {
      return result;
    }
  }

  result.leftover = stack;
  return result;
}

InventoryOperationResult Inventory::merge_stack_into_slot(
    usize slot_index,
    ItemStack& stack,
    const ItemRegistry& registry) {
  if (!is_valid_slot(slot_index)) {
    return {InventoryOperationResultKind::RejectedInvalidSlot};
  }
  if (stack.count == 0U) {
    return {InventoryOperationResultKind::NoOp};
  }

  const u16 max_stack_size = max_stack_size_for(registry, stack.item);
  if (max_stack_size == 0U) {
    return {InventoryOperationResultKind::RejectedIncompatibleItem};
  }

  std::optional<ItemStack>& target = slots_[slot_index];
  if (!target.has_value()) {
    const u16 moved = std::min(max_stack_size, stack.count);
    target = ItemStack{stack.item, moved};
    stack.count = static_cast<u16>(stack.count - moved);
    return {
        stack.count == 0U ? InventoryOperationResultKind::Success
                          : InventoryOperationResultKind::PartialSuccess,
        moved,
        target};
  }

  if (!item_stacks_compatible(target.value(), stack)) {
    return {InventoryOperationResultKind::RejectedIncompatibleItem};
  }
  if (target->count >= max_stack_size) {
    return {InventoryOperationResultKind::RejectedFullInventory, 0U, target};
  }

  const u16 free_space = static_cast<u16>(max_stack_size - target->count);
  const u16 moved = std::min(free_space, stack.count);
  target->count = static_cast<u16>(target->count + moved);
  stack.count = static_cast<u16>(stack.count - moved);
  return {
      stack.count == 0U ? InventoryOperationResultKind::Success
                        : InventoryOperationResultKind::PartialSuccess,
      moved,
      target};
}

std::optional<ItemStack> Inventory::remove_amount(usize slot_index, u16 amount) {
  if (!is_valid_slot(slot_index) || !slots_[slot_index].has_value() || amount == 0U) {
    return std::nullopt;
  }

  ItemStack& stack = slots_[slot_index].value();
  const u16 removed = std::min(amount, stack.count);
  ItemStack taken{stack.item, removed};
  stack.count = static_cast<u16>(stack.count - removed);
  if (stack.count == 0U) {
    slots_[slot_index].reset();
  }
  return taken;
}

std::optional<ItemStack> Inventory::split_stack(usize slot_index, u16 amount) {
  if (!is_valid_slot(slot_index) || !slots_[slot_index].has_value()) {
    return std::nullopt;
  }

  ItemStack& stack = slots_[slot_index].value();
  if (amount == 0U || amount >= stack.count) {
    return std::nullopt;
  }

  stack.count = static_cast<u16>(stack.count - amount);
  return ItemStack{stack.item, amount};
}

bool Inventory::swap_slots(usize first_slot, usize second_slot) {
  if (!is_valid_slot(first_slot) || !is_valid_slot(second_slot)) {
    return false;
  }
  if (first_slot == second_slot) {
    return true;
  }
  std::swap(slots_[first_slot], slots_[second_slot]);
  return true;
}

std::optional<usize> Inventory::find_first_compatible_stack(
    const ItemStack& stack,
    const ItemRegistry& registry) const {
  for (usize slot_index = 0; slot_index < slots_.size(); ++slot_index) {
    if (can_merge_into(slots_[slot_index], stack, registry)) {
      return slot_index;
    }
  }
  return std::nullopt;
}

std::optional<usize> Inventory::find_first_empty_slot() const noexcept {
  for (usize slot_index = 0; slot_index < slots_.size(); ++slot_index) {
    if (!slots_[slot_index].has_value()) {
      return slot_index;
    }
  }
  return std::nullopt;
}

bool Inventory::consume_one(usize slot_index) {
  const std::optional<ItemStack> removed = remove_amount(slot_index, 1U);
  return removed.has_value();
}

bool item_stacks_compatible(const ItemStack& lhs, const ItemStack& rhs) noexcept {
  return lhs.item == rhs.item;
}

InventoryOperationResult apply_inventory_slot_click(
    Inventory& inventory,
    usize slot_index,
    InventoryClickButton button,
    std::optional<ItemStack>& carried_stack,
    const ItemRegistry& registry) {
  if (!inventory.is_valid_slot(slot_index)) {
    return {InventoryOperationResultKind::RejectedInvalidSlot};
  }

  std::optional<ItemStack> slot_stack = inventory.slot(slot_index);
  if (button == InventoryClickButton::Primary) {
    if (!carried_stack.has_value()) {
      if (!slot_stack.has_value()) {
        return {InventoryOperationResultKind::NoOp};
      }
      carried_stack = slot_stack;
      inventory.clear_slot(slot_index);
      return {InventoryOperationResultKind::Success, slot_stack->count, carried_stack};
    }

    if (!slot_stack.has_value() || item_stacks_compatible(slot_stack.value(), carried_stack.value())) {
      InventoryOperationResult result =
          inventory.merge_stack_into_slot(slot_index, carried_stack.value(), registry);
      if (carried_stack->count == 0U) {
        carried_stack.reset();
      }
      return result;
    }

    inventory.set_slot(slot_index, carried_stack);
    carried_stack = slot_stack;
    return {InventoryOperationResultKind::Success, carried_stack->count, carried_stack};
  }

  if (!carried_stack.has_value()) {
    if (!slot_stack.has_value()) {
      return {InventoryOperationResultKind::NoOp};
    }

    const u16 taken = static_cast<u16>((slot_stack->count + 1U) / 2U);
    carried_stack = inventory.remove_amount(slot_index, taken);
    return {InventoryOperationResultKind::Success, taken, carried_stack};
  }

  if (!slot_stack.has_value()) {
    ItemStack one{carried_stack->item, 1U};
    if (!inventory.set_slot(slot_index, one)) {
      return {InventoryOperationResultKind::RejectedInvalidSlot};
    }
    carried_stack->count = static_cast<u16>(carried_stack->count - 1U);
    if (carried_stack->count == 0U) {
      carried_stack.reset();
    }
    return {InventoryOperationResultKind::Success, 1U, one};
  }

  if (!item_stacks_compatible(slot_stack.value(), carried_stack.value())) {
    return {InventoryOperationResultKind::RejectedIncompatibleItem, 0U, slot_stack};
  }

  const u16 max_stack_size = max_stack_size_for(registry, carried_stack->item);
  if (max_stack_size == 0U) {
    return {InventoryOperationResultKind::RejectedIncompatibleItem};
  }
  if (slot_stack->count >= max_stack_size) {
    return {InventoryOperationResultKind::RejectedFullInventory, 0U, slot_stack};
  }

  slot_stack->count = static_cast<u16>(slot_stack->count + 1U);
  inventory.set_slot(slot_index, slot_stack);
  carried_stack->count = static_cast<u16>(carried_stack->count - 1U);
  if (carried_stack->count == 0U) {
    carried_stack.reset();
  }
  return {InventoryOperationResultKind::Success, 1U, slot_stack};
}

} // namespace arv::player
