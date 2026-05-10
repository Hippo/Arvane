#pragma once

#include "arv/ui/core/ui.hpp"

namespace arv::ui {

struct HealthHudModel {
  bool visible{};
  f32 current{};
  f32 max{};
  f32 section_value{1.0F};
  Vec2f viewport{};
};

void build_health_hud(UiTree& tree, HealthHudModel model);

} // namespace arv::ui
