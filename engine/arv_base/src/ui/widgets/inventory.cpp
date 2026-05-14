#include "arv/base/ui/widgets/inventory.hpp"

#include "item_slot.hpp"
#include "widget_common.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace arv::ui {
namespace widgets_detail {

constexpr f32 InventorySlotSize = 40.0F;
constexpr f32 InventorySlotSpacing = 4.0F;
constexpr u32 InventoryGridColumns = 9;
constexpr f32 InventoryGridWidth =
    static_cast<f32>(InventoryGridColumns) * InventorySlotSize +
    static_cast<f32>(InventoryGridColumns - 1U) * InventorySlotSpacing;

void append_inventory_slot_range(
    UiTree& tree,
    UiNodeID grid_id,
    const InventoryUiModel& model,
    usize first_slot,
    usize last_slot_exclusive) {
  for (usize slot_index = first_slot; slot_index < last_slot_exclusive; ++slot_index) {
    const UiItemStackView* stack_view =
        slot_index < model.slots.size() ? &model.slots[slot_index] : nullptr;
    const bool selected =
        slot_index < model.hotbar_slot_count && slot_index == model.selected_hotbar_slot;
    append_slot(
        tree,
        grid_id,
        "inventory.slot." + std::to_string(slot_index),
        static_cast<u8>(slot_index),
        stack_view,
        selected,
        false,
        InventorySlotSize);
  }
}

UiElement inventory_grid(std::string stable_id) {
  UiElement grid;
  grid.stable_id = std::move(stable_id);
  grid.type = UiElementType::Grid;
  grid.layout.grid_columns = InventoryGridColumns;
  grid.layout.spacing = InventorySlotSpacing;
  grid.hit_test = false;
  return grid;
}

void append_inventory_ui(UiTree& tree, InventoryUiModel model) {
  if (!model.visible) {
    return;
  }
  const UiTheme& theme = tree.theme();

  UiElement panel = fixed_panel(
      "inventory.panel",
      426.0F,
      268.0F,
      theme.colors.surface);
  panel.layout.anchor = UiAnchor::Center;
  panel.layout.padding = {
      theme.layout.spacing_lg,
      theme.layout.spacing_lg,
      theme.layout.spacing_md,
      theme.layout.spacing_md};
  style_surface(panel, theme, theme.widgets.panel.radius);
  panel.hit_test = true;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement stack;
  stack.stable_id = "inventory.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = theme.layout.spacing_md;
  stack.hit_test = false;
  const UiNodeID stack_id = tree.add_child(panel_id, std::move(stack));

  tree.add_child(
      stack_id,
      text_element("inventory.title", model.title, theme.colors.text, UiTextRole::Heading));

  const usize slot_total =
      std::max<usize>(model.slots.size(), player::DefaultPlayerInventorySlotCount);
  const usize hotbar_slot_count =
      std::min<usize>(model.hotbar_slot_count, slot_total);

  const UiNodeID grid_id =
      tree.add_child(stack_id, inventory_grid("inventory.grid"));
  append_inventory_slot_range(
      tree,
      grid_id,
      model,
      hotbar_slot_count,
      slot_total);

  UiElement hotbar_separator;
  hotbar_separator.stable_id = "inventory.hotbar_separator";
  hotbar_separator.type = UiElementType::Panel;
  hotbar_separator.layout.width_mode = UiSizeMode::Fixed;
  hotbar_separator.layout.height_mode = UiSizeMode::Fixed;
  hotbar_separator.layout.width = InventoryGridWidth;
  hotbar_separator.layout.height = 2.0F;
  hotbar_separator.style.background = theme.colors.border_focus;
  hotbar_separator.style.background.w = 0.58F;
  hotbar_separator.hit_test = false;
  tree.add_child(stack_id, std::move(hotbar_separator));

  if (hotbar_slot_count > 0U) {
    const UiNodeID hotbar_id =
        tree.add_child(stack_id, inventory_grid("inventory.hotbar"));
    append_inventory_slot_range(
        tree,
        hotbar_id,
        model,
        0U,
        hotbar_slot_count);
  }

  if (model.cursor_stack.has_value() && !model.cursor_stack->empty()) {
    UiElement cursor_slot;
    cursor_slot.stable_id = "inventory.cursor_stack";
    cursor_slot.type = UiElementType::Slot;
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

void append_item_tooltip(UiTree& tree, ItemTooltipUiModel model) {
  if (!model.visible || model.text.empty()) {
    return;
  }
  if (tree.find_by_stable_id("inventory.tooltip") != nullptr) {
    return;
  }

  const UiTheme& theme = tree.theme();
  constexpr f32 ApproxTooltipWidth = 240.0F;
  constexpr f32 ApproxTooltipHeight = 44.0F;
  const bool place_left = model.cursor_position.x + ApproxTooltipWidth >
                          model.viewport.x - theme.layout.spacing_sm;
  const bool place_above = model.cursor_position.y + ApproxTooltipHeight >
                           model.viewport.y - theme.layout.spacing_sm;

  UiElement tooltip;
  tooltip.stable_id = "inventory.tooltip";
  tooltip.type = UiElementType::Panel;
  tooltip.layout.width_mode = UiSizeMode::Content;
  tooltip.layout.height_mode = UiSizeMode::Content;
  tooltip.layout.padding = {
      theme.layout.spacing_md,
      theme.layout.spacing_md,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm};
  tooltip.layout.anchor = UiAnchor::TopLeft;
  tooltip.layout.offset = {
      model.cursor_position.x + (place_left ? -ApproxTooltipWidth : theme.layout.spacing_lg),
      model.cursor_position.y + (place_above ? -ApproxTooltipHeight : theme.layout.spacing_lg)};
  tooltip.style.background = theme.colors.surface_alt;
  tooltip.style.background.w = std::min(tooltip.style.background.w, 0.96F);
  tooltip.hit_test = false;
  widgets_detail::style_surface(tooltip, theme, theme.layout.radius_md);
  const UiNodeID tooltip_id = tree.add_child(tree.root_id(), std::move(tooltip));

  UiElement text = widgets_detail::text_element(
      "inventory.tooltip.text",
      std::move(model.text),
      theme.colors.text,
      UiTextRole::Body);
  text.layout.anchor = UiAnchor::Center;
  tree.add_child(tooltip_id, std::move(text));
}

} // namespace arv::ui
