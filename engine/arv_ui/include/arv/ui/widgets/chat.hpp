#pragma once

#include "arv/protocol/chat.hpp"
#include "arv/ui/core/ui.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::ui {

struct ChatLineView {
  std::string text{};
  f32 opacity{1.0F};
  std::vector<protocol::RichTextRun> runs{};
};

struct ChatUiModel {
  bool visible{true};
  bool input_active{};
  std::span<const ChatLineView> messages{};
  std::string draft{};
  std::optional<std::string> pending_url_confirmation{};
  usize max_visible_messages{8U};
};

struct ChatTooltipUiModel {
  bool visible{};
  std::string text{};
  Vec2f viewport{};
  Vec2f cursor_position{};
};

void build_chat_ui(UiTree& tree, ChatUiModel model);
void append_chat_tooltip(UiTree& tree, ChatTooltipUiModel model);

} // namespace arv::ui
