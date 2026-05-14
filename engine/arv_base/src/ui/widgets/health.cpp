#include "arv/base/ui/widgets/health.hpp"

#include "widget_common.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace arv::ui {
namespace widgets_detail {

void append_health_hud(UiTree& tree, HealthHudModel model) {
  if (!model.visible || !std::isfinite(model.max) || model.max <= 0.0F) {
    return;
  }

  const UiTheme& theme = tree.theme();
  const f32 section_value =
      std::isfinite(model.section_value) && model.section_value > 0.0F
          ? model.section_value
          : 1.0F;
  const u32 section_count = std::clamp(
      static_cast<u32>(std::ceil(model.max / section_value)),
      1U,
      40U);
  const f32 current = std::clamp(
      std::isfinite(model.current) ? model.current : 0.0F,
      0.0F,
      model.max);

  UiElement bar;
  bar.stable_id = "hud.health";
  bar.type = UiElementType::HorizontalStack;
  bar.layout.width_mode = UiSizeMode::Content;
  bar.layout.height_mode = UiSizeMode::Content;
  bar.layout.spacing = 2.0F;
  bar.layout.padding = {
      theme.layout.spacing_sm,
      theme.layout.spacing_xs,
      theme.layout.spacing_xs,
      theme.layout.spacing_xs};
  bar.layout.anchor = UiAnchor::BottomCenter;
  bar.layout.offset = {0.0F, -82.0F};
  bar.style.background = theme.colors.surface_alt;
  bar.style.background.w = std::min(bar.style.background.w, 0.74F);
  style_surface(bar, theme, theme.layout.radius_md);
  bar.hit_test = false;
  const UiNodeID bar_id = tree.add_child(tree.root_id(), std::move(bar));

  for (u32 section = 0; section < section_count; ++section) {
    const f32 start = static_cast<f32>(section) * section_value;
    const f32 end = start + section_value;
    const bool filled = current >= end;
    const bool partial = !filled && current > start;

    UiElement segment;
    segment.stable_id = "hud.health.segment." + std::to_string(section);
    segment.type = UiElementType::Panel;
    segment.layout.width_mode = UiSizeMode::Fixed;
    segment.layout.height_mode = UiSizeMode::Fixed;
    segment.layout.width = 9.0F;
    segment.layout.height = 13.0F;
    segment.style.background =
        filled ? theme.colors.danger :
        partial ? theme.colors.warning :
                  Vec4f{theme.colors.surface.x,
                        theme.colors.surface.y,
                        theme.colors.surface.z,
                        0.55F};
    segment.style.border = theme.colors.border;
    segment.style.border_width = theme.layout.border_width;
    segment.style.radius = theme.layout.radius_sm;
    segment.hit_test = false;
    tree.add_child(bar_id, std::move(segment));
  }

  (void)model.viewport;
}

} // namespace widgets_detail

void build_health_hud(UiTree& tree, HealthHudModel model) {
  tree.clear();
  widgets_detail::append_health_hud(tree, model);
}

} // namespace arv::ui
