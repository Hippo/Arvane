#include "arv/ui/widgets/inventory.hpp"

#include "item_slot.hpp"
#include "widget_common.hpp"

#include <algorithm>
#include <string>

namespace arv::ui {
namespace widgets_detail {

void append_inventory_ui(UiTree& tree, InventoryUiModel model) {
  if (!model.visible) {
    return;
  }

  UiElement panel = fixed_panel(
      "inventory.panel",
      426.0F,
      252.0F,
      {0.035F, 0.04F, 0.047F, 0.94F});
  panel.layout.anchor = UiAnchor::Center;
  panel.layout.padding = {14.0F, 14.0F, 12.0F, 12.0F};
  panel.hit_test = true;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement stack;
  stack.stable_id = "inventory.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = 10.0F;
  stack.hit_test = false;
  const UiNodeID stack_id = tree.add_child(panel_id, std::move(stack));

  tree.add_child(
      stack_id,
      text_element("inventory.title", model.title, {0.94F, 0.95F, 0.96F, 1.0F}));

  UiElement grid;
  grid.stable_id = "inventory.grid";
  grid.type = UiElementType::Grid;
  grid.layout.grid_columns = 9;
  grid.layout.spacing = 4.0F;
  grid.hit_test = false;
  const UiNodeID grid_id = tree.add_child(stack_id, std::move(grid));

  const usize slot_total =
      std::max<usize>(model.slots.size(), player::DefaultPlayerInventorySlotCount);
  for (usize slot_index = 0; slot_index < slot_total; ++slot_index) {
    const UiItemStackView* stack_view =
        slot_index < model.slots.size() ? &model.slots[slot_index] : nullptr;
    const bool in_hotbar = slot_index < model.hotbar_slot_count;
    append_slot(
        tree,
        grid_id,
        "inventory.slot." + std::to_string(slot_index),
        static_cast<u8>(slot_index),
        stack_view,
        in_hotbar && slot_index == model.selected_hotbar_slot,
        false,
        40.0F);
  }

  if (model.cursor_stack.has_value() && !model.cursor_stack->empty()) {
    UiElement cursor_slot;
    cursor_slot.stable_id = "inventory.cursor_stack";
    cursor_slot.type = UiElementType::ItemSlot;
    cursor_slot.layout.width_mode = UiSizeMode::Fixed;
    cursor_slot.layout.height_mode = UiSizeMode::Fixed;
    cursor_slot.layout.width = 40.0F;
    cursor_slot.layout.height = 40.0F;
    cursor_slot.layout.offset = {
        model.cursor_position.x - 20.0F,
        model.cursor_position.y - 20.0F};
    cursor_slot.style.background = {0.0F, 0.0F, 0.0F, 0.0F};
    cursor_slot.hit_test = false;
    const UiNodeID cursor_slot_id = tree.add_child(tree.root_id(), std::move(cursor_slot));
    add_item_slot_children(
        tree,
        cursor_slot_id,
        "inventory.cursor_stack",
        0,
        &model.cursor_stack.value(),
        false);
  }

  (void)model.viewport;
}

} // namespace widgets_detail

void build_inventory_ui(UiTree& tree, InventoryUiModel model) {
  tree.clear();
  widgets_detail::append_inventory_ui(tree, model);
}

} // namespace arv::ui
