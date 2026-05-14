#pragma once

#include "arv/core/types.hpp"
#include "arv/protocol/chat.hpp"

#include <chrono>
#include <string>

namespace arv::client {

inline constexpr usize DefaultMaxChatHistoryMessages = 1000U;

struct ChatHistoryEntry {
  protocol::ChatMessageId message_id{};
  std::string channel{};
  protocol::ChatMessageKind kind{protocol::ChatMessageKind::Chat};
  arv::text::RichText message{};
  u64 received_sequence{};
  std::chrono::steady_clock::time_point received_at{};
  bool local{};
};

} // namespace arv::client
