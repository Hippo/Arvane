#pragma once

#include "arv/core/types.hpp"
#include "arv/player/item.hpp"

#include <optional>
#include <vector>

namespace arv::player {

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
  [[nodiscard]] std::optional<ItemStack> remove_amount(usize slot_index, u16 amount);
  [[nodiscard]] std::optional<ItemStack> split_stack(usize slot_index, u16 amount);
  [[nodiscard]] bool consume_one(usize slot_index);

  [[nodiscard]] bool operator==(const Inventory& other) const noexcept {
    return slots_ == other.slots_;
  }

private:
  std::vector<std::optional<ItemStack>> slots_{};
};

} // namespace arv::player
