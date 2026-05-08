#pragma once

#include "arv/ui/core/ui.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace arv::ui::widgets_detail {

[[nodiscard]] inline UiElement text_element(
    std::string stable_id,
    std::string text,
    Vec4f color = {0.92F, 0.94F, 0.96F, 1.0F},
    UiTextRole role = UiTextRole::Body) {
  UiElement element;
  element.stable_id = std::move(stable_id);
  element.type = UiElementType::Text;
  element.text = std::move(text);
  element.style.foreground = color;
  element.style.text_role = role;
  element.hit_test = false;
  return element;
}

inline void style_surface(UiElement& element, const UiTheme& theme, f32 radius) {
  element.style.border = theme.colors.border;
  element.style.border_width = theme.layout.border_width;
  element.style.border_focus = theme.colors.border_focus;
  element.style.focus_border_width = std::max(theme.layout.border_width * 2.0F, 2.0F);
  element.style.radius = radius;
}

[[nodiscard]] inline UiElement fixed_panel(
    std::string stable_id,
    f32 width,
    f32 height,
    Vec4f background) {
  UiElement panel;
  panel.stable_id = std::move(stable_id);
  panel.type = UiElementType::Panel;
  panel.layout.width_mode = UiSizeMode::Fixed;
  panel.layout.height_mode = UiSizeMode::Fixed;
  panel.layout.width = width;
  panel.layout.height = height;
  panel.style.background = background;
  return panel;
}

[[nodiscard]] inline UiElement image_element(
    std::string stable_id,
    UiImageData image,
    f32 width,
    f32 height) {
  UiElement element;
  element.stable_id = std::move(stable_id);
  element.type = UiElementType::Image;
  element.layout.width_mode = UiSizeMode::Fixed;
  element.layout.height_mode = UiSizeMode::Fixed;
  element.layout.width = width;
  element.layout.height = height;
  element.layout.anchor = UiAnchor::Center;
  element.image = image;
  element.hit_test = false;
  return element;
}

} // namespace arv::ui::widgets_detail
