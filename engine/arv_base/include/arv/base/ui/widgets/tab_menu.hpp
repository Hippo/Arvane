#pragma once

#include "arv/ui/core/ui.hpp"

#include <optional>
#include <span>
#include <string>

namespace arv::ui {

struct PlayerListEntryView {
  std::string name{};
  std::optional<u32> latency_ms{};
};

struct TabMenuUiModel {
  bool visible{};
  std::string header{};
  std::string footer{};
  std::span<const PlayerListEntryView> players{};
};

void build_tab_menu_ui(UiTree& tree, TabMenuUiModel model);

} // namespace arv::ui
