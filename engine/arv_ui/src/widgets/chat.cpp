#include "arv/ui/widgets/chat.hpp"

#include "widget_common.hpp"

#include <algorithm>
#include <string>

namespace arv::ui {
namespace widgets_detail {

void append_chat_ui(UiTree& tree, ChatUiModel model) {
  if (!model.visible && !model.input_active) {
    return;
  }

  UiElement stack;
  stack.stable_id = "chat.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.width_mode = UiSizeMode::Fixed;
  stack.layout.width = 420.0F;
  stack.layout.spacing = 4.0F;
  stack.layout.padding = {8.0F, 8.0F, 8.0F, 8.0F};
  stack.layout.anchor = UiAnchor::BottomLeft;
  stack.layout.offset = {20.0F, -96.0F};
  stack.style.background = model.input_active ? Vec4f{0.02F, 0.024F, 0.03F, 0.52F}
                                              : Vec4f{0.0F, 0.0F, 0.0F, 0.0F};
  stack.hit_test = model.input_active;
  const UiNodeID stack_id = tree.add_child(tree.root_id(), std::move(stack));

  const usize start = model.messages.size() > 8U ? model.messages.size() - 8U : 0U;
  for (usize index = start; index < model.messages.size(); ++index) {
    const ChatLineView& line = model.messages[index];
    tree.add_child(
        stack_id,
        text_element(
            "chat.line." + std::to_string(index),
            line.text,
            {0.92F, 0.94F, 0.96F, std::clamp(line.opacity, 0.0F, 1.0F)}));
  }

  if (model.input_active) {
    UiElement input = fixed_panel(
        "chat.input",
        404.0F,
        30.0F,
        {0.04F, 0.045F, 0.052F, 0.92F});
    input.layout.padding = {8.0F, 8.0F, 6.0F, 6.0F};
    input.focusable = true;
    input.action = "chat_input";
    const UiNodeID input_id = tree.add_child(stack_id, std::move(input));
    tree.add_child(
        input_id,
        text_element("chat.input.text", "> " + model.draft, {0.96F, 0.97F, 0.98F, 1.0F}));
  }
}

} // namespace widgets_detail

void build_chat_ui(UiTree& tree, ChatUiModel model) {
  tree.clear();
  widgets_detail::append_chat_ui(tree, model);
}

} // namespace arv::ui
