#include "arv/base/ui/widgets/inventory.hpp"

#include "arv/base/ui/widgets/text_input.hpp"
#include "item_slot.hpp"
#include "widget_common.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arv::ui {
namespace widgets_detail {

constexpr f32 InventorySlotSize = 40.0F;
constexpr f32 InventorySlotSpacing = 4.0F;
constexpr f32 PlayerMenuInventorySlotSize = 48.0F;
constexpr f32 PlayerMenuInventorySlotSpacing = 6.0F;
constexpr u32 InventoryGridColumns = 9;
constexpr f32 PlayerMenuWidth = 720.0F;
constexpr f32 PlayerMenuInventoryHeight = 392.0F;
constexpr f32 PlayerMenuCraftingHeight = 520.0F;
constexpr f32 PlayerMenuPlaceholderHeight = 320.0F;
constexpr f32 PlayerMenuMinimumTopOffset = 16.0F;
constexpr f32 PlayerMenuContentTop = 66.0F;
constexpr f32 RecipeSlotSize = 44.0F;
constexpr f32 RecipeSlotSpacing = 6.0F;

[[nodiscard]] constexpr f32 inventory_grid_width(f32 slot_size, f32 slot_spacing) noexcept {
  return static_cast<f32>(InventoryGridColumns) * slot_size +
         static_cast<f32>(InventoryGridColumns - 1U) * slot_spacing;
}

void append_inventory_slot_range(
    UiTree& tree,
    UiNodeID grid_id,
    const InventoryUiModel& model,
    usize first_slot,
    usize last_slot_exclusive,
    f32 slot_size) {
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
        slot_size);
  }
}

UiElement inventory_grid(std::string stable_id, f32 slot_spacing) {
  UiElement grid;
  grid.stable_id = std::move(stable_id);
  grid.type = UiElementType::Grid;
  grid.layout.grid_columns = InventoryGridColumns;
  grid.layout.spacing = slot_spacing;
  grid.hit_test = false;
  return grid;
}

UiElement menu_button(
    std::string stable_id,
    std::string action,
    std::string label,
    bool selected,
    f32 width,
    f32 height,
    const UiTheme& theme) {
  UiElement button;
  button.stable_id = std::move(stable_id);
  button.type = UiElementType::Button;
  button.layout.width_mode = UiSizeMode::Fixed;
  button.layout.height_mode = UiSizeMode::Fixed;
  button.layout.width = width;
  button.layout.height = height;
  button.style.background =
      selected ? theme.colors.surface_selected : theme.colors.surface_alt;
  button.style.background_hover = theme.colors.surface_hover;
  button.style.background_pressed = theme.colors.surface_pressed;
  button.style.background_selected = theme.colors.surface_selected;
  button.style.foreground = theme.colors.text;
  button.style.border = selected ? theme.colors.border_focus : theme.colors.border;
  button.style.border_width = theme.layout.border_width;
  button.style.radius = theme.layout.radius_sm;
  button.focusable = true;
  button.selected = selected;
  button.action = std::move(action);
  button.text = std::move(label);
  return button;
}

UiNodeID add_menu_button(
    UiTree& tree,
    UiNodeID parent,
    UiElement button,
    const UiTheme& theme) {
  const std::string stable_id = button.stable_id;
  std::string label = std::move(button.text);
  button.text.clear();
  const bool enabled = button.enabled;
  const UiNodeID button_id = tree.add_child(parent, std::move(button));
  if (!label.empty()) {
    UiElement label_element = text_element(
        stable_id + ".label",
        std::move(label),
        enabled ? theme.colors.text : theme.colors.text_disabled,
        UiTextRole::Body);
    label_element.layout.anchor = UiAnchor::Center;
    tree.add_child(button_id, std::move(label_element));
  }
  return button_id;
}

