#pragma once

#include "arv/ui/core/ui.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace arv::ui {

struct TextInputFieldView {
  std::string text{};
  std::string placeholder{};
  usize cursor{};
  usize selection_start{};
  usize selection_end{};
  bool active{};
  Vec4f text_color{0.930F, 0.945F, 0.960F, 1.0F};
  Vec4f placeholder_color{0.660F, 0.700F, 0.740F, 1.0F};
  UiTextRole text_role{UiTextRole::Body};
  f32 letter_spacing{};
  UiAnchor placeholder_anchor{UiAnchor::TopLeft};
};

inline void apply_text_input_field_state(
    UiElement& element,
    const TextInputFieldView& view) {
  const usize text_size = view.text.size();
  element.text = view.text;
  element.text_input = UiTextInputVisual{
      view.active,
      std::min(text_size, view.cursor),
      std::min(text_size, view.selection_start),
      std::min(text_size, view.selection_end)};
  element.clip_children = true;
  element.style.text_role = view.text_role;
  element.style.letter_spacing = view.letter_spacing;
}

[[nodiscard]] inline UiElement text_input_display_text(
    std::string stable_id,
    const TextInputFieldView& view) {
  const bool showing_placeholder =
      view.text.empty() && !view.placeholder.empty();
  UiElement element;
  element.stable_id = std::move(stable_id);
  element.type = UiElementType::Text;
  element.text = showing_placeholder ? view.placeholder : view.text;
  element.style.foreground =
      showing_placeholder ? view.placeholder_color : view.text_color;
  element.style.text_role = view.text_role;
  element.style.letter_spacing = view.letter_spacing;
  element.layout.anchor =
      showing_placeholder ? view.placeholder_anchor : UiAnchor::TopLeft;
  element.hit_test = false;
  return element;
}

inline UiNodeID add_text_input_field(
    UiTree& tree,
    UiNodeID parent,
    UiElement element,
    const TextInputFieldView& view) {
  const std::string stable_id = element.stable_id;
  apply_text_input_field_state(element, view);
  const UiNodeID field_id = tree.add_child(parent, std::move(element));
  tree.add_child(field_id, text_input_display_text(stable_id + ".text", view));
  return field_id;
}

} // namespace arv::ui
