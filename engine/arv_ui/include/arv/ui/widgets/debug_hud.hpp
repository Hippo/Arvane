#pragma once

#include "arv/ui/core/ui.hpp"

namespace arv::ui {

struct DebugHudModel {
  bool show_fps{};
  u32 fps{};
};

void build_debug_hud(UiTree& tree, DebugHudModel model);

} // namespace arv::ui
