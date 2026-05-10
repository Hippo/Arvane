#pragma once

#include "arv/ui/widgets/chat.hpp"
#include "arv/ui/widgets/debug_hud.hpp"
#include "arv/ui/widgets/health.hpp"
#include "arv/ui/widgets/hotbar.hpp"
#include "arv/ui/widgets/inventory.hpp"
#include "arv/ui/widgets/scoreboard.hpp"
#include "arv/ui/widgets/tab_menu.hpp"

namespace arv::ui::widgets_detail {

void append_hotbar_hud(UiTree& tree, HotbarHudModel model);
void append_health_hud(UiTree& tree, HealthHudModel model);
void append_inventory_ui(UiTree& tree, InventoryUiModel model);
void append_chat_ui(UiTree& tree, ChatUiModel model);
void append_tab_menu_ui(UiTree& tree, TabMenuUiModel model);
void append_scoreboard_ui(UiTree& tree, ScoreboardUiModel model);
void append_debug_hud(UiTree& tree, DebugHudModel model);

} // namespace arv::ui::widgets_detail
