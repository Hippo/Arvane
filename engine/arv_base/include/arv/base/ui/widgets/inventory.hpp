#pragma once

#include "arv/base/ui/screens/screen.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/base/ui/widgets/items.hpp"
#include "arv/player/container.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

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

struct ContainerUiModel {
  bool visible{};
  std::string title{};
  const player::ContainerUiLayoutDefinition* layout{};
  u8 selected_hotbar_slot{};
  u8 hotbar_slot_count{player::DefaultHotbarSlotCount};
  Vec2f viewport{};
  std::span<const UiItemStackView> container_slots{};
  std::span<const UiItemStackView> player_slots{};
  std::optional<UiItemStackView> cursor_stack{};
  Vec2f cursor_position{};
};

struct PlayerMenuTabButtonView {
  std::string id{};
  std::string label{};
  bool selected{};
};

struct CraftingCategoryTabView {
  std::string id{};
  std::string label{};
  UiItemStackView icon{};
  bool selected{};
};

struct CraftingIngredientLineView {
  std::string label{};
  u16 required{};
  u32 owned{};
  bool satisfied{};
};

struct CraftingRecipeTileView {
  std::string id{};
  std::string label{};
  UiItemStackView icon{};
  bool craftable{};
  bool selected{};
};

struct CraftingUiModel {
  std::string title{"Crafting"};
  std::string search{};
  usize search_cursor{};
  usize search_selection_start{};
  usize search_selection_end{};
  bool search_active{true};
  std::vector<CraftingCategoryTabView> categories{};
  std::vector<CraftingRecipeTileView> recipes{};
  std::string selected_recipe_id{};
  std::string selected_recipe_label{};
  UiItemStackView selected_recipe_icon{};
  std::vector<CraftingIngredientLineView> selected_ingredients{};
  bool selected_recipe_craftable{};
  bool has_selected_recipe{};
};

struct PlayerMenuSettingsCaptureView {
  std::string action_name{};
  u8 slot{};
};

struct PlayerMenuSettingsUiModel {
  std::string active_section_id{"overview"};
  f32 scroll_y{};
  RenderSettingsView render{};
  ControlsSettingsView controls{};
  InterfaceSettingsView interface{};
  std::vector<KeyBindEntryView> key_binds{};
  std::optional<PlayerMenuSettingsCaptureView> capture{};
};

struct PlayerMenuUiModel {
  bool visible{};
  std::string title{"Player Menu"};
  std::string active_tab_id{"inventory"};
  Vec2f viewport{};
  Vec2f cursor_position{};
  std::vector<PlayerMenuTabButtonView> tabs{};
  InventoryUiModel inventory{};
  CraftingUiModel crafting{};
  PlayerMenuSettingsUiModel settings{};
  bool show_quit{true};
};

void build_inventory_ui(UiTree& tree, InventoryUiModel model);
void append_player_menu_ui(UiTree& tree, PlayerMenuUiModel model);
void append_container_ui(UiTree& tree, ContainerUiModel model);
void append_item_tooltip(UiTree& tree, ItemTooltipUiModel model);

} // namespace arv::ui
