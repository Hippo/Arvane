#pragma once

#include "arv/player/player_constants.hpp"
#include "arv/ui/widgets/items.hpp"

#include <span>

namespace arv::ui {

struct HotbarHudModel {
  u8 selected_slot{};
  u8 slot_count{player::DefaultHotbarSlotCount};
  Vec2f viewport{};
  std::span<const UiItemStackView> slots{};
};

void build_hotbar_hud(UiTree& tree, HotbarHudModel model);

} // namespace arv::ui
