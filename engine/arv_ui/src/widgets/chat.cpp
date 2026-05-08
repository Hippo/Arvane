#include "arv/ui/widgets/chat.hpp"

#include "widget_common.hpp"

#include <algorithm>
#include <string>

namespace arv::ui {
namespace widgets_detail {

inline constexpr f32 ChatLetterSpacing = 1.0F;
inline constexpr f32 ChatPanelAlpha = 0.42F;
inline constexpr f32 ChatInputAlpha = 0.58F;
inline constexpr f32 ChatBorderAlpha = 0.62F;

[[nodiscard]] Vec4f with_alpha(Vec4f color, f32 alpha) noexcept {
  color.w = alpha;
  return color;
}

[[nodiscard]] UiElement chat_line_text(
    std::string stable_id,
    std::string text,
    Vec4f color,
    Vec2f offset = {}) {
  UiElement element = text_element(
      std::move(stable_id),
      std::move(text),
      color,
      UiTextRole::Body);
  element.layout.offset = offset;
  element.style.letter_spacing = ChatLetterSpacing;
  return element;
}

void append_chat_ui(UiTree& tree, ChatUiModel model) {
  if (!model.visible && !model.input_active) {
    return;
  }
  if (model.messages.empty() && !model.input_active) {
    return;
  }
  const UiTheme& theme = tree.theme();
  constexpr f32 ChatWidth = 420.0F;
  const f32 chat_text_width = ChatWidth - theme.layout.spacing_sm * 2.0F;
  const f32 chat_line_height =
      static_cast<f32>(theme.typography.size_md) + theme.layout.spacing_sm;

  UiElement stack;
  stack.stable_id = "chat.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.width_mode = UiSizeMode::Fixed;
  stack.layout.width = ChatWidth;
  stack.layout.spacing = theme.layout.spacing_xs;
  stack.layout.padding = {
      theme.layout.spacing_sm,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm};
  stack.layout.anchor = UiAnchor::BottomLeft;
  stack.layout.offset = {20.0F, -96.0F};
  stack.style.background = model.input_active ? with_alpha(theme.colors.surface_alt, ChatPanelAlpha)
                                              : Vec4f{0.0F, 0.0F, 0.0F, 0.0F};
  style_surface(stack, theme, theme.layout.radius_md);
  if (model.input_active) {
    stack.style.border = with_alpha(stack.style.border, ChatBorderAlpha);
  } else {
    stack.style.border = {};
    stack.style.border_width = 0.0F;
  }
  stack.hit_test = model.input_active;
  const UiNodeID stack_id = tree.add_child(tree.root_id(), std::move(stack));

  const usize start = model.messages.size() > 8U ? model.messages.size() - 8U : 0U;
  for (usize index = start; index < model.messages.size(); ++index) {
    const ChatLineView& line = model.messages[index];
    const f32 opacity = std::clamp(line.opacity, 0.0F, 1.0F);
    const std::string line_id = "chat.line." + std::to_string(index);
    UiElement row = fixed_panel(
        line_id + ".row",
        chat_text_width,
        chat_line_height,
        Vec4f{0.0F, 0.0F, 0.0F, 0.0F});
    row.hit_test = false;
    row.clip_children = true;
    const UiNodeID row_id = tree.add_child(stack_id, std::move(row));
    tree.add_child(
        row_id,
        chat_line_text(
            line_id + ".shadow",
            line.text,
            {0.0F, 0.0F, 0.0F, opacity * 0.70F},
            {1.0F, 1.0F}));
    tree.add_child(
        row_id,
        chat_line_text(
            line_id,
            line.text,
            {
                theme.colors.text.x,
                theme.colors.text.y,
                theme.colors.text.z,
                opacity}));
  }

  if (model.input_active) {
    UiElement input = fixed_panel(
        "chat.input",
        chat_text_width,
        std::max(36.0F, theme.widgets.input.height),
        with_alpha(theme.colors.surface, ChatInputAlpha));
    input.layout.padding = {
        theme.widgets.input.padding_x,
        theme.widgets.input.padding_x,
        theme.widgets.input.padding_y,
        theme.widgets.input.padding_y};
    style_surface(input, theme, theme.widgets.input.radius);
    input.style.border = with_alpha(input.style.border, ChatBorderAlpha);
    input.focusable = true;
    input.action = "chat_input";
    const UiNodeID input_id = tree.add_child(stack_id, std::move(input));
    tree.add_child(
        input_id,
        chat_line_text("chat.input.text", "> " + model.draft, theme.colors.text));
  }
}

} // namespace widgets_detail

void build_chat_ui(UiTree& tree, ChatUiModel model) {
  tree.clear();
  widgets_detail::append_chat_ui(tree, model);
}

} // namespace arv::ui