void append_inventory_section(
    UiTree& tree,
    UiNodeID parent,
    InventoryUiModel model,
    Vec2f offset,
    bool show_title,
    f32 slot_size,
    f32 slot_spacing,
    f32 stack_spacing) {
  const UiTheme& theme = tree.theme();

  UiElement stack;
  stack.stable_id = show_title ? "player_menu.inventory.stack" : "player_menu.inventory.compact_stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = stack_spacing;
  stack.layout.offset = offset;
  stack.hit_test = false;
  const UiNodeID stack_id = tree.add_child(parent, std::move(stack));

  if (show_title) {
    tree.add_child(
        stack_id,
        text_element("player_menu.inventory.title", model.title, theme.colors.text, UiTextRole::Heading));
  }

  const usize slot_total =
      std::max<usize>(model.slots.size(), player::DefaultPlayerInventorySlotCount);
  const usize hotbar_slot_count =
      std::min<usize>(model.hotbar_slot_count, slot_total);

  const UiNodeID grid_id =
      tree.add_child(stack_id, inventory_grid("player_menu.inventory.grid", slot_spacing));
  append_inventory_slot_range(
      tree,
      grid_id,
      model,
      hotbar_slot_count,
      slot_total,
      slot_size);

  UiElement hotbar_separator;
  hotbar_separator.stable_id = "player_menu.inventory.hotbar_separator";
  hotbar_separator.type = UiElementType::Panel;
  hotbar_separator.layout.width_mode = UiSizeMode::Fixed;
  hotbar_separator.layout.height_mode = UiSizeMode::Fixed;
  hotbar_separator.layout.width = inventory_grid_width(slot_size, slot_spacing);
  hotbar_separator.layout.height = 2.0F;
  hotbar_separator.style.background = theme.colors.border_focus;
  hotbar_separator.style.background.w = 0.58F;
  hotbar_separator.hit_test = false;
  tree.add_child(stack_id, std::move(hotbar_separator));

  if (hotbar_slot_count > 0U) {
    const UiNodeID hotbar_id =
        tree.add_child(stack_id, inventory_grid("player_menu.inventory.hotbar", slot_spacing));
    append_inventory_slot_range(
        tree,
        hotbar_id,
        model,
        0U,
        hotbar_slot_count,
        slot_size);
  }
}

[[nodiscard]] f32 player_menu_height(std::string_view active_tab_id) noexcept {
  if (active_tab_id == "crafting") {
    return PlayerMenuCraftingHeight;
  }
  if (active_tab_id == "inventory") {
    return PlayerMenuInventoryHeight;
  }
  return PlayerMenuPlaceholderHeight;
}

[[nodiscard]] f32 player_menu_top_offset(Vec2f viewport) noexcept {
  constexpr f32 StableVisualHeight =
      (PlayerMenuInventoryHeight + PlayerMenuCraftingHeight) * 0.5F;
  const f32 centered_top = (viewport.y - StableVisualHeight) * 0.5F;
  return std::max(PlayerMenuMinimumTopOffset, centered_top);
}

void append_recipe_slot(
    UiTree& tree,
    UiNodeID parent,
    const CraftingRecipeTileView& recipe,
    usize index,
    Vec2f offset) {
  const UiTheme& theme = tree.theme();
  UiElement slot;
  slot.stable_id = "crafting.recipe." + std::to_string(index);
  slot.type = UiElementType::Slot;
  slot.layout.width_mode = UiSizeMode::Fixed;
  slot.layout.height_mode = UiSizeMode::Fixed;
  slot.layout.width = RecipeSlotSize;
  slot.layout.height = RecipeSlotSize;
  slot.layout.offset = offset;
  slot.style.background =
      recipe.selected ? theme.colors.surface_selected : theme.colors.surface_alt;
  slot.style.background.w = recipe.craftable ? 0.92F : 0.50F;
  slot.style.background_hover = theme.colors.surface_hover;
  slot.style.background_pressed = theme.colors.surface_pressed;
  slot.style.border = recipe.craftable ? theme.colors.border_focus : theme.colors.border;
  slot.style.border_width = theme.layout.border_width;
  slot.style.radius = theme.layout.radius_sm;
  slot.selected = recipe.selected;
  slot.focusable = true;
  slot.action = "crafting.recipe:" + recipe.id;
  const UiNodeID slot_id = tree.add_child(parent, std::move(slot));
  add_item_slot_children(
      tree,
      slot_id,
      "crafting.recipe." + std::to_string(index),
      static_cast<u8>(std::min<usize>(index, 255U)),
      &recipe.icon,
      false);
}

