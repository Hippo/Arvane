#include "arv/base/ui/widgets/gameplay.hpp"

#include "widget_builders.hpp"

namespace arv::ui {

void build_gameplay_ui(UiTree& tree, GameplayUiModel model) {
  tree.clear();
  widgets_detail::append_health_hud(tree, model.health);
  widgets_detail::append_hotbar_hud(tree, model.hotbar);
  widgets_detail::append_debug_hud(tree, model.debug);
  widgets_detail::append_scoreboard_ui(tree, model.scoreboard);
  widgets_detail::append_chat_ui(tree, model.chat);
  widgets_detail::append_tab_menu_ui(tree, model.tab_menu);
  widgets_detail::append_inventory_ui(tree, model.inventory);
}

} // namespace arv::ui
