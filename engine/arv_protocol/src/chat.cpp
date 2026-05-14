#include "arv/protocol/chat.hpp"

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

} // namespace

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
