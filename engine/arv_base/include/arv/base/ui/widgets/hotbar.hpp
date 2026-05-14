#pragma once

#include "arv/player/player_constants.hpp"
#include "arv/base/ui/widgets/items.hpp"

#include <span>
#include <string>

namespace arv::ui {

struct HotbarHudModel {
  u8 selected_slot{};
  u8 slot_count{player::DefaultHotbarSlotCount};
  Vec2f viewport{};
  std::span<const UiItemStackView> slots{};
  std::string item_name_popup{};
  f32 item_name_popup_alpha{};
};

void build_hotbar_hud(UiTree& tree, HotbarHudModel model);

} // namespace arv::ui
