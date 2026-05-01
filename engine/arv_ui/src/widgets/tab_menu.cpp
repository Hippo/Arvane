#include "arv/ui/widgets/tab_menu.hpp"

#include "widget_common.hpp"

#include <algorithm>
#include <string>

namespace arv::ui {
namespace widgets_detail {

void append_tab_menu_ui(UiTree& tree, TabMenuUiModel model) {
  if (!model.visible) {
    return;
  }

  const f32 rows = static_cast<f32>(std::max<usize>(1U, model.players.size()));
  UiElement panel = fixed_panel(
      "tab.panel",
      360.0F,
      60.0F + rows * 24.0F + (model.footer.empty() ? 0.0F : 24.0F),
      {0.03F, 0.034F, 0.04F, 0.88F});
  panel.layout.anchor = UiAnchor::TopCenter;
  panel.layout.offset = {0.0F, 24.0F};
  panel.layout.padding = {12.0F, 12.0F, 10.0F, 10.0F};
  panel.hit_test = false;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement stack;
  stack.stable_id = "tab.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = 6.0F;
  stack.hit_test = false;
  const UiNodeID stack_id = tree.add_child(panel_id, std::move(stack));

  if (!model.header.empty()) {
    tree.add_child(stack_id, text_element("tab.header", model.header));
  }
  for (usize index = 0; index < model.players.size(); ++index) {
    const PlayerListEntryView& player = model.players[index];
    std::string text = player.name;
    if (player.latency_ms.has_value()) {
      text += "  " + std::to_string(player.latency_ms.value()) + "ms";
    }
    tree.add_child(
        stack_id,
        text_element("tab.player." + std::to_string(index), std::move(text)));
  }
  if (!model.footer.empty()) {
    tree.add_child(
        stack_id,
        text_element("tab.footer", model.footer, {0.78F, 0.82F, 0.86F, 1.0F}));
  }
}

} // namespace widgets_detail

void build_tab_menu_ui(UiTree& tree, TabMenuUiModel model) {
  tree.clear();
  widgets_detail::append_tab_menu_ui(tree, model);
}

} // namespace arv::ui
