#pragma once

#include "arv/ui/core/ui.hpp"

#include <span>
#include <string>

namespace arv::ui {

struct ChatLineView {
  std::string text{};
  f32 opacity{1.0F};
};

struct ChatUiModel {
  bool visible{true};
  bool input_active{};
  std::span<const ChatLineView> messages{};
  std::string draft{};
};

void build_chat_ui(UiTree& tree, ChatUiModel model);

} // namespace arv::ui
