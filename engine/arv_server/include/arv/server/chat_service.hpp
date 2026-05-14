#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/rate_limiter.hpp"
#include "arv/core/time.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/text/chat_format.hpp"

#include <string>
#include <string_view>

namespace arv::server {

struct ChatValidationConfig {
  usize max_message_bytes{protocol::MaxServerboundChatMessageBytes};
  usize max_message_codepoints{256U};
  bool allow_newlines{};
  bool trim_outer_whitespace{true};
  bool collapse_repeated_whitespace{};
  usize max_messages_per_window{5U};
  Seconds rate_limit_window{3.0F};
};

struct ChatValidationError {
  std::string message{};
};

struct ChatRateLimitState {
  core::TokenBucket limiter{};
  bool initialized{};
};

[[nodiscard]] Expected<std::string, ChatValidationError> validate_chat_message_text(
    std::string_view message,
    const ChatValidationConfig& config = {},
    const arv::text::LanguageCatalog* language = nullptr);

[[nodiscard]] bool consume_chat_rate_limit(
    ChatRateLimitState& state,
    const ChatValidationConfig& config,
    Seconds now_seconds);

} // namespace arv::server
