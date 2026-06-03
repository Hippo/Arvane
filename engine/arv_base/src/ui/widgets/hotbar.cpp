#include "arv/base/ui/widgets/hotbar.hpp"

#include "item_slot.hpp"

#include <algorithm>
#include <string>

namespace arv::ui {
namespace widgets_detail {
namespace {

constexpr Vec2f HotbarHudOffset{0.0F, -24.0F};
constexpr Vec2f HotbarItemNamePopupOffset{0.0F, -148.0F};

} // namespace

void append_hotbar_item_name_popup(UiTree& tree, const HotbarHudModel& model) {
  if (model.item_name_popup.empty() || model.item_name_popup_alpha <= 0.0F) {
    return;
  }

  const UiTheme& theme = tree.theme();
  const f32 alpha = std::clamp(model.item_name_popup_alpha, 0.0F, 1.0F);
  UiElement popup;
  popup.stable_id = "hud.hotbar.item_name";
  popup.type = UiElementType::Panel;
  popup.layout.width_mode = UiSizeMode::Content;
  popup.layout.height_mode = UiSizeMode::Content;
  popup.layout.padding = {
      theme.layout.spacing_md,
      theme.layout.spacing_md,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm};
  popup.layout.anchor = UiAnchor::BottomCenter;
  popup.layout.offset = HotbarItemNamePopupOffset;
  popup.style.background = theme.colors.surface_alt;
  popup.style.background.w = std::min(popup.style.background.w, 0.86F) * alpha;
  popup.hit_test = false;
  style_surface(popup, theme, theme.layout.radius_md);
  popup.style.border.w *= alpha;
  const UiNodeID popup_id = tree.add_child(tree.root_id(), std::move(popup));

  UiElement text = text_element(
      "hud.hotbar.item_name.text",
      model.item_name_popup,
      theme.colors.text,
      UiTextRole::Body);
  text.layout.anchor = UiAnchor::Center;
  text.style.foreground.w *= alpha;
  tree.add_child(popup_id, std::move(text));
}

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
  hotbar.layout.offset = HotbarHudOffset;
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

  append_hotbar_item_name_popup(tree, model);

  (void)model.viewport;
}

} // namespace widgets_detail

void build_hotbar_hud(UiTree& tree, HotbarHudModel model) {
  tree.clear();
  widgets_detail::append_hotbar_hud(tree, model);
}

} // namespace arv::ui
