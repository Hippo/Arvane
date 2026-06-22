#include "arv/base/ui/widgets/chat.hpp"

#include "arv/base/ui/widgets/text_input.hpp"
#include "widget_common.hpp"

#include "arv/core/text_edit.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] std::string tr(const ChatUiModel& model, std::string_view key) {
  return model.language == nullptr ? std::string{key} : model.language->text(key);
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

struct WrappedChatRun {
  arv::text::RichTextRun run{};
  usize source_run_index{};
};

struct WrappedChatLine {
  std::string text{};
  std::vector<WrappedChatRun> runs{};
};

struct ChatWrapAtom {
  std::string_view text{};
  const arv::text::RichTextRun* source{};
  usize source_run_index{};
};

struct ChatWrapState {
  std::vector<WrappedChatLine> lines{};
  usize column{};
  std::vector<ChatWrapAtom> pending_spaces{};
  std::vector<ChatWrapAtom> pending_word{};
  usize pending_word_columns{};
};

[[nodiscard]] usize chat_wrap_columns(f32 width, const UiTheme& theme) noexcept {
  const f32 estimated_glyph_width =
      std::max(7.0F, static_cast<f32>(theme.typography.size_md) * 0.44F + ChatLetterSpacing);
  return std::max<usize>(8U, static_cast<usize>(std::max(1.0F, width) / estimated_glyph_width));
}

void append_wrapped_atom(
    WrappedChatLine& line,
    const ChatWrapAtom& atom) {
  line.text.append(atom.text);
  if (atom.source == nullptr) {
    return;
  }
  if (!line.runs.empty() && line.runs.back().source_run_index == atom.source_run_index) {
    line.runs.back().run.text.append(atom.text);
    return;
  }
  arv::text::RichTextRun run = *atom.source;
  run.text = std::string{atom.text};
  line.runs.push_back({std::move(run), atom.source_run_index});
}

void append_chat_wrap_line(ChatWrapState& state) {
  state.lines.emplace_back();
  state.column = 0U;
}

void append_chat_wrap_atom(ChatWrapState& state, const ChatWrapAtom& atom) {
  append_wrapped_atom(state.lines.back(), atom);
  ++state.column;
}

void append_chat_wrap_atoms(
    ChatWrapState& state,
    const std::vector<ChatWrapAtom>& atoms) {
  for (const ChatWrapAtom& atom : atoms) {
    append_chat_wrap_atom(state, atom);
  }
}

void append_chat_wrap_long_word(ChatWrapState& state, usize columns) {
  for (const ChatWrapAtom& atom : state.pending_word) {
    if (state.column >= columns && !state.lines.back().text.empty()) {
      append_chat_wrap_line(state);
    }
    append_chat_wrap_atom(state, atom);
  }
}

void flush_chat_wrap_word(ChatWrapState& state, usize columns) {
  if (state.pending_word.empty()) {
    return;
  }

  const bool line_has_text = !state.lines.back().text.empty();
  const usize separator_columns = line_has_text ? state.pending_spaces.size() : 0U;
  if (line_has_text &&
      state.column + separator_columns + state.pending_word_columns > columns) {
    append_chat_wrap_line(state);
  } else if (line_has_text) {
    append_chat_wrap_atoms(state, state.pending_spaces);
  }

  if (state.pending_word_columns <= columns) {
    append_chat_wrap_atoms(state, state.pending_word);
  } else {
    append_chat_wrap_long_word(state, columns);
  }

  state.pending_spaces.clear();
  state.pending_word.clear();
  state.pending_word_columns = 0U;
}

void consume_chat_wrap_atom(ChatWrapState& state, const ChatWrapAtom& atom, usize columns) {
  if (atom.text == "\n") {
    flush_chat_wrap_word(state, columns);
    state.pending_spaces.clear();
    append_chat_wrap_line(state);
    return;
  }
  if (atom.text == " ") {
    if (!state.pending_word.empty()) {
      flush_chat_wrap_word(state, columns);
    }
    state.pending_spaces.push_back(atom);
    return;
  }
  state.pending_word.push_back(atom);
  ++state.pending_word_columns;
}

[[nodiscard]] std::vector<WrappedChatLine> finish_chat_wrap(ChatWrapState& state, usize columns) {
  flush_chat_wrap_word(state, columns);
  if (state.lines.empty()) {
    state.lines.emplace_back();
  }
  return std::move(state.lines);
}

