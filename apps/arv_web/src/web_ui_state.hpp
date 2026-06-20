#pragma once

#include "arv/asset/font.hpp"
#include "arv/base/ui/screens/screen.hpp"
#include "arv/base/ui/widgets/chat.hpp"
#include "arv/client/ui_interactions.hpp"
#include "arv/core/types.hpp"
#include "arv/text/language.hpp"
#include "arv/ui/core/ui.hpp"

#include <optional>
#include <vector>

namespace arv::web {

struct WebUiState {
  ui::UiTree tree{};
  asset::FontSystem fonts{};
  ui::UiFontStacks font_stacks{};
  std::optional<ui::FontUiTextMeasurer> text_measurer{};
  ui::UiTheme theme{ui::make_default_ui_theme()};
  text::LanguageCatalog language{};
  ui::UIScreenManager screen_manager{};
  std::vector<ui::ChatLineView> chat_lines{};
  usize chat_history_scroll_offset{};
  client::HotbarItemPopupState hotbar_item_popup{};
  f32 font_framebuffer_scale{1.0F};
  bool ready{};
};

} // namespace arv::web
