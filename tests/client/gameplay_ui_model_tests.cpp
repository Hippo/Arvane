#include "arv/client/gameplay_ui_model.hpp"
#include "arv/player/player_constants.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] arv::ui::UiItemStackView stack_view(
    std::string item_name,
    std::string display_name,
    arv::u16 count) {
  arv::ui::UiItemStackView stack;
  stack.item_name = std::move(item_name);
  stack.display_name = std::move(display_name);
  stack.count = count;
  return stack;
}

[[nodiscard]] arv::client::ClientRuntime make_client() {
  return arv::client::ClientRuntime{std::unique_ptr<arv::net::ITransport>{}};
}

} // namespace

TEST(GameplayUiModelProjectionTests, PassiveChatProjectionKeepsLatestVisibleLines) {
  arv::client::ClientRuntime client = make_client();
  client.add_chat_message("one");
  client.add_chat_message("two");

  arv::io::UiConfig::ChatConfig chat_config;
  chat_config.passive_max_lines = 1U;
  std::vector<arv::ui::ChatLineView> chat_lines;
  arv::usize history_scroll_offset = 12U;

  arv::client::refresh_gameplay_chat_lines(
      chat_lines,
      client,
      chat_config,
      std::chrono::steady_clock::now(),
      history_scroll_offset);

  ASSERT_EQ(chat_lines.size(), 1U);
  EXPECT_EQ(chat_lines.front().text, "two");
  EXPECT_FLOAT_EQ(chat_lines.front().opacity, 1.0F);
  EXPECT_EQ(history_scroll_offset, 0U);

  const arv::ui::GameplayUiModel model =
      arv::client::build_gameplay_ui_model(
          {
              .client = client,
              .chat_config = chat_config,
              .chat_lines = std::span<const arv::ui::ChatLineView>{
                  chat_lines.data(),
                  chat_lines.size()},
              .viewport = {1280.0F, 720.0F},
          });

  EXPECT_FALSE(model.chat.input_active);
  EXPECT_EQ(model.chat.max_visible_messages, 1U);
  ASSERT_EQ(model.chat.messages.size(), 1U);
  EXPECT_EQ(model.chat.messages.front().text, "two");
}

TEST(GameplayUiModelProjectionTests, BuildsRuntimeBackedOpenChatAndInventoryModel) {
  arv::client::ClientRuntime client = make_client();
  client.add_chat_message("hello");
  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = "draft"});

  arv::io::UiConfig::ChatConfig chat_config;
  std::vector<arv::ui::ChatLineView> chat_lines;
  arv::usize history_scroll_offset = 3U;
  arv::client::refresh_gameplay_chat_lines(
      chat_lines,
      client,
      chat_config,
      std::chrono::steady_clock::now(),
      history_scroll_offset);

  std::vector<arv::ui::UiItemStackView> slots(12);
  slots[0] = stack_view("arvane:stone", "Stone", 6U);
  const std::optional<arv::ui::UiItemStackView> carried =
      stack_view("arvane:stick", "Stick", 1U);
  std::vector<arv::ui::PlayerListEntryView> players{{"Alex", 42U}};
  arv::ui::RidePromptModel ride_prompt;
  ride_prompt.visible = true;
  ride_prompt.key_label = "R";
  ride_prompt.viewport = {1280.0F, 720.0F};

  const arv::ui::GameplayUiModel model =
      arv::client::build_gameplay_ui_model(
          {
              .client = client,
              .chat_config = chat_config,
              .inventory_slots = std::span<const arv::ui::UiItemStackView>{
                  slots.data(),
                  slots.size()},
              .carried_inventory_stack = carried,
              .chat_lines = std::span<const arv::ui::ChatLineView>{
                  chat_lines.data(),
                  chat_lines.size()},
              .chat_history_scroll_offset = history_scroll_offset,
              .tab_players = std::span<const arv::ui::PlayerListEntryView>{
                  players.data(),
                  players.size()},
              .inventory_title = "Inventory",
              .tab_title = "Players",
              .viewport = {1280.0F, 720.0F},
              .cursor_position = {320.0F, 240.0F},
              .displayed_fps = 144U,
              .hotbar_item_popup_name = "Stone",
              .hotbar_item_popup_alpha = 0.75F,
              .ride_prompt = std::move(ride_prompt),
          });

  EXPECT_EQ(model.hotbar.slot_count, arv::player::DefaultHotbarSlotCount);
  EXPECT_EQ(model.hotbar.slots.size(), arv::player::DefaultHotbarSlotCount);
  EXPECT_EQ(model.hotbar.item_name_popup, "Stone");
  EXPECT_FLOAT_EQ(model.hotbar.item_name_popup_alpha, 0.75F);
  EXPECT_FALSE(model.inventory.visible);
  ASSERT_TRUE(model.inventory.cursor_stack.has_value());
  EXPECT_EQ(model.inventory.cursor_stack->display_name, "Stick");
  EXPECT_TRUE(model.chat.input_active);
  EXPECT_EQ(model.chat.draft, "draft");
  EXPECT_EQ(model.chat.history_scroll_offset, history_scroll_offset);
  EXPECT_EQ(model.tab_menu.header, "Players");
  ASSERT_EQ(model.tab_menu.players.size(), 1U);
  EXPECT_EQ(model.tab_menu.players.front().name, "Alex");
  EXPECT_TRUE(model.debug.show_fps);
  EXPECT_EQ(model.debug.fps, 144U);
  EXPECT_TRUE(model.ride_prompt.visible);
  EXPECT_EQ(model.ride_prompt.key_label, "R");
}
