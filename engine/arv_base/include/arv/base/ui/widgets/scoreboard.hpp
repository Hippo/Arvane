#pragma once

#include "arv/ui/core/ui.hpp"
#include "arv/text/variables.hpp"

#include <optional>
#include <span>
#include <string>

namespace arv::ui {

using ScoreboardVariableMap = arv::text::TextVariableMap;

struct ScoreboardUiModel {
  bool visible{};
  std::string title{};
  std::span<const std::string> lines{};
  const ScoreboardVariableMap* variables{};
  const arv::text::TextVariableStore* variable_store{};
  std::optional<PlayerID> player_id{};
};

void build_scoreboard_ui(UiTree& tree, ScoreboardUiModel model);

} // namespace arv::ui
