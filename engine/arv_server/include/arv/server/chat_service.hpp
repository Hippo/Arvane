#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/rate_limiter.hpp"
#include "arv/core/time.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/protocol/chat.hpp"

#include <optional>
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

struct ChatFormatContext {
  crypto::PlayerProfileID sender_profile_id{};
  std::string sender_name{};
  std::string channel{"arvane:global"};
  std::string raw_message{};
  std::optional<std::string> rank_id{};
  std::optional<protocol::RichText> rank_prefix{};
  bool is_private_message{};
  bool is_system_message{};
};

class ChatFormatter {
public:
  [[nodiscard]] protocol::RichText format_player_chat(
      const ChatFormatContext& context) const;
  [[nodiscard]] protocol::RichText format_system_message(
      std::string_view message) const;
  [[nodiscard]] protocol::RichText format_error_message(
      std::string_view message) const;
};

[[nodiscard]] Expected<std::string, ChatValidationError> validate_chat_message_text(
    std::string_view message,
    const ChatValidationConfig& config = {});

[[nodiscard]] bool consume_chat_rate_limit(
    ChatRateLimitState& state,
    const ChatValidationConfig& config,
    Seconds now_seconds);

} // namespace arv::server
