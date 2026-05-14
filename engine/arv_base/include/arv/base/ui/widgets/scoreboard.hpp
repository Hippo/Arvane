#pragma once

#include "arv/ui/core/ui.hpp"

#include <span>
#include <string>

namespace arv::ui {

using ScoreboardVariableMap = HashMap<std::string, std::string>;

struct ScoreboardUiModel {
  bool visible{};
  std::string title{};
  std::span<const std::string> lines{};
  const ScoreboardVariableMap* variables{};
};

void build_scoreboard_ui(UiTree& tree, ScoreboardUiModel model);

} // namespace arv::ui
