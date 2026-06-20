#pragma once

#include "arv/asset/font.hpp"
#include "arv/base/ui/widgets/gameplay.hpp"
#include "arv/client/client_ui_assets.hpp"
#include "arv/client/ui_interactions.hpp"
#include "arv/core/types.hpp"
#include "arv/render/render_types.hpp"
#include "arv/ui/core/ui.hpp"

#include <optional>
#include <span>

namespace arv::client {

struct GameplayUiTreeBuildInput {
  ui::UiTree* tree{};
  const ui::UiTheme* theme{};
  ui::GameplayUiModel model{};
  FunctionRef<void()> append_active_screens{};
  const ui::UiDisplayMetrics* metrics{};
  const ui::IUiTextMeasurer* text_measurer{};
};

struct GameplayUiFrameHoverInput {
  bool inventory_open{};
  bool container_open{};
  bool chat_open{};
  std::optional<ui::UiItemStackView> carried_stack{};
  std::span<const ui::UiItemStackView> inventory_slots{};
  std::span<const ui::UiItemStackView> container_slots{};
  std::span<const ui::ChatLineView> chat_lines{};
};

struct GameplayUiFrameAppendInput {
  ui::UiTree* tree{};
  const ui::UiDisplayMetrics* metrics{};
  const ui::IUiTextMeasurer* text_measurer{};
  Vec2f cursor_position{};
  GameplayUiFrameHoverInput hover{};
  FunctionRef<void()> before_draw_append{};
  asset::FontSystem* font_system{};
  const ui::UiFontStacks* font_stacks{};
  render::FramePacket* frame{};
  f32 time_seconds{};
  FunctionRef<bool()> sync_font_atlas{};
};

struct GameplayUiTreeBuildResult {
  bool built{};
  bool active_screens_appended{};
  bool laid_out{};
};

struct GameplayUiFrameAppendResult {
  GameplayHoverTooltipResult hover_tooltips{};
  UiDrawCommandAppendResult draw_commands{};
  bool before_draw_append_called{};
};

[[nodiscard]] GameplayUiTreeBuildResult build_gameplay_ui_tree(
    GameplayUiTreeBuildInput input);

[[nodiscard]] GameplayUiFrameAppendResult append_gameplay_ui_frame(
    GameplayUiFrameAppendInput input);

} // namespace arv::client
