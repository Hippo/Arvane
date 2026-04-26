#include "arv/player/inventory.hpp"

#include <algorithm>

namespace arv::player {

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

bool Inventory::consume_one(usize slot_index) {
  const std::optional<ItemStack> removed = remove_amount(slot_index, 1U);
  return removed.has_value();
}

} // namespace arv::player