[[nodiscard]] std::vector<std::string> wrap_plain_chat_text(
    std::string_view text,
    usize columns) {
  ChatWrapState state;
  state.lines.emplace_back();
  for (usize index = 0U; index < text.size();) {
    const usize next = arv::core::next_utf8_codepoint(text, index);
    consume_chat_wrap_atom(state, {text.substr(index, next - index)}, columns);
    index = next;
  }
  std::vector<std::string> lines;
  std::vector<WrappedChatLine> wrapped = finish_chat_wrap(state, columns);
  lines.reserve(wrapped.size());
  for (WrappedChatLine& line : wrapped) {
    lines.push_back(std::move(line.text));
  }
  if (lines.empty()) {
    lines.emplace_back();
  }
  return lines;
}

[[nodiscard]] std::vector<WrappedChatLine> wrap_rich_chat_runs(
    const std::vector<arv::text::RichTextRun>& runs,
    usize columns) {
  ChatWrapState state;
  state.lines.emplace_back();
  for (usize run_index = 0U; run_index < runs.size(); ++run_index) {
    const arv::text::RichTextRun& run = runs[run_index];
    for (usize index = 0U; index < run.text.size();) {
      const usize next = arv::core::next_utf8_codepoint(run.text, index);
      const std::string_view codepoint = std::string_view{run.text}.substr(index, next - index);
      consume_chat_wrap_atom(state, {codepoint, &run, run_index}, columns);
      index = next;
    }
  }
  return finish_chat_wrap(state, columns);
}

[[nodiscard]] std::string wrapped_line_id(const std::string& line_id, usize wrap_index) {
  return wrap_index == 0U
      ? line_id
      : line_id + ".wrap." + std::to_string(wrap_index);
}

[[nodiscard]] Vec4f run_color(
    const arv::text::RichTextRun& run,
    const UiTheme& theme,
    f32 opacity) noexcept {
  const f32 scale = 1.0F / 255.0F;
  const arv::text::RgbColor color = run.style.color;
  return {
      static_cast<f32>(color.r) * scale,
      static_cast<f32>(color.g) * scale,
      static_cast<f32>(color.b) * scale,
      opacity <= 0.0F ? theme.colors.text.w : opacity};
}

[[nodiscard]] bool run_interactive(const arv::text::RichTextRun& run) noexcept {
  return run.click_event.has_value() || run.hover_event.has_value();
}

void append_chat_run(
    UiTree& tree,
    UiNodeID row_id,
    const std::string& line_id,
    usize source_run_index,
    const arv::text::RichTextRun& run,
    const UiTheme& theme,
    f32 opacity,
    f32 line_height) {
  if (run.text.empty()) {
    return;
  }

  const std::string run_id = line_id + ".run." + std::to_string(source_run_index);
  UiElement segment;
  segment.stable_id = run_id;
  segment.type = UiElementType::Panel;
  segment.layout.height_mode = UiSizeMode::Fixed;
  segment.layout.height = line_height;
  segment.style.background = {0.0F, 0.0F, 0.0F, 0.0F};
  segment.hit_test = run_interactive(run);
  segment.action = segment.hit_test ? "chat_run" : "";
  const UiNodeID segment_id = tree.add_child(row_id, std::move(segment));
  tree.add_child(
      segment_id,
      chat_line_text(
          run_id + ".shadow",
          run.text,
          {0.0F, 0.0F, 0.0F, opacity * 0.70F},
          {1.0F, 1.0F}));
  tree.add_child(
      segment_id,
      chat_line_text(
          run_id + ".text",
          run.text,
          run_color(run, theme, opacity)));
}

[[nodiscard]] UiElement chat_button(
    std::string stable_id,
    std::string action,
    f32 width,
    const UiTheme& theme,
    UiControlRole role = UiControlRole::Neutral) {
  UiElement button;
  button.stable_id = std::move(stable_id);
  button.type = UiElementType::Button;
  button.layout.width_mode = UiSizeMode::Fixed;
  button.layout.height_mode = UiSizeMode::Fixed;
  button.layout.width = width;
  button.layout.height = theme.widgets.button.height;
  button.action = std::move(action);
  button.focusable = true;
  if (role == UiControlRole::Primary) {
    button.style.background = theme.colors.accent;
    button.style.background_hover = theme.colors.accent_hover;
    button.style.background_pressed = theme.colors.accent_pressed;
  } else if (role == UiControlRole::Danger) {
    button.style.background = theme.colors.danger;
    button.style.background_hover = theme.colors.danger_hover;
    button.style.background_pressed = theme.colors.danger_pressed;
  } else {
    button.style.background = theme.colors.surface;
    button.style.background_hover = theme.colors.surface_hover;
    button.style.background_pressed = theme.colors.surface_pressed;
  }
  button.style.control_role = role;
  style_surface(button, theme, theme.widgets.button.radius);
  return button;
}

