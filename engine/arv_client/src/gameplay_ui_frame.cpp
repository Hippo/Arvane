#include "arv/client/gameplay_ui_frame.hpp"

#include "arv/base/ui/widgets/gameplay.hpp"

#include <utility>

namespace arv::client {

GameplayUiTreeBuildResult build_gameplay_ui_tree(
    GameplayUiTreeBuildInput input) {
  GameplayUiTreeBuildResult result;
  if (input.tree == nullptr || input.metrics == nullptr) {
    return result;
  }

  input.tree->set_theme(input.theme);
  ui::build_gameplay_ui(*input.tree, std::move(input.model));
  result.built = true;

  if (input.append_active_screens) {
    input.append_active_screens();
    result.active_screens_appended = true;
  }

  input.tree->layout_with_metrics(*input.metrics, input.text_measurer);
  result.laid_out = true;
  return result;
}

GameplayUiFrameAppendResult append_gameplay_ui_frame(
    GameplayUiFrameAppendInput input) {
  GameplayUiFrameAppendResult result;
  if (input.tree == nullptr ||
      input.metrics == nullptr ||
      input.font_system == nullptr ||
      input.font_stacks == nullptr ||
      input.frame == nullptr) {
    return result;
  }

  result.hover_tooltips = update_gameplay_hover_tooltips(
      {
          .tree = input.tree,
          .inventory_open = input.hover.inventory_open,
          .container_open = input.hover.container_open,
          .chat_open = input.hover.chat_open,
          .carried_stack = input.hover.carried_stack,
          .inventory_slots = input.hover.inventory_slots,
          .container_slots = input.hover.container_slots,
          .chat_lines = input.hover.chat_lines,
          .metrics = input.metrics,
          .text_measurer = input.text_measurer,
          .cursor_position = input.cursor_position,
      });

  if (input.before_draw_append) {
    input.before_draw_append();
    result.before_draw_append_called = true;
  }

  result.draw_commands = append_ui_draw_commands_with_font_sync(
      *input.tree,
      *input.font_system,
      *input.font_stacks,
      *input.frame,
      *input.metrics,
      input.time_seconds,
      input.sync_font_atlas);
  return result;
}

} // namespace arv::client
