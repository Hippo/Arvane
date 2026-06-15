#pragma once

#include "arv/text/chat_message.hpp"
#include "arv/text/language.hpp"
#include "arv/text/rich_text.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace arv::text {

struct ChatFormatContext {
  const LanguageCatalog* language{};
  std::string sender_profile_id{};
  std::string sender_name{};
  std::string channel{"arvane:global"};
  std::string raw_message{};
  std::optional<std::string> rank_id{};
  std::optional<RichText> rank_prefix{};
  ChatMarkupPermissions message_markup{};
  bool is_private_message{};
  bool is_system_message{};
};

class ChatFormatter {
public:
  [[nodiscard]] RichText format_player_chat(const ChatFormatContext& context) const;
  [[nodiscard]] RichText format_system_message(std::string_view message) const;
  [[nodiscard]] RichText format_error_message(std::string_view message) const;
};

} // namespace arv::text