void append_url_confirmation(
    UiTree& tree,
    UiNodeID stack_id,
    const ChatUiModel& model,
    f32 width) {
  if (!model.pending_url_confirmation.has_value()) {
    return;
  }
  const UiTheme& theme = tree.theme();

  UiElement panel;
  panel.stable_id = "chat.url.panel";
  panel.type = UiElementType::VerticalStack;
  panel.layout.width_mode = UiSizeMode::Fixed;
  panel.layout.width = width;
  panel.layout.spacing = theme.layout.spacing_xs;
  panel.layout.padding = {
      theme.layout.spacing_sm,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm};
  panel.style.background = with_alpha(theme.colors.surface_alt, 0.92F);
  panel.hit_test = true;
  style_surface(panel, theme, theme.layout.radius_sm);
  const UiNodeID panel_id = tree.add_child(stack_id, std::move(panel));

  tree.add_child(
      panel_id,
      chat_line_text(
          "chat.url.prompt",
          tr(model, "ui.chat.url_prompt"),
          theme.colors.text,
          {}));
  tree.add_child(
      panel_id,
      chat_line_text(
          "chat.url.value",
          model.pending_url_confirmation.value(),
          theme.colors.text_muted,
          {}));

  UiElement actions;
  actions.stable_id = "chat.url.actions";
  actions.type = UiElementType::HorizontalStack;
  actions.layout.spacing = theme.layout.spacing_sm;
  actions.hit_test = false;
  const UiNodeID actions_id = tree.add_child(panel_id, std::move(actions));

  constexpr f32 ButtonWidth = 94.0F;
  const UiNodeID confirm_id = tree.add_child(
      actions_id,
      chat_button(
          "chat.url.confirm",
          "chat_url_confirm",
          ButtonWidth,
          theme,
          UiControlRole::Primary));
  UiElement confirm_text =
      chat_line_text("chat.url.confirm.text", tr(model, "ui.chat.url_open"), theme.colors.text);
  confirm_text.layout.anchor = UiAnchor::Center;
  tree.add_child(confirm_id, std::move(confirm_text));

  const UiNodeID cancel_id = tree.add_child(
      actions_id,
      chat_button("chat.url.cancel", "chat_url_cancel", ButtonWidth, theme));
  UiElement cancel_text =
      chat_line_text("chat.url.cancel.text", tr(model, "ui.menu.common.cancel"), theme.colors.text);
  cancel_text.layout.anchor = UiAnchor::Center;
  tree.add_child(cancel_id, std::move(cancel_text));
}

