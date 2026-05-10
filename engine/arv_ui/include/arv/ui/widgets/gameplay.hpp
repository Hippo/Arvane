#pragma once

#include "arv/ui/widgets/chat.hpp"
#include "arv/ui/widgets/debug_hud.hpp"
#include "arv/ui/widgets/health.hpp"
#include "arv/ui/widgets/hotbar.hpp"
#include "arv/ui/widgets/inventory.hpp"
#include "arv/ui/widgets/scoreboard.hpp"
#include "arv/ui/widgets/tab_menu.hpp"

namespace arv::ui {

struct GameplayUiModel {
  HotbarHudModel hotbar{};
  InventoryUiModel inventory{};
  ChatUiModel chat{};
  TabMenuUiModel tab_menu{};
  ScoreboardUiModel scoreboard{};
  DebugHudModel debug{};
  HealthHudModel health{};
};

void build_gameplay_ui(UiTree& tree, GameplayUiModel model);

} // namespace arv::ui
