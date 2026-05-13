#include "arv/protocol/chat.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace arv::protocol {
namespace {

enum StyleFlag : u16 {
  StyleColor = 1U << 0U,
  StyleBold = 1U << 1U,
  StyleItalic = 1U << 2U,
  StyleUnderlined = 1U << 3U,
  StyleStrikethrough = 1U << 4U,
  StyleObfuscated = 1U << 5U,
  StyleFont = 1U << 6U,
};

inline constexpr u16 KnownStyleFlags = StyleColor | StyleBold | StyleItalic |
                                       StyleUnderlined | StyleStrikethrough |
                                       StyleObfuscated | StyleFont;

struct RichTextReadStats {
  usize nodes{};
};

struct RichTextValidateStats {
  usize nodes{};
  usize text_bytes{};
};

[[nodiscard]] Expected<void, ProtocolError> require_no_trailing_bytes(
    MessageReader& reader,
    std::string_view packet_name) {
  if (reader.remaining() != 0U) {
    std::string message{"trailing bytes in "};
    message.append(packet_name);
    return Expected<void, ProtocolError>::failure(ProtocolError{std::move(message)});
  }
  return Expected<void, ProtocolError>::success();
}

[[nodiscard]] Expected<std::string, ProtocolError> read_bounded_string(
    MessageReader& reader,
    usize max_bytes,
    std::string_view label,
    bool require_utf8 = true) {
  auto text = reader.read_string();
  if (!text) {
    return Expected<std::string, ProtocolError>::failure(text.error());
  }
  if (text.value().size() > max_bytes) {
    std::string message{label};
    message.append(" exceeds maximum length");
    return Expected<std::string, ProtocolError>::failure(ProtocolError{std::move(message)});
  }
  if (require_utf8 && !is_valid_utf8(text.value())) {
    std::string message{label};
    message.append(" is not valid UTF-8");
    return Expected<std::string, ProtocolError>::failure(ProtocolError{std::move(message)});
  }
  return Expected<std::string, ProtocolError>::success(std::move(text.value()));
}

[[nodiscard]] bool valid_chat_kind(ChatMessageKind kind) noexcept {
  switch (kind) {
    case ChatMessageKind::Chat:
    case ChatMessageKind::System:
    case ChatMessageKind::Announcement:
    case ChatMessageKind::PrivateMessage:
    case ChatMessageKind::Error:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_click_action(ClickAction action) noexcept {
  switch (action) {
    case ClickAction::OpenUrl:
    case ClickAction::SuggestCommand:
    case ClickAction::RunCommand:
    case ClickAction::CopyToClipboard:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_hover_action(HoverAction action) noexcept {
  switch (action) {
    case HoverAction::ShowText:
    case HoverAction::ShowItem:
    case HoverAction::ShowEntity:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_rich_text_node_kind(RichTextNodeKind kind) noexcept {
  switch (kind) {
    case RichTextNodeKind::Literal:
    case RichTextNodeKind::Translatable:
      return true;
  }
  return false;
}

void write_style_patch(MessageWriter& writer, const TextStylePatch& style) {
  u16 flags = 0U;
  if (style.color.has_value()) {
    flags |= StyleColor;
  }
  if (style.bold.has_value()) {
    flags |= StyleBold;
  }
  if (style.italic.has_value()) {
    flags |= StyleItalic;
  }
  if (style.underlined.has_value()) {
    flags |= StyleUnderlined;
  }
  if (style.strikethrough.has_value()) {
    flags |= StyleStrikethrough;
  }
  if (style.obfuscated.has_value()) {
    flags |= StyleObfuscated;
  }
  if (style.font.has_value()) {
    flags |= StyleFont;
  }

  writer.write<u16>(flags);
  if (style.color.has_value()) {
    writer.write<u8>(style.color->r);
    writer.write<u8>(style.color->g);
    writer.write<u8>(style.color->b);
  }
  if (style.bold.has_value()) {
    writer.write_bool(style.bold.value());
  }
  if (style.italic.has_value()) {
    writer.write_bool(style.italic.value());
  }
  if (style.underlined.has_value()) {
    writer.write_bool(style.underlined.value());
  }
  if (style.strikethrough.has_value()) {
    writer.write_bool(style.strikethrough.value());
  }
  if (style.obfuscated.has_value()) {
    writer.write_bool(style.obfuscated.value());
  }
  if (style.font.has_value()) {
    writer.write_string(style.font.value());
  }
}

[[nodiscard]] Expected<TextStylePatch, ProtocolError> read_style_patch(
    MessageReader& reader) {
  auto flags = reader.read<u16>();
  if (!flags) {
    return Expected<TextStylePatch, ProtocolError>::failure(flags.error());
  }
  if ((flags.value() & ~KnownStyleFlags) != 0U) {
    return Expected<TextStylePatch, ProtocolError>::failure(
        ProtocolError{"unknown rich text style flags"});
  }

  TextStylePatch style;
  if ((flags.value() & StyleColor) != 0U) {
    auto r = reader.read<u8>();
    auto g = reader.read<u8>();
    auto b = reader.read<u8>();
    if (!r || !g || !b) {
      return Expected<TextStylePatch, ProtocolError>::failure(
          ProtocolError{"failed to decode rich text color"});
    }
    style.color = RgbColor{r.value(), g.value(), b.value()};
  }
  if ((flags.value() & StyleBold) != 0U) {
    auto value = reader.read_bool();
    if (!value) {
      return Expected<TextStylePatch, ProtocolError>::failure(value.error());
    }
    style.bold = value.value();
  }
  if ((flags.value() & StyleItalic) != 0U) {
    auto value = reader.read_bool();
    if (!value) {
      return Expected<TextStylePatch, ProtocolError>::failure(value.error());
    }
    style.italic = value.value();
  }
  if ((flags.value() & StyleUnderlined) != 0U) {
    auto value = reader.read_bool();
    if (!value) {
      return Expected<TextStylePatch, ProtocolError>::failure(value.error());
    }
    style.underlined = value.value();
  }
  if ((flags.value() & StyleStrikethrough) != 0U) {
    auto value = reader.read_bool();
    if (!value) {
      return Expected<TextStylePatch, ProtocolError>::failure(value.error());
    }
    style.strikethrough = value.value();
  }
  if ((flags.value() & StyleObfuscated) != 0U) {
    auto value = reader.read_bool();
    if (!value) {
      return Expected<TextStylePatch, ProtocolError>::failure(value.error());
    }
    style.obfuscated = value.value();
  }
  if ((flags.value() & StyleFont) != 0U) {
    auto value = read_bounded_string(reader, MaxRichTextFontBytes, "rich text font");
    if (!value) {
      return Expected<TextStylePatch, ProtocolError>::failure(value.error());
    }
    style.font = std::move(value.value());
  }
  return Expected<TextStylePatch, ProtocolError>::success(std::move(style));
}

void write_click_event(MessageWriter& writer, const ClickEvent& event) {
  writer.write<ClickAction>(event.action);
  writer.write_string(event.value);
}

[[nodiscard]] Expected<ClickEvent, ProtocolError> read_click_event(
    MessageReader& reader,
    const RichTextLimits& limits) {
  auto action = reader.read<ClickAction>();
  if (!action) {
    return Expected<ClickEvent, ProtocolError>::failure(action.error());
  }
  if (!valid_click_action(action.value())) {
    return Expected<ClickEvent, ProtocolError>::failure(
        ProtocolError{"invalid rich text click action"});
  }
  auto value = read_bounded_string(
      reader,
      limits.max_click_value_bytes,
      "rich text click value");
  if (!value) {
    return Expected<ClickEvent, ProtocolError>::failure(value.error());
  }
  return Expected<ClickEvent, ProtocolError>::success(
      {action.value(), std::move(value.value())});
}

void write_rich_text_node(MessageWriter& writer, const RichText& text);

void write_hover_event(MessageWriter& writer, const HoverEvent& event) {
  writer.write<HoverAction>(event.action);
  switch (event.action) {
    case HoverAction::ShowText:
      write_rich_text_node(
          writer,
          event.show_text == nullptr ? RichText{} : *event.show_text);
      return;
    case HoverAction::ShowItem:
    case HoverAction::ShowEntity:
      writer.write_string(event.fallback_text);
      return;
  }
}

[[nodiscard]] Expected<RichText, ProtocolError> read_rich_text_node(
    MessageReader& reader,
    const RichTextLimits& limits,
    usize depth,
    RichTextReadStats& stats);

[[nodiscard]] Expected<HoverEvent, ProtocolError> read_hover_event(
    MessageReader& reader,
    const RichTextLimits& limits,
    usize depth,
    RichTextReadStats& stats) {
  auto action = reader.read<HoverAction>();
  if (!action) {
    return Expected<HoverEvent, ProtocolError>::failure(action.error());
  }
  if (!valid_hover_action(action.value())) {
    return Expected<HoverEvent, ProtocolError>::failure(
        ProtocolError{"invalid rich text hover action"});
  }

  HoverEvent event;
  event.action = action.value();
  switch (event.action) {
    case HoverAction::ShowText: {
      auto text = read_rich_text_node(reader, limits, depth + 1U, stats);
      if (!text) {
        return Expected<HoverEvent, ProtocolError>::failure(text.error());
      }
      event.show_text = std::make_shared<RichText>(std::move(text.value()));
      return Expected<HoverEvent, ProtocolError>::success(std::move(event));
    }
    case HoverAction::ShowItem:
    case HoverAction::ShowEntity: {
      auto fallback = read_bounded_string(
          reader,
          limits.max_hover_text_bytes,
          "rich text unsupported hover fallback");
      if (!fallback) {
        return Expected<HoverEvent, ProtocolError>::failure(fallback.error());
      }
      event.fallback_text = std::move(fallback.value());
      return Expected<HoverEvent, ProtocolError>::success(std::move(event));
    }
  }
  return Expected<HoverEvent, ProtocolError>::failure(
      ProtocolError{"invalid rich text hover action"});
}

void write_rich_text_node(MessageWriter& writer, const RichText& text) {
  writer.write<RichTextNodeKind>(text.kind);
  writer.write_string(
      text.kind == RichTextNodeKind::Translatable ? text.translation_key
                                                  : text.text);
  write_style_patch(writer, text.style);
  writer.write<u32>(static_cast<u32>(text.children.size()));
  for (const RichText& child : text.children) {
    write_rich_text_node(writer, child);
  }
  writer.write_bool(text.click_event.has_value());
  if (text.click_event.has_value()) {
    write_click_event(writer, text.click_event.value());
  }
  writer.write_bool(text.hover_event.has_value());
  if (text.hover_event.has_value()) {
    write_hover_event(writer, text.hover_event.value());
  }
}

[[nodiscard]] Expected<RichText, ProtocolError> read_rich_text_node(
    MessageReader& reader,
    const RichTextLimits& limits,
    usize depth,
    RichTextReadStats& stats) {
  if (depth > limits.max_depth) {
    return Expected<RichText, ProtocolError>::failure(
        ProtocolError{"rich text exceeds maximum depth"});
  }
  ++stats.nodes;
  if (stats.nodes > limits.max_nodes) {
    return Expected<RichText, ProtocolError>::failure(
        ProtocolError{"rich text exceeds maximum node count"});
  }

  auto kind = reader.read<RichTextNodeKind>();
  if (!kind) {
    return Expected<RichText, ProtocolError>::failure(kind.error());
  }
  if (!valid_rich_text_node_kind(kind.value())) {
    return Expected<RichText, ProtocolError>::failure(
        ProtocolError{"invalid rich text node kind"});
  }
  const usize max_payload_bytes =
      kind.value() == RichTextNodeKind::Translatable
          ? MaxRichTextTranslationKeyBytes
          : limits.max_text_bytes;
  auto payload = read_bounded_string(
      reader,
      max_payload_bytes,
      kind.value() == RichTextNodeKind::Translatable
          ? std::string_view{"rich text translation key"}
          : std::string_view{"rich text node text"});
  if (!payload) {
    return Expected<RichText, ProtocolError>::failure(payload.error());
  }
  auto style = read_style_patch(reader);
  if (!style) {
    return Expected<RichText, ProtocolError>::failure(style.error());
  }
  auto child_count = reader.read<u32>();
  if (!child_count) {
    return Expected<RichText, ProtocolError>::failure(child_count.error());
  }
  if (child_count.value() > limits.max_nodes) {
    return Expected<RichText, ProtocolError>::failure(
        ProtocolError{"rich text child count exceeds maximum node count"});
  }

  RichText node;
  node.kind = kind.value();
  if (node.kind == RichTextNodeKind::Translatable) {
    node.translation_key = std::move(payload.value());
  } else {
    node.text = std::move(payload.value());
  }
  node.style = std::move(style.value());
  node.children.reserve(child_count.value());
  for (u32 index = 0; index < child_count.value(); ++index) {
    auto child = read_rich_text_node(reader, limits, depth + 1U, stats);
    if (!child) {
      return Expected<RichText, ProtocolError>::failure(child.error());
    }
    node.children.push_back(std::move(child.value()));
  }

  auto has_click = reader.read_bool();
  if (!has_click) {
    return Expected<RichText, ProtocolError>::failure(has_click.error());
  }
  if (has_click.value()) {
    auto click = read_click_event(reader, limits);
    if (!click) {
      return Expected<RichText, ProtocolError>::failure(click.error());
    }
    node.click_event = std::move(click.value());
  }

  auto has_hover = reader.read_bool();
  if (!has_hover) {
    return Expected<RichText, ProtocolError>::failure(has_hover.error());
  }
  if (has_hover.value()) {
    auto hover = read_hover_event(reader, limits, depth + 1U, stats);
    if (!hover) {
      return Expected<RichText, ProtocolError>::failure(hover.error());
    }
    node.hover_event = std::move(hover.value());
  }
  return Expected<RichText, ProtocolError>::success(std::move(node));
}

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

std::vector<u8> serialize_rich_text(const RichText& text) {
  MessageWriter writer;
  write_rich_text_node(writer, text);
  return std::move(writer).take_bytes();
}

Expected<RichText, ProtocolError> deserialize_rich_text(
    std::span<const u8> bytes,
    const RichTextLimits& limits) {
  if (bytes.size() > limits.max_serialized_size_bytes) {
    return Expected<RichText, ProtocolError>::failure(
        ProtocolError{"rich text payload exceeds maximum serialized size"});
  }
  MessageReader reader(bytes);
  RichTextReadStats stats;
  auto text = read_rich_text_node(reader, limits, 1U, stats);
  if (!text) {
    return Expected<RichText, ProtocolError>::failure(text.error());
  }
  auto trailing = require_no_trailing_bytes(reader, "rich text");
  if (!trailing) {
    return Expected<RichText, ProtocolError>::failure(trailing.error());
  }
  RichTextValidation validation = validate_rich_text(text.value(), limits);
  if (!validation.valid) {
    return Expected<RichText, ProtocolError>::failure(
        ProtocolError{std::move(validation.message)});
  }
  return Expected<RichText, ProtocolError>::success(std::move(text.value()));
}

std::vector<u8> serialize_serverbound_chat_message(
    const ServerboundChatMessage& message) {
  MessageWriter writer(sizeof(u16) + message.message.size() + sizeof(u8));
  writer.write_string(message.message);
  writer.write_bool(message.channel.has_value());
  if (message.channel.has_value()) {
    writer.write_string(message.channel.value());
  }
  return std::move(writer).take_bytes();
}

Expected<ServerboundChatMessage, ProtocolError> deserialize_serverbound_chat_message(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto message = read_bounded_string(
      reader,
      MaxServerboundChatMessageBytes,
      "serverbound chat message");
  if (!message) {
    return Expected<ServerboundChatMessage, ProtocolError>::failure(message.error());
  }
  auto has_channel = reader.read_bool();
  if (!has_channel) {
    return Expected<ServerboundChatMessage, ProtocolError>::failure(has_channel.error());
  }
  ServerboundChatMessage packet;
  packet.message = std::move(message.value());
  if (has_channel.value()) {
    auto channel = read_bounded_string(reader, MaxChatChannelBytes, "chat channel");
    if (!channel) {
      return Expected<ServerboundChatMessage, ProtocolError>::failure(channel.error());
    }
    packet.channel = std::move(channel.value());
  }
  auto trailing = require_no_trailing_bytes(reader, "serverbound chat message");
  if (!trailing) {
    return Expected<ServerboundChatMessage, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundChatMessage, ProtocolError>::success(std::move(packet));
}

std::vector<u8> serialize_clientbound_chat_message(
    const ClientboundChatMessage& message) {
  MessageWriter writer;
  writer.write<u64>(message.message_id);
  writer.write_string(message.channel);
  writer.write<ChatMessageKind>(message.kind);
  write_rich_text_node(writer, message.message);
  return std::move(writer).take_bytes();
}

Expected<ClientboundChatMessage, ProtocolError> deserialize_clientbound_chat_message(
    std::span<const u8> bytes,
    const RichTextLimits& limits) {
  if (bytes.size() > limits.max_serialized_size_bytes + MaxChatChannelBytes + 16U) {
    return Expected<ClientboundChatMessage, ProtocolError>::failure(
        ProtocolError{"clientbound chat message exceeds maximum serialized size"});
  }
  MessageReader reader(bytes);
  auto message_id = reader.read<u64>();
  auto channel = read_bounded_string(reader, MaxChatChannelBytes, "chat channel");
  auto kind = reader.read<ChatMessageKind>();
  if (!message_id || !channel || !kind) {
    return Expected<ClientboundChatMessage, ProtocolError>::failure(
        ProtocolError{"failed to decode clientbound chat message"});
  }
  if (!valid_chat_kind(kind.value())) {
    return Expected<ClientboundChatMessage, ProtocolError>::failure(
        ProtocolError{"invalid chat message kind"});
  }
  RichTextReadStats stats;
  auto message = read_rich_text_node(reader, limits, 1U, stats);
  if (!message) {
    return Expected<ClientboundChatMessage, ProtocolError>::failure(message.error());
  }
  auto trailing = require_no_trailing_bytes(reader, "clientbound chat message");
  if (!trailing) {
    return Expected<ClientboundChatMessage, ProtocolError>::failure(trailing.error());
  }
  RichTextValidation validation = validate_rich_text(message.value(), limits);
  if (!validation.valid) {
    return Expected<ClientboundChatMessage, ProtocolError>::failure(
        ProtocolError{std::move(validation.message)});
  }
  return Expected<ClientboundChatMessage, ProtocolError>::success(
      {message_id.value(), std::move(channel.value()), kind.value(), std::move(message.value())});
}

} // namespace arv::protocol
