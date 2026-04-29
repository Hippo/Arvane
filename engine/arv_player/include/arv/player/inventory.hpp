#pragma once

#include "arv/core/types.hpp"
#include "arv/player/item.hpp"

#include <optional>
#include <vector>

namespace arv::player {

enum class InventoryOperationResultKind : u8 {
  Success,
  PartialSuccess,
  NoOp,
  RejectedInvalidSlot,
  RejectedIncompatibleItem,
  RejectedFullInventory,
  RejectedInsufficientQuantity,
};

struct InventoryOperationResult {
  InventoryOperationResultKind kind{InventoryOperationResultKind::NoOp};
  u16 moved_count{};
  std::optional<ItemStack> stack{};

  [[nodiscard]] bool accepted() const noexcept {
    return kind == InventoryOperationResultKind::Success ||
           kind == InventoryOperationResultKind::PartialSuccess ||
           kind == InventoryOperationResultKind::NoOp;
  }
};

struct InventoryInsertResult {
  std::optional<ItemStack> leftover{};

  [[nodiscard]] bool complete() const noexcept {
    return !leftover.has_value();
  }
};

class Inventory {
public:
  explicit Inventory(usize slot_count = 0);

  [[nodiscard]] usize slot_count() const noexcept {
    return slots_.size();
  }

  [[nodiscard]] bool is_valid_slot(usize slot_index) const noexcept {
    return slot_index < slots_.size();
  }

  [[nodiscard]] const std::optional<ItemStack>& slot(usize slot_index) const;

  [[nodiscard]] const std::vector<std::optional<ItemStack>>& slots() const noexcept {
    return slots_;
  }

  bool set_slot(usize slot_index, std::optional<ItemStack> stack);
  bool clear_slot(usize slot_index);

  [[nodiscard]] InventoryInsertResult insert_stack(ItemStack stack, const ItemRegistry& registry);
  [[nodiscard]] InventoryOperationResult merge_stack_into_slot(
      usize slot_index,
      ItemStack& stack,
      const ItemRegistry& registry);
  [[nodiscard]] std::optional<ItemStack> remove_amount(usize slot_index, u16 amount);
  [[nodiscard]] std::optional<ItemStack> split_stack(usize slot_index, u16 amount);
  [[nodiscard]] bool swap_slots(usize first_slot, usize second_slot);
  [[nodiscard]] std::optional<usize> find_first_compatible_stack(
      const ItemStack& stack,
      const ItemRegistry& registry) const;
  [[nodiscard]] std::optional<usize> find_first_empty_slot() const noexcept;
  [[nodiscard]] bool consume_one(usize slot_index);

  [[nodiscard]] bool operator==(const Inventory& other) const noexcept {
    return slots_ == other.slots_;
  }

private:
  std::vector<std::optional<ItemStack>> slots_{};
};

enum class InventoryClickButton : u8 {
  Primary,
  Secondary,
};

[[nodiscard]] bool item_stacks_compatible(const ItemStack& lhs, const ItemStack& rhs) noexcept;

[[nodiscard]] InventoryOperationResult apply_inventory_slot_click(
    Inventory& inventory,
    usize slot_index,
    InventoryClickButton button,
    std::optional<ItemStack>& carried_stack,
    const ItemRegistry& registry);

} // namespace arv::player
