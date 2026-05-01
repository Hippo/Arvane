#pragma once

#include "arv/player/player_constants.hpp"
#include "arv/ui/widgets/items.hpp"

#include <optional>
#include <span>
#include <string>

namespace arv::ui {

struct InventoryUiModel {
  bool visible{};
  std::string title{"Inventory"};
  u8 selected_hotbar_slot{};
  u8 hotbar_slot_count{player::DefaultHotbarSlotCount};
  Vec2f viewport{};
  std::span<const UiItemStackView> slots{};
  std::optional<UiItemStackView> cursor_stack{};
  Vec2f cursor_position{};
};

void build_inventory_ui(UiTree& tree, InventoryUiModel model);

} // namespace arv::ui
