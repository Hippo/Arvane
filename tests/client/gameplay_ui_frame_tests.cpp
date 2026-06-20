#include "arv/client/gameplay_ui_frame.hpp"

#include "arv/base/ui/widgets/gameplay.hpp"
#include "arv/ui/core/ui.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

namespace {

class FixedTextMeasurer final : public arv::ui::IUiTextMeasurer {
public:
  [[nodiscard]] arv::Vec2f measure(
      std::string_view text,
      arv::ui::UiTextRole role = arv::ui::UiTextRole::Body) const override {
    const arv::f32 height = line_height(role);
    return {static_cast<arv::f32>(text.size()) * height * 0.5F, height};
  }

  [[nodiscard]] arv::f32 line_height(
      arv::ui::UiTextRole role = arv::ui::UiTextRole::Body) const override {
    switch (role) {
    case arv::ui::UiTextRole::Small:
      return 12.0F;
    case arv::ui::UiTextRole::Title:
      return 24.0F;
    case arv::ui::UiTextRole::Heading:
      return 20.0F;
    case arv::ui::UiTextRole::Mono:
      return 16.0F;
    case arv::ui::UiTextRole::Body:
    default:
      return 16.0F;
    }
  }
};

[[nodiscard]] arv::ui::UiDisplayMetrics test_metrics() {
  return arv::ui::UiDisplayMetrics::from_logical_size({800.0F, 600.0F});
}

void add_centered_slot(arv::ui::UiTree& tree) {
  arv::ui::UiElement slot;
  slot.stable_id = "inventory.slot.0";
  slot.action = "inventory_slot";
  slot.type = arv::ui::UiElementType::Slot;
  slot.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  slot.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  slot.layout.width = 48.0F;
  slot.layout.height = 48.0F;
  slot.layout.anchor = arv::ui::UiAnchor::Center;
  slot.style.background = {0.1F, 0.1F, 0.1F, 1.0F};
  tree.add_child(tree.root_id(), std::move(slot));
}

} // namespace

TEST(ClientGameplayUiFrameTests, BuildsGameplayTreeAppendsScreensAndLayouts) {
  arv::ui::UiTree tree;
  const arv::ui::UiTheme theme = arv::ui::make_default_ui_theme();
  const arv::ui::UiDisplayMetrics metrics = test_metrics();
  FixedTextMeasurer text;

  arv::ui::GameplayUiModel model;
  model.crosshair.visible = true;
  model.crosshair.viewport = metrics.logical_size;
  model.crosshair.size = 24.0F;
  model.crosshair.image.valid = true;

  bool appended_screen = false;
  const auto append_screen = [&]() {
    appended_screen = true;
    arv::ui::UiElement panel;
    panel.stable_id = "test.screen";
    panel.type = arv::ui::UiElementType::Panel;
    panel.layout.width_mode = arv::ui::UiSizeMode::Fixed;
    panel.layout.height_mode = arv::ui::UiSizeMode::Fixed;
    panel.layout.width = 64.0F;
    panel.layout.height = 32.0F;
    panel.layout.anchor = arv::ui::UiAnchor::Center;
    panel.style.background = {0.2F, 0.3F, 0.4F, 1.0F};
    tree.add_child(tree.root_id(), std::move(panel));
  };

  const arv::client::GameplayUiTreeBuildResult result =
      arv::client::build_gameplay_ui_tree(
          {
              .tree = &tree,
              .theme = &theme,
              .model = std::move(model),
              .append_active_screens = append_screen,
              .metrics = &metrics,
              .text_measurer = &text,
          });

  EXPECT_TRUE(result.built);
  EXPECT_TRUE(result.active_screens_appended);
  EXPECT_TRUE(result.laid_out);
  EXPECT_TRUE(appended_screen);
  ASSERT_NE(tree.find_by_stable_id("hud.crosshair"), nullptr);
  const arv::ui::UiElement* panel = tree.find_by_stable_id("test.screen");
  ASSERT_NE(panel, nullptr);
  EXPECT_FLOAT_EQ(panel->rect.width, 64.0F);
  EXPECT_FLOAT_EQ(panel->rect.height, 32.0F);
}

