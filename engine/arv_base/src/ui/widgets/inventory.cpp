#include "arv/base/ui/widgets/inventory.hpp"

#include "item_slot.hpp"
#include "widget_common.hpp"

#include <algorithm>
#include <optional>
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
    append_slot(
        tree,
        grid_id,
        "inventory.slot." + std::to_string(slot_index),
        static_cast<u8>(slot_index),
        stack_view,
        false,
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

[[nodiscard]] const UiItemStackView* container_stack_for_slot(
    const ContainerUiModel& model,
    player::ContainerSlotSource source,
    usize slot_index) noexcept {
  switch (source) {
    case player::ContainerSlotSource::OpenedContainer:
      return slot_index < model.container_slots.size()
                 ? &model.container_slots[slot_index]
                 : nullptr;
    case player::ContainerSlotSource::PlayerInventory:
      return slot_index < model.player_slots.size()
                 ? &model.player_slots[slot_index]
                 : nullptr;
  }
  return nullptr;
}

[[nodiscard]] std::string container_slot_stable_id(
    player::ContainerSlotSource source,
    usize slot_index) {
  switch (source) {
    case player::ContainerSlotSource::OpenedContainer:
      return "container.slot." + std::to_string(slot_index);
    case player::ContainerSlotSource::PlayerInventory:
      return "container.player_slot." + std::to_string(slot_index);
  }
  return "container.slot." + std::to_string(slot_index);
}

struct ContainerSlotGroupBounds {
  f32 left{};
  f32 top{};
  f32 right{};
  f32 bottom{};
};

[[nodiscard]] std::optional<ContainerSlotGroupBounds> container_slot_group_bounds(
    const player::ContainerSlotGroupDefinition& group) {
  if (group.slot_count == 0U) {
    return std::nullopt;
  }
  const u32 columns = std::max(1U, group.columns);
  const u32 rows = (group.slot_count + columns - 1U) / columns;
  const u32 last_row_count =
      group.slot_count - (rows - 1U) * columns;
  const u32 max_columns = rows == 1U ? last_row_count : columns;
  return ContainerSlotGroupBounds{
      group.position.x,
      group.position.y,
      group.position.x +
          static_cast<f32>(max_columns - 1U) * group.slot_spacing.x +
          InventorySlotSize,
      group.position.y +
          static_cast<f32>(rows - 1U) * group.slot_spacing.y +
          InventorySlotSize};
}

[[nodiscard]] bool should_separate_container_groups(
    const player::ContainerSlotGroupDefinition& previous,
    const player::ContainerSlotGroupDefinition& current) noexcept {
  if (previous.source != current.source) {
    return true;
  }
  if (previous.source != player::ContainerSlotSource::PlayerInventory) {
    return false;
  }

  const bool previous_is_hotbar =
      previous.start_slot < player::DefaultHotbarSlotCount;
  const bool current_is_hotbar =
      current.start_slot < player::DefaultHotbarSlotCount;
  return previous_is_hotbar != current_is_hotbar;
}

void append_container_separator(
    UiTree& tree,
    UiNodeID panel_id,
    const UiTheme& theme,
    std::string stable_id,
    f32 left,
    f32 right,
    f32 y) {
  UiElement separator;
  separator.stable_id = std::move(stable_id);
  separator.type = UiElementType::Panel;
  separator.layout.width_mode = UiSizeMode::Fixed;
  separator.layout.height_mode = UiSizeMode::Fixed;
  separator.layout.width = std::max(0.0F, right - left);
  separator.layout.height = 2.0F;
  separator.layout.offset = {left, y};
  separator.style.background = theme.colors.border_focus;
  separator.style.background.w = 0.58F;
  separator.hit_test = false;
  tree.add_child(panel_id, std::move(separator));
}

void append_container_group_separators(
    UiTree& tree,
    UiNodeID panel_id,
    const UiTheme& theme,
    const player::ContainerUiLayoutDefinition& layout) {
  for (usize index = 1U; index < layout.slot_groups.size(); ++index) {
    const player::ContainerSlotGroupDefinition& previous =
        layout.slot_groups[index - 1U];
    const player::ContainerSlotGroupDefinition& current =
        layout.slot_groups[index];
    if (!should_separate_container_groups(previous, current)) {
      continue;
    }

    const std::optional<ContainerSlotGroupBounds> previous_bounds =
        container_slot_group_bounds(previous);
    const std::optional<ContainerSlotGroupBounds> current_bounds =
        container_slot_group_bounds(current);
    if (!previous_bounds.has_value() || !current_bounds.has_value() ||
        current_bounds->top <= previous_bounds->bottom) {
      continue;
    }

    const f32 left = std::min(previous_bounds->left, current_bounds->left);
    const f32 right = std::max(previous_bounds->right, current_bounds->right);
    const f32 y =
        (previous_bounds->bottom + current_bounds->top) * 0.5F - 1.0F;
    append_container_separator(
        tree,
        panel_id,
        theme,
        "container.separator." + previous.id + "." + current.id,
        left,
        right,
        y);
  }
}

void append_container_slot_group(
    UiTree& tree,
    UiNodeID panel_id,
    const ContainerUiModel& model,
    const player::ContainerSlotGroupDefinition& group) {
  const u32 columns = std::max(1U, group.columns);
  for (u32 offset = 0U; offset < group.slot_count; ++offset) {
    const usize slot_index = static_cast<usize>(group.start_slot + offset);
    const u32 column = offset % columns;
    const u32 row = offset / columns;
    const Vec2f position{
        group.position.x + static_cast<f32>(column) * group.slot_spacing.x,
        group.position.y + static_cast<f32>(row) * group.slot_spacing.y};
    const UiItemStackView* stack =
        container_stack_for_slot(model, group.source, slot_index);
    append_slot(
        tree,
        panel_id,
        container_slot_stable_id(group.source, slot_index),
        static_cast<u8>(std::min<usize>(slot_index, 255U)),
        stack,
        false,
        false,
        InventorySlotSize,
        position);
  }
}

void append_container_ui(UiTree& tree, ContainerUiModel model) {
  if (!model.visible || model.layout == nullptr) {
    return;
  }

  const UiTheme& theme = tree.theme();
  UiElement panel = fixed_panel(
      "container.panel",
      model.layout->size.x,
      model.layout->size.y,
      theme.colors.surface);
  panel.layout.anchor = UiAnchor::Center;
  panel.style.background.w = std::min(panel.style.background.w, 0.96F);
  style_surface(panel, theme, theme.widgets.panel.radius);
  panel.hit_test = true;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement title =
      text_element("container.title", model.title, theme.colors.text, UiTextRole::Heading);
  title.layout.offset = {18.0F, 10.0F};
  tree.add_child(panel_id, std::move(title));

  for (const player::ContainerSlotGroupDefinition& group : model.layout->slot_groups) {
    append_container_slot_group(tree, panel_id, model, group);
  }
  append_container_group_separators(tree, panel_id, theme, *model.layout);

  if (model.cursor_stack.has_value() && !model.cursor_stack->empty()) {
    UiElement cursor_slot;
    cursor_slot.stable_id = "container.cursor_stack";
    cursor_slot.type = UiElementType::Slot;
    cursor_slot.layout.width_mode = UiSizeMode::Fixed;
    cursor_slot.layout.height_mode = UiSizeMode::Fixed;
    cursor_slot.layout.width = InventorySlotSize;
    cursor_slot.layout.height = InventorySlotSize;
    cursor_slot.layout.offset = {
        model.cursor_position.x - InventorySlotSize * 0.5F,
        model.cursor_position.y - InventorySlotSize * 0.5F};
    cursor_slot.style.background = {0.0F, 0.0F, 0.0F, 0.0F};
    cursor_slot.hit_test = false;
    const UiNodeID cursor_slot_id = tree.add_child(tree.root_id(), std::move(cursor_slot));
    add_item_slot_children(
        tree,
        cursor_slot_id,
        "container.cursor_stack",
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

void append_container_ui(UiTree& tree, ContainerUiModel model) {
  widgets_detail::append_container_ui(tree, std::move(model));
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
