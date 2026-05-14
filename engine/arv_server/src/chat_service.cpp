#include "arv/server/chat_service.hpp"

#include "arv/text/language.hpp"

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

[[nodiscard]] std::string tr(
    const arv::text::LanguageCatalog* language,
    std::string_view key,
    std::string_view fallback) {
  return language == nullptr ? std::string{fallback} : language->text(key, fallback);
}

} // namespace

Expected<std::string, ChatValidationError> validate_chat_message_text(
    std::string_view message,
    const ChatValidationConfig& config,
    const arv::text::LanguageCatalog* language) {
  if (message.size() > config.max_message_bytes) {
    return Expected<std::string, ChatValidationError>::failure(
        {tr(language, "server.chat.validation.too_long", "Chat message is too long.")});
  }
  if (!arv::text::is_valid_utf8(message)) {
    return Expected<std::string, ChatValidationError>::failure(
        {tr(
            language,
            "server.chat.validation.invalid_utf8",
            "Chat message is not valid UTF-8.")});
  }
  if (arv::text::utf8_codepoint_count(message) > config.max_message_codepoints) {
    return Expected<std::string, ChatValidationError>::failure(
        {tr(
            language,
            "server.chat.validation.too_many_characters",
            "Chat message has too many characters.")});
  }
  for (const char ch : message) {
    if (disallowed_control(ch, config.allow_newlines)) {
      return Expected<std::string, ChatValidationError>::failure(
          {tr(
              language,
              "server.chat.validation.disallowed_control",
              "Chat message contains a disallowed control character.")});
    }
  }
  if (!config.allow_newlines &&
      (message.find('\n') != std::string_view::npos ||
       message.find('\r') != std::string_view::npos)) {
    return Expected<std::string, ChatValidationError>::failure(
        {tr(
            language,
            "server.chat.validation.no_newlines",
            "Chat messages cannot contain newlines.")});
  }

  std::string normalized =
      config.trim_outer_whitespace ? trim_ascii(message) : std::string{message};
  if (config.collapse_repeated_whitespace) {
    normalized = collapse_ascii_whitespace(normalized);
  }
  if (normalized.empty()) {
    return Expected<std::string, ChatValidationError>::failure(
        {tr(language, "server.chat.validation.empty", "Chat message cannot be empty.")});
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
