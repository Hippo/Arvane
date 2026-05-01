#include "arv/ui/widgets/debug_hud.hpp"

#include "widget_common.hpp"

#include <string>

namespace arv::ui {
namespace widgets_detail {

void append_debug_hud(UiTree& tree, DebugHudModel model) {
  if (!model.show_fps) {
    return;
  }

  UiElement fps = text_element(
      "hud.debug.fps",
      "FPS: " + std::to_string(model.fps),
      {0.86F, 0.94F, 0.78F, 1.0F});
  fps.layout.anchor = UiAnchor::TopLeft;
  fps.layout.offset = {8.0F, 8.0F};
  tree.add_child(tree.root_id(), std::move(fps));
}

} // namespace widgets_detail

void build_debug_hud(UiTree& tree, DebugHudModel model) {
  tree.clear();
  widgets_detail::append_debug_hud(tree, model);
}

} // namespace arv::ui
