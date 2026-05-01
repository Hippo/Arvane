#include "arv/ui/widgets/scoreboard.hpp"

#include "widget_common.hpp"

#include <string>
#include <string_view>

namespace arv::ui {
namespace {

[[nodiscard]] std::string interpolate_scoreboard_line(
    std::string_view line,
    const ScoreboardVariableMap* variables) {
  std::string output;
  output.reserve(line.size());
  for (usize index = 0; index < line.size();) {
    if (line[index] != '{') {
      output.push_back(line[index++]);
      continue;
    }

    const usize close = line.find('}', index + 1U);
    if (close == std::string_view::npos) {
      output.push_back(line[index++]);
      continue;
    }

    const std::string key{line.substr(index + 1U, close - index - 1U)};
    if (variables != nullptr) {
      const auto found = variables->find(key);
      if (found != variables->end()) {
        output += found->second;
      } else {
        output += "{" + key + "}";
      }
    } else {
      output += "{" + key + "}";
    }
    index = close + 1U;
  }
  return output;
}

} // namespace

namespace widgets_detail {

void append_scoreboard_ui(UiTree& tree, ScoreboardUiModel model) {
  if (!model.visible || (model.lines.empty() && model.title.empty())) {
    return;
  }

  const f32 rows = static_cast<f32>(model.lines.size() + (model.title.empty() ? 0U : 1U));
  UiElement panel = fixed_panel(
      "scoreboard.panel",
      260.0F,
      22.0F + rows * 22.0F,
      {0.035F, 0.04F, 0.045F, 0.76F});
  panel.layout.anchor = UiAnchor::TopRight;
  panel.layout.offset = {-20.0F, 72.0F};
  panel.layout.padding = {10.0F, 10.0F, 8.0F, 8.0F};
  panel.hit_test = false;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement stack;
  stack.stable_id = "scoreboard.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = 4.0F;
  stack.hit_test = false;
  const UiNodeID stack_id = tree.add_child(panel_id, std::move(stack));

  if (!model.title.empty()) {
    tree.add_child(stack_id, text_element("scoreboard.title", model.title));
  }
  for (usize index = 0; index < model.lines.size(); ++index) {
    tree.add_child(
        stack_id,
        text_element(
            "scoreboard.line." + std::to_string(index),
            interpolate_scoreboard_line(model.lines[index], model.variables),
            {0.86F, 0.89F, 0.92F, 1.0F}));
  }
}

} // namespace widgets_detail

void build_scoreboard_ui(UiTree& tree, ScoreboardUiModel model) {
  tree.clear();
  widgets_detail::append_scoreboard_ui(tree, model);
}

} // namespace arv::ui
