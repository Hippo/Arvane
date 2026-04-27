#include "arv/client/ui.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

class FixedTextMeasurer final : public arv::client::IUiTextMeasurer {
public:
  [[nodiscard]] arv::Vec2f measure(std::string_view text) const override {
    return {static_cast<arv::f32>(text.size()) * 8.0F, 16.0F};
  }

  [[nodiscard]] arv::f32 line_height() const override {
    return 16.0F;
  }
};

} // namespace

TEST(UiLayoutTests, VerticalStackArrangesChildrenDeterministically) {
  arv::client::UiTree tree;
  arv::client::UiElement stack;
  stack.stable_id = "stack";
  stack.type = arv::client::UiElementType::VerticalStack;
  stack.layout.spacing = 4.0F;
  stack.layout.padding = {2.0F, 2.0F, 3.0F, 3.0F};
  const arv::client::UiNodeID stack_id = tree.add_child(tree.root_id(), std::move(stack));

  arv::client::UiElement first;
  first.stable_id = "first";
  first.type = arv::client::UiElementType::Spacer;
  first.layout.width_mode = arv::client::UiSizeMode::Fixed;
  first.layout.height_mode = arv::client::UiSizeMode::Fixed;
  first.layout.width = 20.0F;
  first.layout.height = 10.0F;
  tree.add_child(stack_id, std::move(first));

  arv::client::UiElement second;
  second.stable_id = "second";
  second.type = arv::client::UiElementType::Spacer;
  second.layout.width_mode = arv::client::UiSizeMode::Fixed;
  second.layout.height_mode = arv::client::UiSizeMode::Fixed;
  second.layout.width = 12.0F;
  second.layout.height = 8.0F;
  tree.add_child(stack_id, std::move(second));

  tree.layout({200.0F, 100.0F}, nullptr);

  const auto* stack_element = tree.find_by_stable_id("stack");
  const auto* first_element = tree.find_by_stable_id("first");
  const auto* second_element = tree.find_by_stable_id("second");
  ASSERT_NE(stack_element, nullptr);
  ASSERT_NE(first_element, nullptr);
  ASSERT_NE(second_element, nullptr);
  EXPECT_FLOAT_EQ(stack_element->rect.width, 24.0F);
  EXPECT_FLOAT_EQ(stack_element->rect.height, 28.0F);
  EXPECT_FLOAT_EQ(first_element->rect.x, 2.0F);
  EXPECT_FLOAT_EQ(first_element->rect.y, 3.0F);
  EXPECT_FLOAT_EQ(second_element->rect.y, 17.0F);
}

TEST(UiInputTests, MouseRoutingSeparatesHoverActiveAndKeyboardFocus) {
  arv::client::UiTree tree;
  arv::client::UiElement button;
  button.stable_id = "button";
  button.type = arv::client::UiElementType::Button;
  button.layout.width_mode = arv::client::UiSizeMode::Fixed;
  button.layout.height_mode = arv::client::UiSizeMode::Fixed;
  button.layout.width = 80.0F;
  button.layout.height = 32.0F;
  button.focusable = true;
  button.action = "press";
  tree.add_child(tree.root_id(), std::move(button));
  tree.layout({200.0F, 100.0F}, nullptr);

  auto hover = tree.route_mouse_move({12.0F, 10.0F});
  ASSERT_TRUE(hover.has_value());
  EXPECT_NE(tree.hovered().value, 0U);
  EXPECT_EQ(tree.active().value, 0U);
  EXPECT_EQ(tree.keyboard_focus().value, 0U);

  auto down = tree.route_mouse_down({12.0F, 10.0F}, arv::client::UiMouseButton::Left);
  ASSERT_TRUE(down.has_value());
  EXPECT_NE(tree.active().value, 0U);
  EXPECT_NE(tree.keyboard_focus().value, 0U);
}

TEST(UiDocumentTests, RejectsUnsafePluginDocumentShapes) {
  arv::client::UiDocument document;
  document.version = 1;
  document.screen_id = "shop";
  document.owner = "plugin:test";
  document.root = {"root", "root", "", "", {{"unknown_widget", "x", "", "", {}}}};

  const arv::client::UiDocumentValidation validation = arv::client::validate_ui_document(document);
  EXPECT_FALSE(validation.accepted);
  ASSERT_FALSE(validation.errors.empty());
}

TEST(HotbarHudTests, BuildsSelectedSlotAsRetainedUi) {
  FixedTextMeasurer text;
  arv::client::UiTree tree;
  arv::client::build_hotbar_hud(tree, {2, 9, {800.0F, 600.0F}});
  tree.layout({800.0F, 600.0F}, &text);

  const auto* selected = tree.find_by_stable_id("hud.hotbar.slot.2");
  ASSERT_NE(selected, nullptr);
  EXPECT_TRUE(selected->selected);
  EXPECT_GT(tree.elements().size(), 9U);
}

TEST(HotbarHudTests, RebuildClearsPreviousSelectedSlot) {
  FixedTextMeasurer text;
  arv::client::UiTree tree;

  arv::client::build_hotbar_hud(tree, {1, 9, {800.0F, 600.0F}});
  tree.layout({800.0F, 600.0F}, &text);
  ASSERT_TRUE(tree.find_by_stable_id("hud.hotbar.slot.1")->selected);

  arv::client::build_hotbar_hud(tree, {4, 9, {800.0F, 600.0F}});
  tree.layout({800.0F, 600.0F}, &text);

  const auto* old_slot = tree.find_by_stable_id("hud.hotbar.slot.1");
  const auto* new_slot = tree.find_by_stable_id("hud.hotbar.slot.4");
  ASSERT_NE(old_slot, nullptr);
  ASSERT_NE(new_slot, nullptr);
  EXPECT_FALSE(old_slot->selected);
  EXPECT_TRUE(new_slot->selected);
}

TEST(GameplayUiTests, BuildsInventoryChatTabAndScoreboardSurfaces) {
  FixedTextMeasurer text;
  arv::client::UiTree tree;

  std::vector<arv::client::UiItemStackView> slots(36);
  slots[0] = {"arvane:stone", "Stone", 32};
  std::vector<arv::client::ChatLineView> chat{{"hello", 1.0F}};
  std::vector<arv::client::PlayerListEntryView> players{{"local_player", 12}};
  std::vector<std::string> scoreboard_lines{"Health: {player_health}", ""};
  arv::client::ScoreboardVariableMap vars{{"player_health", "20"}};

  arv::client::build_gameplay_ui(
      tree,
      {
          {0, 9, {800.0F, 600.0F}, {slots.data(), 9}},
          {true, "Inventory", 0, 9, {800.0F, 600.0F}, {slots.data(), slots.size()}},
          {true, true, {chat.data(), chat.size()}, "test"},
          {true, "Players", "footer", {players.data(), players.size()}},
          {true, "Stats", {scoreboard_lines.data(), scoreboard_lines.size()}, &vars},
      });
  tree.layout({800.0F, 600.0F}, &text);

  ASSERT_NE(tree.find_by_stable_id("inventory.panel"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("chat.input"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("tab.panel"), nullptr);
  const auto* scoreboard_line = tree.find_by_stable_id("scoreboard.line.0");
  ASSERT_NE(scoreboard_line, nullptr);
  EXPECT_EQ(scoreboard_line->text, "Health: 20");
}
