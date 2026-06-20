#include "arv/base/ui.hpp"
#include "arv/client/ui_interactions.hpp"
#include "arv/text/rich_text.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <span>
#include <string_view>
#include <vector>

namespace {

class FixedTextMeasurer final : public arv::ui::IUiTextMeasurer {
public:
  [[nodiscard]] arv::Vec2f measure(
      std::string_view text,
      arv::ui::UiTextRole role = arv::ui::UiTextRole::Body) const override {
    const arv::f32 width =
        role == arv::ui::UiTextRole::Title ? 12.0F : 8.0F;
    const arv::f32 height =
        role == arv::ui::UiTextRole::Title ? 30.0F : role == arv::ui::UiTextRole::Heading ? 22.0F
                                                 : role == arv::ui::UiTextRole::Small     ? 14.0F
                                                                                          : 16.0F;
    return {static_cast<arv::f32>(text.size()) * width, height};
  }

  [[nodiscard]] arv::f32 line_height(
      arv::ui::UiTextRole role = arv::ui::UiTextRole::Body) const override {
    return role == arv::ui::UiTextRole::Title ? 30.0F : role == arv::ui::UiTextRole::Heading ? 22.0F
                                                    : role == arv::ui::UiTextRole::Small     ? 14.0F
                                                                                             : 16.0F;
  }
};

[[nodiscard]] arv::Vec2f center_of(const arv::ui::UiElement& element) noexcept {
  return {
      element.rect.x + element.rect.width * 0.5F,
      element.rect.y + element.rect.height * 0.5F};
}

} // namespace

TEST(ClientUiInteractionsTests, MountExitPromptShowsOnMountTransitionThenFades) {
  arv::client::MountExitPromptState state;
  const std::chrono::steady_clock::time_point start{};

  arv::client::update_mount_exit_prompt(state, false, start);
  EXPECT_FLOAT_EQ(arv::client::mount_exit_prompt_alpha(state, start), 0.0F);

  const auto mounted_at = start + std::chrono::milliseconds(10);
  arv::client::update_mount_exit_prompt(state, true, mounted_at);
  EXPECT_FLOAT_EQ(arv::client::mount_exit_prompt_alpha(state, mounted_at), 1.0F);
  EXPECT_FLOAT_EQ(
      arv::client::mount_exit_prompt_alpha(
          state,
          mounted_at + std::chrono::milliseconds(2400)),
      1.0F);
  const arv::f32 fading_alpha =
      arv::client::mount_exit_prompt_alpha(
          state,
          mounted_at + std::chrono::milliseconds(2750));
  EXPECT_GT(fading_alpha, 0.0F);
  EXPECT_LT(fading_alpha, 1.0F);
  EXPECT_FLOAT_EQ(
      arv::client::mount_exit_prompt_alpha(
          state,
          mounted_at + std::chrono::milliseconds(3200)),
      0.0F);

  arv::client::update_mount_exit_prompt(
      state,
      false,
      mounted_at + std::chrono::milliseconds(100));
  EXPECT_FLOAT_EQ(
      arv::client::mount_exit_prompt_alpha(
          state,
          mounted_at + std::chrono::milliseconds(100)),
      0.0F);
}

TEST(ClientUiInteractionsTests, MountExitPromptDoesNotShowForInitialMountedObservation) {
  arv::client::MountExitPromptState state;
  const std::chrono::steady_clock::time_point start{};

  arv::client::update_mount_exit_prompt(state, true, start);

  EXPECT_FLOAT_EQ(arv::client::mount_exit_prompt_alpha(state, start), 0.0F);
}

TEST(ClientUiInteractionsTests, UpdateGameplayHoverTooltipsAppendsInventoryTooltip) {
  constexpr arv::Vec2f Viewport{800.0F, 600.0F};
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  std::vector<arv::ui::UiItemStackView> slots(36);
  slots[0] = {"arvane:stone", "Stone", 3U};

  arv::ui::InventoryUiModel model;
  model.visible = true;
  model.title = "Inventory";
  model.hotbar_slot_count = 9U;
  model.viewport = Viewport;
  model.slots = std::span<const arv::ui::UiItemStackView>{slots.data(), slots.size()};
  arv::ui::build_inventory_ui(tree, model);
  tree.layout(Viewport, &text);

  const arv::ui::UiElement* slot = tree.find_by_stable_id("inventory.slot.0");
  ASSERT_NE(slot, nullptr);
  const arv::Vec2f cursor = center_of(*slot);
  arv::ui::UiDisplayMetrics metrics =
      arv::ui::UiDisplayMetrics::from_logical_size(Viewport);

  arv::client::GameplayHoverTooltipInput input;
  input.tree = &tree;
  input.inventory_open = true;
  input.inventory_slots =
      std::span<const arv::ui::UiItemStackView>{slots.data(), slots.size()};
  input.metrics = &metrics;
  input.text_measurer = &text;
  input.cursor_position = cursor;
  const arv::client::GameplayHoverTooltipResult result =
      arv::client::update_gameplay_hover_tooltips(input);

  EXPECT_TRUE(result.inventory_added);
  EXPECT_FALSE(result.container_added);
  EXPECT_FALSE(result.chat_added);
  EXPECT_TRUE(result.relaid_out);
  const arv::ui::UiElement* tooltip =
      tree.find_by_stable_id("inventory.tooltip");
  const arv::ui::UiElement* label =
      tree.find_by_stable_id("inventory.tooltip.text");
  ASSERT_NE(tooltip, nullptr);
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->text, "Stone");
  EXPECT_GT(tooltip->rect.width, 0.0F);
}

TEST(ClientUiInteractionsTests, UpdateGameplayHoverTooltipsAppendsChatTooltip) {
  constexpr arv::Vec2f Viewport{800.0F, 600.0F};
  FixedTextMeasurer text;
  arv::ui::UiTree tree;

  const arv::text::RichText rich =
      arv::text::RichTextBuilder()
          .append("Hippo")
          .hover_text("Click to message Hippo")
          .append(": hello")
          .build();
  arv::ui::ChatLineView line;
  line.text = arv::text::to_plain_text(rich);
  line.opacity = 1.0F;
  line.runs = arv::text::flatten_rich_text(rich);
  arv::ui::build_chat_ui(tree, {true, true, {&line, 1U}, ""});
  tree.layout(Viewport, &text);

  const arv::ui::UiElement* run = tree.find_by_stable_id("chat.line.0.run.0");
  ASSERT_NE(run, nullptr);
  const arv::Vec2f cursor = center_of(*run);
  arv::ui::UiDisplayMetrics metrics =
      arv::ui::UiDisplayMetrics::from_logical_size(Viewport);

  arv::client::GameplayHoverTooltipInput input;
  input.tree = &tree;
  input.chat_open = true;
  input.chat_lines = std::span<const arv::ui::ChatLineView>{&line, 1U};
  input.metrics = &metrics;
  input.text_measurer = &text;
  input.cursor_position = cursor;
  const arv::client::GameplayHoverTooltipResult result =
      arv::client::update_gameplay_hover_tooltips(input);

  EXPECT_FALSE(result.inventory_added);
  EXPECT_FALSE(result.container_added);
  EXPECT_TRUE(result.chat_added);
  EXPECT_TRUE(result.relaid_out);
  const arv::ui::UiElement* tooltip = tree.find_by_stable_id("chat.tooltip");
  const arv::ui::UiElement* label = tree.find_by_stable_id("chat.tooltip.text");
  ASSERT_NE(tooltip, nullptr);
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->text, "Click to message Hippo");
  EXPECT_GT(tooltip->rect.width, 0.0F);
}
