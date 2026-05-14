#include "arv/text/rich_text.hpp"

#include <algorithm>
#include <utility>

namespace arv::text {
namespace {

struct RichTextValidateStats {
  usize nodes{};
  usize text_bytes{};
};

[[nodiscard]] ResolvedTextStyle resolve_style(
    ResolvedTextStyle inherited,
    const TextStylePatch& patch) {
  if (patch.color.has_value()) {
    inherited.color = patch.color.value();
  }
  if (patch.bold.has_value()) {
    inherited.bold = patch.bold.value();
  }
  if (patch.italic.has_value()) {
    inherited.italic = patch.italic.value();
  }
  if (patch.underlined.has_value()) {
    inherited.underlined = patch.underlined.value();
  }
  if (patch.strikethrough.has_value()) {
    inherited.strikethrough = patch.strikethrough.value();
  }
  if (patch.obfuscated.has_value()) {
    inherited.obfuscated = patch.obfuscated.value();
  }
  if (patch.font.has_value()) {
    inherited.font = patch.font.value();
  }
  return inherited;
}

void flatten_rich_text_into(
    const RichText& text,
    ResolvedTextStyle inherited,
    std::vector<RichTextRun>& runs) {
  const ResolvedTextStyle style = resolve_style(std::move(inherited), text.style);
  const std::string& display_text =
      text.kind == RichTextNodeKind::Translatable ? text.translation_key : text.text;
  if (!display_text.empty()) {
    runs.push_back({display_text, style, text.click_event, text.hover_event});
  }
  for (const RichText& child : text.children) {
    flatten_rich_text_into(child, style, runs);
  }
}

void append_plain_text(const RichText& text, std::string& out) {
  out.append(
      text.kind == RichTextNodeKind::Translatable ? text.translation_key : text.text);
  for (const RichText& child : text.children) {
    append_plain_text(child, out);
  }
}

[[nodiscard]] RichTextValidation validation_error(std::string message) {
  return {false, std::move(message)};
}

[[nodiscard]] RichTextValidation validate_rich_text_node(
    const RichText& text,
    const RichTextLimits& limits,
    usize depth,
    RichTextValidateStats& stats,
    bool hover_root) {
  if (depth > limits.max_depth) {
    return validation_error("rich text exceeds maximum depth");
  }
  ++stats.nodes;
  if (stats.nodes > limits.max_nodes) {
    return validation_error("rich text exceeds maximum node count");
  }
  if (!valid_rich_text_node_kind(text.kind)) {
    return validation_error("rich text contains invalid node kind");
  }
  const std::string& payload =
      text.kind == RichTextNodeKind::Translatable ? text.translation_key : text.text;
  stats.text_bytes += payload.size();
  if (stats.text_bytes > (hover_root ? limits.max_hover_text_bytes : limits.max_text_bytes)) {
    return validation_error("rich text exceeds maximum text bytes");
  }
  if (text.kind == RichTextNodeKind::Translatable &&
      text.translation_key.size() > MaxRichTextTranslationKeyBytes) {
    return validation_error("rich text translation key exceeds maximum length");
  }
  if (!is_valid_utf8(payload)) {
    return validation_error("rich text contains invalid UTF-8");
  }
  if (text.style.font.has_value()) {
    if (text.style.font->size() > MaxRichTextFontBytes) {
      return validation_error("rich text font exceeds maximum length");
    }
    if (!is_valid_utf8(text.style.font.value())) {
      return validation_error("rich text font contains invalid UTF-8");
    }
  }
  if (text.click_event.has_value()) {
    const ClickEvent& click = text.click_event.value();
    if (!valid_click_action(click.action)) {
      return validation_error("rich text contains invalid click action");
    }
    if (click.value.size() > limits.max_click_value_bytes) {
      return validation_error("rich text click value exceeds maximum length");
    }
    if (!is_valid_utf8(click.value)) {
      return validation_error("rich text click value contains invalid UTF-8");
    }
  }
  if (text.hover_event.has_value()) {
    const HoverEvent& hover = text.hover_event.value();
    if (!valid_hover_action(hover.action)) {
      return validation_error("rich text contains invalid hover action");
    }
    if (hover.action == HoverAction::ShowText && hover.show_text != nullptr) {
      RichTextValidateStats hover_stats;
      RichTextValidation hover_validation = validate_rich_text_node(
          *hover.show_text,
          limits,
          depth + 1U,
          hover_stats,
          true);
      if (!hover_validation.valid) {
        return hover_validation;
      }
    } else if (hover.fallback_text.size() > limits.max_hover_text_bytes) {
      return validation_error("rich text hover fallback exceeds maximum length");
    } else if (!is_valid_utf8(hover.fallback_text)) {
      return validation_error("rich text hover fallback contains invalid UTF-8");
    }
  }
  for (const RichText& child : text.children) {
    RichTextValidation child_validation = validate_rich_text_node(
        child,
        limits,
        depth + 1U,
        stats,
        hover_root);
    if (!child_validation.valid) {
      return child_validation;
    }
  }
  return {true, {}};
}

} // namespace

bool valid_click_action(ClickAction action) noexcept {
  switch (action) {
    case ClickAction::OpenUrl:
    case ClickAction::SuggestCommand:
    case ClickAction::RunCommand:
    case ClickAction::CopyToClipboard:
      return true;
  }
  return false;
}

bool valid_hover_action(HoverAction action) noexcept {
  switch (action) {
    case HoverAction::ShowText:
    case HoverAction::ShowItem:
    case HoverAction::ShowEntity:
      return true;
  }
  return false;
}

bool valid_rich_text_node_kind(RichTextNodeKind kind) noexcept {
  switch (kind) {
    case RichTextNodeKind::Literal:
    case RichTextNodeKind::Translatable:
      return true;
  }
  return false;
}

RichTextBuilder& RichTextBuilder::append(std::string text, TextStylePatch style) {
  root_.children.push_back(plain_rich_text(std::move(text), std::move(style)));
  return *this;
}

RichTextBuilder& RichTextBuilder::append_literal(std::string text, TextStylePatch style) {
  return append(std::move(text), std::move(style));
}

RichTextBuilder& RichTextBuilder::append_component(RichText component) {
  root_.children.push_back(std::move(component));
  return *this;
}

RichTextBuilder& RichTextBuilder::click_open_url(std::string value) {
  attach_click(ClickAction::OpenUrl, std::move(value));
  return *this;
}

RichTextBuilder& RichTextBuilder::click_suggest_command(std::string value) {
  attach_click(ClickAction::SuggestCommand, std::move(value));
  return *this;
}

RichTextBuilder& RichTextBuilder::click_run_command(std::string value) {
  attach_click(ClickAction::RunCommand, std::move(value));
  return *this;
}

RichTextBuilder& RichTextBuilder::click_copy_to_clipboard(std::string value) {
  attach_click(ClickAction::CopyToClipboard, std::move(value));
  return *this;
}

RichTextBuilder& RichTextBuilder::hover_text(std::string text) {
  return hover_text(plain_rich_text(std::move(text)));
}

RichTextBuilder& RichTextBuilder::hover_text(RichText text) {
  if (!root_.children.empty()) {
    root_.children.back().hover_event = make_show_text_hover(std::move(text));
  }
  return *this;
}

RichText RichTextBuilder::build() const {
  return root_;
}

void RichTextBuilder::attach_click(ClickAction action, std::string value) {
  if (!root_.children.empty()) {
    root_.children.back().click_event = ClickEvent{action, std::move(value)};
  }
}

TextStylePatch style_patch_color(RgbColor color) {
  TextStylePatch style;
  style.color = color;
  return style;
}

TextStylePatch style_patch_color(u8 r, u8 g, u8 b) {
  return style_patch_color(RgbColor{r, g, b});
}

RichText plain_rich_text(std::string text, TextStylePatch style) {
  RichText rich;
  rich.kind = RichTextNodeKind::Literal;
  rich.text = std::move(text);
  rich.style = std::move(style);
  return rich;
}

RichText translatable_rich_text(std::string translation_key, TextStylePatch style) {
  RichText rich;
  rich.kind = RichTextNodeKind::Translatable;
  rich.translation_key = std::move(translation_key);
  rich.style = std::move(style);
  return rich;
}

HoverEvent make_show_text_hover(RichText text) {
  HoverEvent event;
  event.action = HoverAction::ShowText;
  event.show_text = std::make_shared<RichText>(std::move(text));
  return event;
}

bool is_valid_utf8(std::string_view text) noexcept {
  usize offset = 0U;
  while (offset < text.size()) {
    const u8 first = static_cast<u8>(text[offset]);
    u32 codepoint = 0U;
    usize length = 0U;
    if (first <= 0x7FU) {
      codepoint = first;
      length = 1U;
    } else if ((first & 0xE0U) == 0xC0U) {
      codepoint = first & 0x1FU;
      length = 2U;
      if (codepoint == 0U) {
        return false;
      }
    } else if ((first & 0xF0U) == 0xE0U) {
      codepoint = first & 0x0FU;
      length = 3U;
    } else if ((first & 0xF8U) == 0xF0U) {
      codepoint = first & 0x07U;
      length = 4U;
    } else {
      return false;
    }
    if (offset + length > text.size()) {
      return false;
    }
    for (usize index = 1U; index < length; ++index) {
      const u8 next = static_cast<u8>(text[offset + index]);
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    if ((length == 2U && codepoint < 0x80U) ||
        (length == 3U && codepoint < 0x800U) ||
        (length == 4U && codepoint < 0x10000U) ||
        codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
      return false;
    }
    offset += length;
  }
  return true;
}

usize utf8_codepoint_count(std::string_view text) noexcept {
  usize count = 0U;
  for (usize offset = 0U; offset < text.size();) {
    const u8 first = static_cast<u8>(text[offset]);
    usize length = 1U;
    if ((first & 0xE0U) == 0xC0U) {
      length = 2U;
    } else if ((first & 0xF0U) == 0xE0U) {
      length = 3U;
    } else if ((first & 0xF8U) == 0xF0U) {
      length = 4U;
    }
    offset += std::min(length, text.size() - offset);
    ++count;
  }
  return count;
}

std::string to_plain_text(const RichText& text) {
  std::string out;
  append_plain_text(text, out);
  return out;
}

std::vector<RichTextRun> flatten_rich_text(
    const RichText& text,
    ResolvedTextStyle base_style) {
  std::vector<RichTextRun> runs;
  flatten_rich_text_into(text, std::move(base_style), runs);
  return runs;
}

RichTextValidation validate_rich_text(
    const RichText& text,
    const RichTextLimits& limits) {
  RichTextValidateStats stats;
  return validate_rich_text_node(text, limits, 1U, stats, false);
}

} // namespace arv::text
