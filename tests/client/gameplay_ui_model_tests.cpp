#include "arv/client/gameplay_ui_model.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/text/language.hpp"

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

TEST(GameplayUiModelProjectionTests, BuildsRidePromptModelWhenGameplayCanMount) {
  const auto has_targeted_rideable = []() -> bool {
    return true;
  };

  const arv::ui::RidePromptModel prompt =
      arv::client::build_ride_prompt_model(
          {
              .interactions_available = true,
              .key_label = "M",
              .viewport = {1280.0F, 720.0F},
              .has_targeted_rideable = has_targeted_rideable,
          });

  EXPECT_TRUE(prompt.visible);
  EXPECT_EQ(prompt.key_label, "M");
  EXPECT_EQ(prompt.suffix, " to Ride");
  EXPECT_FLOAT_EQ(prompt.alpha, 1.0F);
  EXPECT_FLOAT_EQ(prompt.viewport.x, 1280.0F);
  EXPECT_FLOAT_EQ(prompt.viewport.y, 720.0F);
}

TEST(GameplayUiModelProjectionTests, BuildsMountExitPromptModelWhileMountedAndFading) {
  const arv::ui::RidePromptModel prompt =
      arv::client::build_ride_prompt_model(
          {
              .interactions_available = true,
              .local_player_mounted = true,
              .mount_exit_prompt_alpha = 0.45F,
              .key_label = "F",
              .viewport = {1280.0F, 720.0F},
          });

  EXPECT_TRUE(prompt.visible);
  EXPECT_EQ(prompt.key_label, "F");
  EXPECT_EQ(prompt.suffix, " to Exit");
  EXPECT_FLOAT_EQ(prompt.alpha, 0.45F);
}

TEST(GameplayUiModelProjectionTests, RidePromptModelSkipsTargetLookupWhenBlocked) {
  bool target_lookup_called = false;
  const auto has_targeted_rideable = [&target_lookup_called]() -> bool {
    target_lookup_called = true;
    return true;
  };

  const arv::ui::RidePromptModel prompt =
      arv::client::build_ride_prompt_model(
          {
              .interactions_available = true,
              .gameplay_blocked = true,
              .key_label = "M",
              .has_targeted_rideable = has_targeted_rideable,
          });

  EXPECT_FALSE(prompt.visible);
  EXPECT_FALSE(target_lookup_called);
}

TEST(GameplayUiModelProjectionTests, RidePromptModelRequiresTargetedRideable) {
  const auto has_targeted_rideable = []() -> bool {
    return false;
  };

  const arv::ui::RidePromptModel prompt =
      arv::client::build_ride_prompt_model(
          {
              .interactions_available = true,
              .key_label = "M",
              .has_targeted_rideable = has_targeted_rideable,
          });

  EXPECT_FALSE(prompt.visible);
  EXPECT_TRUE(prompt.key_label.empty());
}

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
  EXPECT_TRUE(model.hotbar.visible);
  EXPECT_EQ(model.hotbar.item_name_popup, "Stone");
  EXPECT_FLOAT_EQ(model.hotbar.item_name_popup_alpha, 0.75F);
  EXPECT_FALSE(model.inventory.visible);
  ASSERT_TRUE(model.player_menu.inventory.cursor_stack.has_value());
  EXPECT_EQ(model.player_menu.inventory.cursor_stack->display_name, "Stick");
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

TEST(GameplayUiModelProjectionTests, PlayerInventoryKeepsGameplayHotbarVisible) {
  arv::client::ClientRuntime client = make_client();
  client.submit_input(arv::client::InputFrame{.open_inventory_pressed = true});
  ASSERT_TRUE(client.inventory_open());

  std::vector<arv::ui::UiItemStackView> slots(12);
  slots[0] = stack_view("arvane:stone", "Stone", 6U);

  const arv::ui::GameplayUiModel model =
      arv::client::build_gameplay_ui_model(
          {
              .client = client,
              .chat_config = arv::io::UiConfig::ChatConfig{},
              .inventory_slots = std::span<const arv::ui::UiItemStackView>{
                  slots.data(),
                  slots.size()},
              .inventory_title = "Inventory",
              .viewport = {1280.0F, 720.0F},
          });

  EXPECT_TRUE(model.player_menu.visible);
  EXPECT_TRUE(model.player_menu.inventory.visible);
  EXPECT_TRUE(model.hotbar.visible);
}

TEST(GameplayUiModelProjectionTests, SnapshotBuilderUsesLanguageFallbackAndPopupGate) {
  arv::client::ClientRuntime client = make_client();
  arv::text::LanguageCatalog language;
  language.set("ui.inventory.title", "Localized Inventory");
  language.set("ui.tab.players", "Localized Players");

  arv::ui::RidePromptModel ride_prompt;
  ride_prompt.visible = true;
  ride_prompt.key_label = "R";

  const arv::ui::GameplayUiModel model =
      arv::client::build_gameplay_ui_model_from_snapshots(
          {
              .client = client,
              .chat_config = arv::io::UiConfig::ChatConfig{},
              .viewport = {800.0F, 450.0F},
              .cursor_position = {12.0F, 34.0F},
              .hotbar_item_popup_name = "Stone",
              .hotbar_item_popup_alpha = 0.0F,
              .crosshair = {.visible = true},
              .ride_prompt = std::move(ride_prompt),
              .language = &language,
              .debug_fps_visible = false,
          });

  EXPECT_EQ(model.player_menu.inventory.title, "Localized Inventory");
  EXPECT_EQ(model.tab_menu.header, "Localized Players");
  EXPECT_TRUE(model.hotbar.item_name_popup.empty());
  EXPECT_FLOAT_EQ(model.hotbar.item_name_popup_alpha, 0.0F);
  EXPECT_TRUE(model.crosshair.visible);
  EXPECT_FLOAT_EQ(model.crosshair.viewport.x, 800.0F);
  EXPECT_FLOAT_EQ(model.crosshair.viewport.y, 450.0F);
  EXPECT_TRUE(model.ride_prompt.visible);
  EXPECT_FLOAT_EQ(model.ride_prompt.viewport.x, 800.0F);
  EXPECT_FLOAT_EQ(model.ride_prompt.viewport.y, 450.0F);
  EXPECT_FALSE(model.debug.show_fps);
}

TEST(GameplayUiModelProjectionTests, SnapshotBuilderKeepsExplicitTitlesAndVisiblePopup) {
  arv::client::ClientRuntime client = make_client();
  arv::text::LanguageCatalog language;
  language.set("ui.inventory.title", "Ignored Inventory");
  language.set("ui.tab.players", "Ignored Players");

  const arv::ui::GameplayUiModel model =
      arv::client::build_gameplay_ui_model_from_snapshots(
          {
              .client = client,
              .chat_config = arv::io::UiConfig::ChatConfig{},
              .inventory_title = "Backpack",
              .tab_title = "Crew",
              .hotbar_item_popup_name = "Pickaxe",
              .hotbar_item_popup_alpha = 0.5F,
              .language = &language,
          });

  EXPECT_EQ(model.player_menu.inventory.title, "Backpack");
  EXPECT_EQ(model.tab_menu.header, "Crew");
  EXPECT_EQ(model.hotbar.item_name_popup, "Pickaxe");
  EXPECT_FLOAT_EQ(model.hotbar.item_name_popup_alpha, 0.5F);
}
