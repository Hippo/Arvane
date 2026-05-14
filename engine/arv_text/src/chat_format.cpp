#include "arv/text/chat_format.hpp"

#include <utility>

namespace arv::text {
namespace {

[[nodiscard]] TextStylePatch chat_color(u8 r, u8 g, u8 b) {
  return style_patch_color(r, g, b);
}

[[nodiscard]] std::string tr(
    const LanguageCatalog* language,
    std::string_view key,
    std::initializer_list<FormatArg> args,
    std::string_view fallback) {
  return language == nullptr ? LanguageCatalog{}.format(key, args, fallback)
                             : language->format(key, args, fallback);
}

} // namespace

RichText ChatFormatter::format_player_chat(const ChatFormatContext& context) const {
  const std::string sender_name =
      context.sender_name.empty() ? std::string{"player"} : context.sender_name;

  RichTextBuilder builder;
  if (context.rank_prefix.has_value()) {
    builder.append_component(context.rank_prefix.value());
    builder.append(" ");
  }

  builder
      .append(sender_name, chat_color(0xFFU, 0xFFU, 0xFFU))
      .click_suggest_command("/msg " + sender_name + " ")
      .hover_text(tr(
          context.language,
          "server.chat.hover_message",
          {{"name", sender_name}},
          "Click to message {name}"))
      .append(": ", chat_color(0xA8U, 0xA8U, 0xA8U))
      .append_literal(context.raw_message, chat_color(0xDDU, 0xDDU, 0xDDU));
  return builder.build();
}

RichText ChatFormatter::format_system_message(std::string_view message) const {
  return RichTextBuilder()
      .append(std::string{message}, chat_color(0xE6U, 0xE1U, 0x8DU))
      .build();
}

RichText ChatFormatter::format_error_message(std::string_view message) const {
  TextStylePatch style = chat_color(0xFFU, 0x6BU, 0x6BU);
  style.bold = true;
  return RichTextBuilder()
      .append(std::string{message}, std::move(style))
      .build();
}

} // namespace arv::text