TEST(ClientGameplayUiFrameTests, AppendsHoverTooltipsBeforeDrawCommands) {
  arv::ui::UiTree tree;
  const arv::ui::UiTheme theme = arv::ui::make_default_ui_theme();
  const arv::ui::UiDisplayMetrics metrics = test_metrics();
  FixedTextMeasurer text;
  tree.set_theme(&theme);
  add_centered_slot(tree);
  tree.layout_with_metrics(metrics, &text);

  std::vector<arv::ui::UiItemStackView> slots(1);
  slots[0].item_name = "arvane:stone";
  slots[0].display_name = "Stone";
  slots[0].count = 1;

  arv::asset::FontSystem fonts;
  arv::ui::UiFontStacks font_stacks;
  arv::render::FramePacket frame;
  bool before_draw_called = false;
  int sync_calls = 0;

  const arv::client::GameplayUiFrameAppendResult result =
      arv::client::append_gameplay_ui_frame(
          {
              .tree = &tree,
              .metrics = &metrics,
              .text_measurer = &text,
              .cursor_position = {400.0F, 300.0F},
              .hover = {
                  .inventory_open = true,
                  .inventory_slots =
                      std::span<const arv::ui::UiItemStackView>{
                          slots.data(),
                          slots.size()},
              },
              .before_draw_append =
                  [&]() {
                    before_draw_called = true;
                    EXPECT_NE(tree.find_by_stable_id("inventory.tooltip"), nullptr);
                  },
              .font_system = &fonts,
              .font_stacks = &font_stacks,
              .frame = &frame,
              .time_seconds = 1.0F,
              .sync_font_atlas =
                  [&]() -> bool {
                    ++sync_calls;
                    return true;
                  },
          });

  EXPECT_TRUE(result.hover_tooltips.inventory_added);
  EXPECT_TRUE(result.hover_tooltips.relaid_out);
  EXPECT_TRUE(result.before_draw_append_called);
  EXPECT_TRUE(before_draw_called);
  EXPECT_EQ(sync_calls, 1);
  EXPECT_FALSE(result.draw_commands.rolled_back);
  EXPECT_GT(result.draw_commands.ui_rects_appended, 0U);
  EXPECT_NE(tree.find_by_stable_id("inventory.tooltip"), nullptr);
}

TEST(ClientGameplayUiFrameTests, RollsBackDrawCommandsWhenFontSyncFails) {
  arv::ui::UiTree tree;
  const arv::ui::UiTheme theme = arv::ui::make_default_ui_theme();
  const arv::ui::UiDisplayMetrics metrics = test_metrics();
  tree.set_theme(&theme);
  add_centered_slot(tree);
  tree.layout_with_metrics(metrics, nullptr);

  arv::asset::FontSystem fonts;
  arv::ui::UiFontStacks font_stacks;
  arv::render::FramePacket frame;
  frame.ui_rects.push_back({});
  bool before_draw_called = false;

  const arv::client::GameplayUiFrameAppendResult result =
      arv::client::append_gameplay_ui_frame(
          {
              .tree = &tree,
              .metrics = &metrics,
              .cursor_position = {10.0F, 10.0F},
              .before_draw_append =
                  [&]() {
                    before_draw_called = true;
                  },
              .font_system = &fonts,
              .font_stacks = &font_stacks,
              .frame = &frame,
              .time_seconds = 1.0F,
              .sync_font_atlas = []() -> bool {
                return false;
              },
          });

  EXPECT_TRUE(result.before_draw_append_called);
  EXPECT_TRUE(before_draw_called);
  EXPECT_TRUE(result.draw_commands.rolled_back);
  EXPECT_EQ(frame.ui_rects.size(), 1U);
}
