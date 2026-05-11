#pragma once

#include "arv/player/player_constants.hpp"
#include "arv/ui/widgets/items.hpp"

#include <optional>
#include <span>
#include <string>

namespace arv::ui {

struct InventoryUiModel {
  bool visible{};
  std::string title{};
  u8 selected_hotbar_slot{};
  u8 hotbar_slot_count{player::DefaultHotbarSlotCount};
  Vec2f viewport{};
  std::span<const UiItemStackView> slots{};
  std::optional<UiItemStackView> cursor_stack{};
  Vec2f cursor_position{};
};

struct ItemTooltipUiModel {
  bool visible{true};
  std::string text{};
  Vec2f viewport{};
  Vec2f cursor_position{};
};

void build_inventory_ui(UiTree& tree, InventoryUiModel model);
void append_item_tooltip(UiTree& tree, ItemTooltipUiModel model);

} // namespace arv::ui
