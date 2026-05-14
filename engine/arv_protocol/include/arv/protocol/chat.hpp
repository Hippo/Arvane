#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/text/rich_text.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::protocol {

inline constexpr std::string_view ChatChannelGlobal = "arvane:global";
inline constexpr std::string_view ChatChannelSystem = "arvane:system";
inline constexpr usize MaxServerboundChatMessageBytes = 512U;
inline constexpr usize MaxChatChannelBytes = 128U;

using ChatMessageId = u64;

using arv::text::ClickAction;
using arv::text::ClickEvent;
using arv::text::HoverAction;
using arv::text::HoverEvent;
using arv::text::MaxRichTextFontBytes;
using arv::text::MaxRichTextTranslationKeyBytes;
using arv::text::ResolvedTextStyle;
using arv::text::RgbColor;
using arv::text::RichText;
using arv::text::RichTextBuilder;
using arv::text::RichTextLimits;
using arv::text::RichTextNodeKind;
using arv::text::RichTextRun;
using arv::text::RichTextValidation;
using arv::text::TextStylePatch;
using arv::text::flatten_rich_text;
using arv::text::is_valid_utf8;
using arv::text::make_show_text_hover;
using arv::text::plain_rich_text;
using arv::text::style_patch_color;
using arv::text::to_plain_text;
using arv::text::translatable_rich_text;
using arv::text::utf8_codepoint_count;
using arv::text::validate_rich_text;

enum class ChatMessageKind : u8 {
  Chat,
  System,
  Announcement,
  PrivateMessage,
  Error,
};

struct ServerboundChatMessage {
  std::string message{};
  std::optional<std::string> channel{};
};

struct ClientboundChatMessage {
  ChatMessageId message_id{};
  std::string channel{"arvane:global"};
  ChatMessageKind kind{ChatMessageKind::Chat};
  RichText message{};
};

[[nodiscard]] std::vector<u8> serialize_rich_text(const RichText& text);
[[nodiscard]] Expected<RichText, ProtocolError> deserialize_rich_text(
    std::span<const u8> bytes,
    const RichTextLimits& limits = {});

[[nodiscard]] std::vector<u8> serialize_serverbound_chat_message(
    const ServerboundChatMessage& message);
[[nodiscard]] Expected<ServerboundChatMessage, ProtocolError>
deserialize_serverbound_chat_message(std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_clientbound_chat_message(
    const ClientboundChatMessage& message);
[[nodiscard]] Expected<ClientboundChatMessage, ProtocolError>
deserialize_clientbound_chat_message(
    std::span<const u8> bytes,
    const RichTextLimits& limits = {});

} // namespace arv::protocol
