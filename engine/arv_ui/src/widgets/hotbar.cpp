#include "arv/ui/widgets/hotbar.hpp"

#include "item_slot.hpp"

#include <algorithm>
#include <string>

namespace arv::ui {
namespace widgets_detail {

void append_hotbar_hud(UiTree& tree, HotbarHudModel model) {
  const UiTheme& theme = tree.theme();
  const u8 slot_count = static_cast<u8>(
      std::max<u32>(1U, std::min<u32>(model.slot_count, player::DefaultHotbarSlotCount)));
  const bool selected_slot_is_valid = model.selected_slot < slot_count;

  UiElement hotbar;
  hotbar.stable_id = "hud.hotbar";
  hotbar.type = UiElementType::HorizontalStack;
  hotbar.layout.width_mode = UiSizeMode::Content;
  hotbar.layout.height_mode = UiSizeMode::Content;
  hotbar.layout.spacing = 4.0F;
  hotbar.layout.padding = {
      theme.layout.spacing_sm,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm};
  hotbar.layout.anchor = UiAnchor::BottomCenter;
  hotbar.layout.offset = {0.0F, -24.0F};
  hotbar.style.background = theme.colors.surface_alt;
  hotbar.style.background.w = std::min(hotbar.style.background.w, 0.82F);
  style_surface(hotbar, theme, theme.layout.radius_lg);
  hotbar.hit_test = false;
  const UiNodeID hotbar_id = tree.add_child(tree.root_id(), std::move(hotbar));

  for (u8 slot = 0; slot < slot_count; ++slot) {
    const UiItemStackView* stack =
        slot < model.slots.size() ? &model.slots[slot] : nullptr;
    append_slot(
        tree,
        hotbar_id,
        "hud.hotbar.slot." + std::to_string(slot),
        slot,
        stack,
        selected_slot_is_valid && slot == model.selected_slot,
        false,
        42.0F);
  }

  (void)model.viewport;
}

} // namespace widgets_detail

void build_hotbar_hud(UiTree& tree, HotbarHudModel model) {
  tree.clear();
  widgets_detail::append_hotbar_hud(tree, model);
}

} // namespace arv::ui