void append_chat_ui(UiTree& tree, ChatUiModel model) {
  if (!model.visible && !model.input_active) {
    return;
  }
  if (model.messages.empty() && !model.input_active) {
    return;
  }
  if (!model.input_active && model.max_visible_messages == 0U) {
    return;
  }
  const UiTheme& theme = tree.theme();
  constexpr f32 ChatWidth = 560.0F;
  const f32 chat_text_width = ChatWidth - theme.layout.spacing_sm * 2.0F;
  const f32 chat_line_height =
      static_cast<f32>(theme.typography.size_md) + theme.layout.spacing_sm;
  const usize wrap_columns = chat_wrap_columns(chat_text_width, theme);

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
  stack.style.background = with_alpha(theme.colors.surface_alt, ChatPanelAlpha);
  style_surface(stack, theme, theme.layout.radius_md);
  if (model.input_active) {
    stack.style.border = with_alpha(stack.style.border, ChatBorderAlpha);
  } else {
    stack.style.border = {};
    stack.style.border_width = 0.0F;
  }
  stack.hit_test = model.input_active;
  const UiNodeID stack_id = tree.add_child(tree.root_id(), std::move(stack));

  const usize max_visible_messages = std::max<usize>(1U, model.max_visible_messages);
  const usize max_scroll_offset =
      model.messages.size() > max_visible_messages
          ? model.messages.size() - max_visible_messages
          : 0U;
  const usize scroll_offset =
      std::min(model.history_scroll_offset, max_scroll_offset);
  const usize end =
      model.messages.size() > scroll_offset ? model.messages.size() - scroll_offset : 0U;
  const usize start = end > max_visible_messages ? end - max_visible_messages : 0U;
  for (usize index = start; index < end; ++index) {
    const ChatLineView& line = model.messages[index];
    const f32 opacity = std::clamp(line.opacity, 0.0F, 1.0F);
    const std::string line_id = "chat.line." + std::to_string(index);
    if (line.runs.empty()) {
      const std::vector<std::string> wrapped = wrap_plain_chat_text(line.text, wrap_columns);
      for (usize wrap_index = 0U; wrap_index < wrapped.size(); ++wrap_index) {
        const std::string visual_line_id = wrapped_line_id(line_id, wrap_index);
        UiElement row;
        row.stable_id = visual_line_id + ".row";
        row.type = UiElementType::Panel;
        row.layout.width_mode = UiSizeMode::Fixed;
        row.layout.height_mode = UiSizeMode::Fixed;
        row.layout.width = chat_text_width;
        row.layout.height = chat_line_height;
        row.style.background = {0.0F, 0.0F, 0.0F, 0.0F};
        row.hit_test = false;
        row.clip_children = true;
        const UiNodeID row_id = tree.add_child(stack_id, std::move(row));
        tree.add_child(
            row_id,
            chat_line_text(
                visual_line_id + ".shadow",
                wrapped[wrap_index],
                {0.0F, 0.0F, 0.0F, opacity * 0.70F},
                {1.0F, 1.0F}));
        tree.add_child(
            row_id,
            chat_line_text(
                visual_line_id,
                wrapped[wrap_index],
                {theme.colors.text.x,
                 theme.colors.text.y,
                 theme.colors.text.z,
                 opacity}));
      }
    } else {
      const std::vector<WrappedChatLine> wrapped =
          wrap_rich_chat_runs(line.runs, wrap_columns);
      for (usize wrap_index = 0U; wrap_index < wrapped.size(); ++wrap_index) {
        const std::string visual_line_id = wrapped_line_id(line_id, wrap_index);
        UiElement row;
        row.stable_id = visual_line_id + ".row";
        row.type = UiElementType::HorizontalStack;
        row.layout.width_mode = UiSizeMode::Fixed;
        row.layout.height_mode = UiSizeMode::Fixed;
        row.layout.width = chat_text_width;
        row.layout.height = chat_line_height;
        row.layout.spacing = ChatLetterSpacing;
        row.style.background = {0.0F, 0.0F, 0.0F, 0.0F};
        row.hit_test = false;
        row.clip_children = true;
        const UiNodeID row_id = tree.add_child(stack_id, std::move(row));
        for (const WrappedChatRun& wrapped_run : wrapped[wrap_index].runs) {
          append_chat_run(
              tree,
              row_id,
              visual_line_id,
              wrapped_run.source_run_index,
              wrapped_run.run,
              theme,
              opacity,
              chat_line_height);
        }
      }
    }
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
    input.style.border_focus = theme.colors.border_focus;
    input.style.accent = theme.colors.accent;
    input.style.text_role = UiTextRole::Body;
    input.style.letter_spacing = ChatLetterSpacing;
    input.focusable = true;
    input.action = "chat_input";
    std::string visual_text = "> " + model.draft;
    constexpr usize PromptBytes = 2U;
    TextInputFieldView input_view;
    input_view.text = std::move(visual_text);
    input_view.cursor = PromptBytes + model.draft_cursor;
    input_view.selection_start = PromptBytes + model.draft_selection_start;
    input_view.selection_end = PromptBytes + model.draft_selection_end;
    input_view.active = true;
    input_view.text_color = theme.colors.text;
    input_view.placeholder_color = theme.colors.text_muted;
    input_view.text_role = UiTextRole::Body;
    input_view.letter_spacing = ChatLetterSpacing;
    add_text_input_field(tree, stack_id, std::move(input), input_view);
  }

  append_url_confirmation(tree, stack_id, model, chat_text_width);
}

} // namespace widgets_detail

void build_chat_ui(UiTree& tree, ChatUiModel model) {
  tree.clear();
  widgets_detail::append_chat_ui(tree, model);
}

void append_chat_tooltip(UiTree& tree, ChatTooltipUiModel model) {
  if (!model.visible || model.text.empty()) {
    return;
  }
  if (tree.find_by_stable_id("chat.tooltip") != nullptr) {
    return;
  }

  const UiTheme& theme = tree.theme();
  constexpr f32 ApproxTooltipWidth = 260.0F;
  constexpr f32 ApproxTooltipHeight = 44.0F;
  const bool place_left = model.cursor_position.x + ApproxTooltipWidth >
                          model.viewport.x - theme.layout.spacing_sm;
  const bool place_above = model.cursor_position.y + ApproxTooltipHeight >
                           model.viewport.y - theme.layout.spacing_sm;

  UiElement tooltip;
  tooltip.stable_id = "chat.tooltip";
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
      "chat.tooltip.text",
      std::move(model.text),
      theme.colors.text,
      UiTextRole::Body);
  text.layout.anchor = UiAnchor::Center;
  tree.add_child(tooltip_id, std::move(text));
}

} // namespace arv::ui
