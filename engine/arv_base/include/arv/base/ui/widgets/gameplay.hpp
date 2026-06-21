#pragma once

#include "arv/base/ui/widgets/chat.hpp"
#include "arv/base/ui/widgets/debug_hud.hpp"
#include "arv/base/ui/widgets/health.hpp"
#include "arv/base/ui/widgets/hotbar.hpp"
#include "arv/base/ui/widgets/inventory.hpp"
#include "arv/base/ui/widgets/scoreboard.hpp"
#include "arv/base/ui/widgets/tab_menu.hpp"

#include <string>

namespace arv::ui {

struct RidePromptModel {
  bool visible{};
  std::string key_label{};
  std::string suffix{" to Ride"};
  f32 alpha{1.0F};
  Vec2f viewport{};
};

struct CrosshairHudModel {
  bool visible{};
  Vec2f viewport{};
  UiImageData image{};
  f32 size{24.0F};
};

struct GameplayUiModel {
  HotbarHudModel hotbar{};
  InventoryUiModel inventory{};
  PlayerMenuUiModel player_menu{};
  ChatUiModel chat{};
  TabMenuUiModel tab_menu{};
  ScoreboardUiModel scoreboard{};
  DebugHudModel debug{};
  HealthHudModel health{};
  RidePromptModel ride_prompt{};
  ContainerUiModel container{};
  CrosshairHudModel crosshair{};
};

void build_gameplay_ui(UiTree& tree, GameplayUiModel model);

} // namespace arv::ui
