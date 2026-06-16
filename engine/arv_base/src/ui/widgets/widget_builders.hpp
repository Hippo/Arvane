#pragma once

#include "arv/base/ui/widgets/chat.hpp"
#include "arv/base/ui/widgets/debug_hud.hpp"
#include "arv/base/ui/widgets/health.hpp"
#include "arv/base/ui/widgets/hotbar.hpp"
#include "arv/base/ui/widgets/inventory.hpp"
#include "arv/base/ui/widgets/scoreboard.hpp"
#include "arv/base/ui/widgets/tab_menu.hpp"

namespace arv::ui::widgets_detail {

void append_hotbar_hud(UiTree& tree, HotbarHudModel model);
void append_health_hud(UiTree& tree, HealthHudModel model);
void append_inventory_ui(UiTree& tree, InventoryUiModel model);
void append_container_ui(UiTree& tree, ContainerUiModel model);
void append_chat_ui(UiTree& tree, ChatUiModel model);
void append_tab_menu_ui(UiTree& tree, TabMenuUiModel model);
void append_scoreboard_ui(UiTree& tree, ScoreboardUiModel model);
void append_debug_hud(UiTree& tree, DebugHudModel model);
void append_ride_prompt_hud(UiTree& tree, RidePromptModel model);

} // namespace arv::ui::widgets_detail
