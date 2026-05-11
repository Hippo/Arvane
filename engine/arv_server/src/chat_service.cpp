#include "arv/server/chat_service.hpp"

#include <algorithm>

namespace arv::server {
namespace {

[[nodiscard]] bool ascii_space(char ch) noexcept {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

[[nodiscard]] std::string trim_ascii(std::string_view text) {
  usize begin = 0U;
  while (begin < text.size() && ascii_space(text[begin])) {
    ++begin;
  }
  usize end = text.size();
  while (end > begin && ascii_space(text[end - 1U])) {
    --end;
  }
  return std::string{text.substr(begin, end - begin)};
}

[[nodiscard]] bool disallowed_control(char ch, bool allow_newlines) noexcept {
  const auto value = static_cast<unsigned char>(ch);
  if (value >= 0x20U) {
    return false;
  }
  if (ch == '\n' || ch == '\r') {
    return !allow_newlines;
  }
  return ch != '\t';
}

[[nodiscard]] std::string collapse_ascii_whitespace(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool previous_space = false;
  for (const char ch : text) {
    if (ascii_space(ch)) {
      if (!previous_space) {
        out.push_back(' ');
      }
      previous_space = true;
      continue;
    }
    out.push_back(ch);
    previous_space = false;
  }
  return out;
}

[[nodiscard]] protocol::TextStylePatch chat_color(u8 r, u8 g, u8 b) {
  return protocol::style_patch_color(r, g, b);
}

} // namespace

protocol::RichText ChatFormatter::format_player_chat(
    const ChatFormatContext& context) const {
  const std::string sender_name =
      context.sender_name.empty() ? std::string{"player"} : context.sender_name;

  protocol::RichTextBuilder builder;
  if (context.rank_prefix.has_value()) {
    builder.append_component(context.rank_prefix.value());
    builder.append(" ");
  }

  builder
      .append(sender_name, chat_color(0xFFU, 0xFFU, 0xFFU))
      .click_suggest_command("/msg " + sender_name + " ")
      .hover_text("Click to message " + sender_name)
      .append(": ", chat_color(0xA8U, 0xA8U, 0xA8U))
      .append_literal(context.raw_message, chat_color(0xDDU, 0xDDU, 0xDDU));
  return builder.build();
}

protocol::RichText ChatFormatter::format_system_message(
    std::string_view message) const {
  return protocol::RichTextBuilder()
      .append(std::string{message}, chat_color(0xE6U, 0xE1U, 0x8DU))
      .build();
}

protocol::RichText ChatFormatter::format_error_message(
    std::string_view message) const {
  protocol::TextStylePatch style = chat_color(0xFFU, 0x6BU, 0x6BU);
  style.bold = true;
  return protocol::RichTextBuilder()
      .append(std::string{message}, std::move(style))
      .build();
}

Expected<std::string, ChatValidationError> validate_chat_message_text(
    std::string_view message,
    const ChatValidationConfig& config) {
  if (message.size() > config.max_message_bytes) {
    return Expected<std::string, ChatValidationError>::failure(
        {"Chat message is too long."});
  }
  if (!protocol::is_valid_utf8(message)) {
    return Expected<std::string, ChatValidationError>::failure(
        {"Chat message is not valid UTF-8."});
  }
  if (protocol::utf8_codepoint_count(message) > config.max_message_codepoints) {
    return Expected<std::string, ChatValidationError>::failure(
        {"Chat message has too many characters."});
  }
  for (const char ch : message) {
    if (disallowed_control(ch, config.allow_newlines)) {
      return Expected<std::string, ChatValidationError>::failure(
          {"Chat message contains a disallowed control character."});
    }
  }
  if (!config.allow_newlines &&
      (message.find('\n') != std::string_view::npos ||
       message.find('\r') != std::string_view::npos)) {
    return Expected<std::string, ChatValidationError>::failure(
        {"Chat messages cannot contain newlines."});
  }

  std::string normalized =
      config.trim_outer_whitespace ? trim_ascii(message) : std::string{message};
  if (config.collapse_repeated_whitespace) {
    normalized = collapse_ascii_whitespace(normalized);
  }
  if (normalized.empty()) {
    return Expected<std::string, ChatValidationError>::failure(
        {"Chat message cannot be empty."});
  }
  return Expected<std::string, ChatValidationError>::success(std::move(normalized));
}

bool consume_chat_rate_limit(
    ChatRateLimitState& state,
    const ChatValidationConfig& config,
    Seconds now_seconds) {
  if (!state.initialized) {
    const double capacity = static_cast<double>(config.max_messages_per_window);
    const double refill_per_second =
        config.rate_limit_window <= 0.0F
            ? capacity
            : capacity / static_cast<double>(config.rate_limit_window);
    state.limiter = core::TokenBucket{capacity, refill_per_second};
    state.initialized = true;
  }
  return state.limiter.try_consume(1.0, static_cast<double>(now_seconds));
}

} // namespace arv::server
