#include "arv/base/ui/widgets/gameplay.hpp"

#include "widget_builders.hpp"
#include "widget_common.hpp"

#include <algorithm>
#include <utility>

namespace arv::ui {
namespace widgets_detail {

namespace {

constexpr Vec2f RidePromptOffset{0.0F, 54.0F};

[[nodiscard]] Vec4f with_alpha(Vec4f color, f32 alpha) noexcept {
  color.w *= std::clamp(alpha, 0.0F, 1.0F);
  return color;
}

} // namespace

void append_crosshair_hud(UiTree& tree, CrosshairHudModel model) {
  if (!model.visible || !model.image.valid || model.size <= 0.0F) {
    return;
  }

  UiElement crosshair;
  crosshair.stable_id = "hud.crosshair";
  crosshair.type = UiElementType::Image;
  crosshair.layout.width_mode = UiSizeMode::Fixed;
  crosshair.layout.height_mode = UiSizeMode::Fixed;
  crosshair.layout.width = model.size;
  crosshair.layout.height = model.size;
  crosshair.layout.anchor = UiAnchor::Center;
  crosshair.image = model.image;
  crosshair.hit_test = false;
  tree.add_child(tree.root_id(), std::move(crosshair));

  (void)model.viewport;
}

void append_ride_prompt_hud(UiTree& tree, RidePromptModel model) {
  const f32 alpha = std::clamp(model.alpha, 0.0F, 1.0F);
  if (!model.visible || model.key_label.empty() || model.suffix.empty() ||
      alpha <= 0.0F) {
    return;
  }

  const UiTheme& theme = tree.theme();
  UiElement prompt;
  prompt.stable_id = "hud.ride_prompt";
  prompt.type = UiElementType::HorizontalStack;
  prompt.layout.width_mode = UiSizeMode::Content;
  prompt.layout.height_mode = UiSizeMode::Content;
  prompt.layout.spacing = 0.0F;
  prompt.layout.padding = {
      theme.layout.spacing_md,
      theme.layout.spacing_md,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm};
  prompt.layout.anchor = UiAnchor::Center;
  prompt.layout.offset = RidePromptOffset;
  prompt.style.background = theme.colors.surface_alt;
  prompt.style.background.w = std::min(prompt.style.background.w, 0.86F);
  prompt.hit_test = false;
  style_surface(prompt, theme, theme.layout.radius_md);
  prompt.style.background = with_alpha(prompt.style.background, alpha);
  prompt.style.border = with_alpha(prompt.style.border, alpha);
  const UiNodeID prompt_id = tree.add_child(tree.root_id(), std::move(prompt));

  tree.add_child(
      prompt_id,
      text_element(
          "hud.ride_prompt.prefix",
          "Press ",
          with_alpha(theme.colors.text, alpha)));
  tree.add_child(
      prompt_id,
      text_element(
          "hud.ride_prompt.key",
          std::move(model.key_label),
          with_alpha(theme.colors.accent, alpha)));
  tree.add_child(
      prompt_id,
      text_element(
          "hud.ride_prompt.suffix",
          std::move(model.suffix),
          with_alpha(theme.colors.text, alpha)));

  (void)model.viewport;
}

} // namespace widgets_detail

void build_gameplay_ui(UiTree& tree, GameplayUiModel model) {
  tree.clear();
  widgets_detail::append_crosshair_hud(tree, model.crosshair);
  widgets_detail::append_health_hud(tree, model.health);
  widgets_detail::append_hotbar_hud(tree, model.hotbar);
  widgets_detail::append_ride_prompt_hud(tree, std::move(model.ride_prompt));
  widgets_detail::append_debug_hud(tree, model.debug);
  widgets_detail::append_scoreboard_ui(tree, model.scoreboard);
  widgets_detail::append_chat_ui(tree, model.chat);
  widgets_detail::append_tab_menu_ui(tree, model.tab_menu);
  widgets_detail::append_inventory_ui(tree, model.inventory);
  append_player_menu_ui(tree, std::move(model.player_menu));
  widgets_detail::append_container_ui(tree, std::move(model.container));
}

} // namespace arv::ui