void append_crafting_ui(
    UiTree& tree,
    UiNodeID panel_id,
    const PlayerMenuUiModel& model) {
  const UiTheme& theme = tree.theme();
  const CraftingUiModel& crafting = model.crafting;

  UiElement categories;
  categories.stable_id = "crafting.categories";
  categories.type = UiElementType::VerticalStack;
  categories.layout.spacing = 6.0F;
  categories.layout.offset = {20.0F, PlayerMenuContentTop};
  categories.hit_test = false;
  const UiNodeID categories_id = tree.add_child(panel_id, std::move(categories));

  for (usize index = 0; index < crafting.categories.size(); ++index) {
    const CraftingCategoryTabView& category = crafting.categories[index];
    UiElement button = menu_button(
        "crafting.category." + std::to_string(index),
        "crafting.category:" + category.id,
        category.label,
        category.selected,
        116.0F,
        34.0F,
        theme);
    add_menu_button(tree, categories_id, std::move(button), theme);
  }

  UiElement search = fixed_panel(
      "crafting.search",
      350.0F,
      34.0F,
      theme.colors.surface_alt);
  search.layout.offset = {154.0F, PlayerMenuContentTop};
  search.layout.padding = {12.0F, 12.0F, 7.0F, 7.0F};
  search.style.background.w = 0.78F;
  search.action = "crafting.search.focus";
  search.focusable = true;
  search.style.border_focus = theme.colors.border_focus;
  search.style.accent = theme.colors.accent;
  search.style.text_role = UiTextRole::Body;
  style_surface(search, theme, theme.layout.radius_sm);
  TextInputFieldView search_input;
  search_input.text = crafting.search;
  search_input.placeholder = "Search";
  search_input.cursor = crafting.search_cursor;
  search_input.selection_start = crafting.search_selection_start;
  search_input.selection_end = crafting.search_selection_end;
  search_input.active = crafting.search_active;
  search_input.text_color = theme.colors.text;
  search_input.placeholder_color = theme.colors.text_muted;
  search_input.placeholder_anchor = UiAnchor::Center;
  add_text_input_field(tree, panel_id, std::move(search), search_input);

  UiElement clear_button = menu_button(
      "crafting.search.clear",
      "crafting.search.clear",
      "X",
      false,
      34.0F,
      34.0F,
      theme);
  clear_button.layout.offset = {512.0F, PlayerMenuContentTop};
  add_menu_button(tree, panel_id, std::move(clear_button), theme);

  UiElement recipes_panel = fixed_panel(
      "crafting.recipes.panel",
      374.0F,
      214.0F,
      theme.colors.surface_alt);
  recipes_panel.layout.offset = {154.0F, 108.0F};
  recipes_panel.style.background.w = 0.38F;
  recipes_panel.hit_test = false;
  style_surface(recipes_panel, theme, theme.layout.radius_sm);
  const UiNodeID recipes_panel_id = tree.add_child(panel_id, std::move(recipes_panel));

  constexpr u32 RecipeColumns = 7;
  for (usize index = 0; index < crafting.recipes.size(); ++index) {
    const u32 column = static_cast<u32>(index % RecipeColumns);
    const u32 row = static_cast<u32>(index / RecipeColumns);
    if (row >= 4U) {
      break;
    }
    append_recipe_slot(
        tree,
        recipes_panel_id,
        crafting.recipes[index],
        index,
        {12.0F + static_cast<f32>(column) * (RecipeSlotSize + RecipeSlotSpacing),
         12.0F + static_cast<f32>(row) * (RecipeSlotSize + RecipeSlotSpacing)});
  }

  UiElement detail = fixed_panel(
      "crafting.detail",
      152.0F,
      214.0F,
      theme.colors.surface_alt);
  detail.layout.offset = {548.0F, 108.0F};
  detail.style.background.w = 0.46F;
  detail.hit_test = false;
  style_surface(detail, theme, theme.layout.radius_sm);
  const UiNodeID detail_id = tree.add_child(panel_id, std::move(detail));
  if (crafting.has_selected_recipe) {
    UiElement title = text_element(
        "crafting.detail.title",
        crafting.selected_recipe_label,
        theme.colors.text,
        UiTextRole::Body);
    title.layout.anchor = UiAnchor::TopCenter;
    title.layout.offset = {0.0F, 8.0F};
    tree.add_child(detail_id, std::move(title));
    for (usize index = 0; index < crafting.selected_ingredients.size() && index < 4U; ++index) {
      const CraftingIngredientLineView& ingredient = crafting.selected_ingredients[index];
      UiElement line = text_element(
          "crafting.detail.ingredient." + std::to_string(index),
          ingredient.label + " " + std::to_string(ingredient.owned) + "/" +
              std::to_string(ingredient.required),
          ingredient.satisfied ? theme.colors.success : theme.colors.warning,
          UiTextRole::Small);
      line.layout.anchor = UiAnchor::TopCenter;
      line.layout.offset = {0.0F, 38.0F + static_cast<f32>(index) * 22.0F};
      tree.add_child(detail_id, std::move(line));
    }
    UiElement craft = menu_button(
        "crafting.craft",
        "crafting.craft:" + crafting.selected_recipe_id,
        "Craft",
        false,
        112.0F,
        34.0F,
        theme);
    craft.layout.anchor = UiAnchor::BottomCenter;
    craft.layout.offset = {0.0F, -12.0F};
    craft.enabled = crafting.selected_recipe_craftable;
    add_menu_button(tree, detail_id, std::move(craft), theme);
  } else {
    UiElement empty = text_element(
        "crafting.detail.empty",
        "No recipe",
        theme.colors.text_muted,
        UiTextRole::Body);
    empty.layout.anchor = UiAnchor::Center;
    tree.add_child(detail_id, std::move(empty));
  }

  append_inventory_section(
      tree,
      panel_id,
      model.inventory,
      {154.0F, 324.0F},
      false,
      InventorySlotSize,
      InventorySlotSpacing,
      theme.layout.spacing_md);
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
      tree.add_child(stack_id, inventory_grid("inventory.grid", InventorySlotSpacing));
  append_inventory_slot_range(
      tree,
      grid_id,
      model,
      hotbar_slot_count,
      slot_total,
      InventorySlotSize);

  UiElement hotbar_separator;
  hotbar_separator.stable_id = "inventory.hotbar_separator";
  hotbar_separator.type = UiElementType::Panel;
  hotbar_separator.layout.width_mode = UiSizeMode::Fixed;
  hotbar_separator.layout.height_mode = UiSizeMode::Fixed;
  hotbar_separator.layout.width =
      inventory_grid_width(InventorySlotSize, InventorySlotSpacing);
  hotbar_separator.layout.height = 2.0F;
  hotbar_separator.style.background = theme.colors.border_focus;
  hotbar_separator.style.background.w = 0.58F;
  hotbar_separator.hit_test = false;
  tree.add_child(stack_id, std::move(hotbar_separator));

  if (hotbar_slot_count > 0U) {
    const UiNodeID hotbar_id =
        tree.add_child(stack_id, inventory_grid("inventory.hotbar", InventorySlotSpacing));
    append_inventory_slot_range(
        tree,
        hotbar_id,
        model,
        0U,
        hotbar_slot_count,
        InventorySlotSize);
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

void append_player_menu_ui(UiTree& tree, PlayerMenuUiModel model) {
  if (!model.visible) {
    return;
  }
  const UiTheme& theme = tree.theme();
  UiElement panel = fixed_panel(
      "player_menu.panel",
      PlayerMenuWidth,
      player_menu_height(model.active_tab_id),
      theme.colors.surface);
  panel.layout.anchor = UiAnchor::TopCenter;
  panel.layout.offset = {0.0F, player_menu_top_offset(model.viewport)};
  panel.style.background.w = std::min(panel.style.background.w, 0.96F);
  style_surface(panel, theme, theme.widgets.panel.radius);
  panel.hit_test = true;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement tabs;
  tabs.stable_id = "player_menu.tabs";
  tabs.type = UiElementType::HorizontalStack;
  tabs.layout.spacing = theme.layout.spacing_sm;
  tabs.layout.offset = {20.0F, 20.0F};
  tabs.hit_test = false;
  const UiNodeID tabs_id = tree.add_child(panel_id, std::move(tabs));
  for (const PlayerMenuTabButtonView& tab : model.tabs) {
    UiElement tab_button = menu_button(
        "player_menu.tab." + tab.id,
        "player_menu.tab:" + tab.id,
        tab.label,
        tab.selected,
        116.0F,
        34.0F,
        theme);
    add_menu_button(tree, tabs_id, std::move(tab_button), theme);
  }

  if (model.active_tab_id == "crafting") {
    append_crafting_ui(tree, panel_id, model);
  } else if (model.active_tab_id == "settings") {
    UiElement text = text_element(
        "player_menu.settings.placeholder",
        "Settings",
        theme.colors.text,
        UiTextRole::Heading);
    text.layout.anchor = UiAnchor::Center;
    tree.add_child(panel_id, std::move(text));
  } else if (model.active_tab_id == "exit") {
    UiElement text = text_element(
        "player_menu.exit.placeholder",
        "Exit Game",
        theme.colors.text,
        UiTextRole::Heading);
    text.layout.anchor = UiAnchor::Center;
    tree.add_child(panel_id, std::move(text));
  } else {
    append_inventory_section(
        tree,
        panel_id,
        std::move(model.inventory),
        {
            (PlayerMenuWidth - inventory_grid_width(
                                   PlayerMenuInventorySlotSize,
                                   PlayerMenuInventorySlotSpacing)) *
                0.5F,
            PlayerMenuContentTop,
        },
        true,
        PlayerMenuInventorySlotSize,
        PlayerMenuInventorySlotSpacing,
        theme.layout.spacing_lg);
  }

  if (model.inventory.cursor_stack.has_value() &&
      !model.inventory.cursor_stack->empty()) {
    UiElement cursor_slot;
    cursor_slot.stable_id = "player_menu.cursor_stack";
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
        "player_menu.cursor_stack",
        0,
        &model.inventory.cursor_stack.value(),
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

void append_player_menu_ui(UiTree& tree, PlayerMenuUiModel model) {
  widgets_detail::append_player_menu_ui(tree, std::move(model));
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
