#include "arv/client/client_runtime.hpp"
#include "arv/client/client_ui_routing.hpp"
#include "arv/base/ui/screens/pause_menu.hpp"
#include "arv/net/transport.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

class RecordingScreen final : public arv::ui::UIScreen {
public:
  [[nodiscard]] arv::ui::MenuScreenType type() const noexcept override {
    return arv::ui::MenuScreenType::MainMenu;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "recording";
  }

  void build(arv::ui::UIContext&) override {
  }

  bool handle_text_input(std::string_view text, arv::ui::UIContext&) override {
    last_text_input = std::string{text};
    ++text_input_count;
    return true;
  }

  bool handle_backspace(arv::ui::UIContext&) override {
    ++backspace_count;
    return true;
  }

  bool handle_text_edit(
      const arv::ui::UITextEditInput& input,
      arv::ui::UIContext&) override {
    last_text_edit = input.command;
    last_text_edit_extend_selection = input.extend_selection;
    ++text_edit_count;
    return true;
  }

  bool handle_submit(arv::ui::UIContext&) override {
    ++submit_count;
    return true;
  }

  bool handle_captured_input(
      const arv::ui::CapturedInputView& input,
      arv::ui::UIContext&) override {
    last_captured_code = input.input_code;
    ++captured_count;
    return true;
  }

  bool handle_scroll(
      arv::Vec2f,
      arv::f32 wheel_y,
      arv::ui::UIContext&) override {
    last_scroll = wheel_y;
    ++scroll_count;
    return true;
  }

  bool handle_pointer(
      arv::Vec2f,
      arv::Vec2f,
      bool,
      bool,
      bool,
      arv::ui::UIContext&) override {
    ++pointer_count;
    return true;
  }

  int text_input_count{};
  int backspace_count{};
  int text_edit_count{};
  int submit_count{};
  int captured_count{};
  int scroll_count{};
  int pointer_count{};
  std::string last_text_input{};
  std::string last_captured_code{};
  arv::ui::UITextEditCommand last_text_edit{arv::ui::UITextEditCommand::MoveLeft};
  bool last_text_edit_extend_selection{};
  arv::f32 last_scroll{};
};

class CapturingCommandSink final : public arv::ui::UICommandSink {
public:
  void submit(arv::ui::UICommand command) override {
    commands.push_back(std::move(command));
  }

  std::vector<arv::ui::UICommand> commands{};
};

class FixedSettingsData final : public arv::ui::UIScreenDataSource {
public:
  [[nodiscard]] arv::ui::RenderSettingsView render_settings() const override {
    return render;
  }

  [[nodiscard]] arv::ui::ControlsSettingsView controls_settings() const override {
    return controls;
  }

  [[nodiscard]] arv::ui::InterfaceSettingsView interface_settings() const override {
    return interface;
  }

  [[nodiscard]] std::vector<arv::ui::KeyBindEntryView> key_bind_entries() const override {
    return key_binds;
  }

  arv::ui::RenderSettingsView render{};
  arv::ui::ControlsSettingsView controls{};
  arv::ui::InterfaceSettingsView interface{};
  std::vector<arv::ui::KeyBindEntryView> key_binds{};
};

[[nodiscard]] arv::ui::UiTree make_action_button_tree(
    std::string stable_id,
    std::string action_id) {
  arv::ui::UiTree tree;
  arv::ui::UiElement button;
  button.stable_id = std::move(stable_id);
  button.type = arv::ui::UiElementType::Button;
  button.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.width = 160.0F;
  button.layout.height = 34.0F;
  button.action = std::move(action_id);
  tree.add_child(tree.root_id(), std::move(button));
  tree.layout({240.0F, 120.0F}, nullptr);
  return tree;
}

[[nodiscard]] arv::ui::UiTree make_inventory_slot_tree() {
  arv::ui::UiTree tree;
  arv::ui::UiElement slot;
  slot.stable_id = "inventory.slot.3";
  slot.type = arv::ui::UiElementType::Button;
  slot.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  slot.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  slot.layout.width = 40.0F;
  slot.layout.height = 40.0F;
  slot.action = "inventory_slot";
  tree.add_child(tree.root_id(), std::move(slot));
  tree.layout({200.0F, 120.0F}, nullptr);
  return tree;
}

[[nodiscard]] arv::ui::UiTree make_cancel_capture_tree() {
  arv::ui::UiTree tree;
  arv::ui::UiElement button;
  button.stable_id = "key_bind.cancel_capture";
  button.type = arv::ui::UiElementType::Button;
  button.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.width = 80.0F;
  button.layout.height = 30.0F;
  button.action = "key_bind.cancel_capture";
  tree.add_child(tree.root_id(), std::move(button));
  tree.layout({200.0F, 120.0F}, nullptr);
  return tree;
}

[[nodiscard]] arv::ui::UiTree make_chat_action_tree(
    std::string stable_id,
    std::string action_id) {
  arv::ui::UiTree tree;
  arv::ui::UiElement button;
  button.stable_id = std::move(stable_id);
  button.type = arv::ui::UiElementType::Button;
  button.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.width = 120.0F;
  button.layout.height = 30.0F;
  button.action = std::move(action_id);
  tree.add_child(tree.root_id(), std::move(button));
  tree.layout({200.0F, 120.0F}, nullptr);
  return tree;
}

[[nodiscard]] std::vector<arv::ui::ChatLineView> make_click_chat_lines(
    arv::text::ClickAction action,
    std::string value) {
  arv::text::RichTextRun run;
  run.text = value;
  run.click_event = arv::text::ClickEvent{action, std::move(value)};
  arv::ui::ChatLineView line;
  line.text = run.text;
  line.runs.push_back(std::move(run));
  std::vector<arv::ui::ChatLineView> lines;
  lines.push_back(std::move(line));
  return lines;
}

TEST(ClientUiRoutingTests, InventorySlotClickRoutesThroughSharedHelper) {
  arv::ui::UiTree tree = make_inventory_slot_tree();
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  arv::platform::InputSnapshot input;
  input.mouse_left_pressed = true;
  input.cursor_x = 10.0F;
  input.cursor_y = 10.0F;

  EXPECT_TRUE(arv::client::route_inventory_mouse_input(
      tree,
      client,
      input,
      false));
}

TEST(ClientUiRoutingTests, InventoryOutsideClickDropsCarriedStack) {
  arv::ui::UiTree tree = make_inventory_slot_tree();
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  arv::platform::InputSnapshot input;
  input.mouse_right_pressed = true;
  input.cursor_x = 180.0F;
  input.cursor_y = 100.0F;

  EXPECT_TRUE(arv::client::route_inventory_mouse_input(
      tree,
      client,
      input,
      true));
}

TEST(ClientUiRoutingTests, PlayerMenuSettingsAndExitActionsSubmitSharedCommands) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  client.submit_input(arv::client::InputFrame{.open_inventory_pressed = true});
  client.set_player_menu_tab(arv::client::PlayerMenuTab::Settings);

  FixedSettingsData data;
  data.render.load_distance = 4;
  data.render.view_distance = 2;
  data.render.stream_distance = 3;
  data.render.simulation_distance = 1;
  CapturingCommandSink sink;

  arv::platform::InputSnapshot input;
  input.mouse_left_pressed = true;
  input.cursor_x = 10.0F;
  input.cursor_y = 10.0F;

  input.cursor_x = 159.0F;
  arv::ui::UiTree view_distance_tree =
      make_action_button_tree("render.view_distance.slider", "render.view_distance.slider");
  EXPECT_TRUE(arv::client::route_inventory_mouse_input(
      view_distance_tree,
      client,
      input,
      false,
      &sink,
      &data));
  ASSERT_EQ(sink.commands.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::SetRenderSettingsCommand>(sink.commands[0]));
  const auto& render =
      std::get<arv::ui::SetRenderSettingsCommand>(sink.commands[0]).settings;
  EXPECT_EQ(render.view_distance, 4);
  EXPECT_EQ(render.stream_distance, 4);

  sink.commands.clear();
  arv::platform::InputSnapshot release_input;
  release_input.mouse_left_released = true;
  release_input.cursor_x = 159.0F;
  release_input.cursor_y = 10.0F;
  EXPECT_TRUE(arv::client::route_inventory_mouse_input(
      view_distance_tree,
      client,
      release_input,
      false,
      &sink,
      &data));
  sink.commands.clear();
  input.cursor_x = 10.0F;
  arv::ui::UiTree main_menu_tree =
      make_action_button_tree("player_menu.exit.main_menu", "player_menu.exit.main_menu");
  EXPECT_TRUE(arv::client::route_inventory_mouse_input(
      main_menu_tree,
      client,
      input,
      false,
      &sink,
      &data));
  ASSERT_EQ(sink.commands.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<arv::ui::BackToMainMenuCommand>(sink.commands[0]));

  sink.commands.clear();
  arv::ui::UiTree quit_tree =
      make_action_button_tree("player_menu.exit.quit", "player_menu.exit.quit");
  EXPECT_TRUE(arv::client::route_inventory_mouse_input(
      quit_tree,
      client,
      input,
      false,
      &sink,
      &data));
  ASSERT_EQ(sink.commands.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<arv::ui::QuitGameCommand>(sink.commands[0]));
}

TEST(ClientUiRoutingTests, PlayerMenuKeyBindCaptureSubmitsBindingCommand) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  client.submit_input(arv::client::InputFrame{.open_inventory_pressed = true});
  client.set_player_menu_tab(arv::client::PlayerMenuTab::Settings);
  client.set_player_menu_settings_section(arv::client::PlayerMenuSettingsSection::KeyBinds);

  FixedSettingsData data;
  arv::ui::KeyBindEntryView jump;
  jump.action_name = "Jump";
  jump.display_name = "Jump";
  data.key_binds.push_back(jump);
  CapturingCommandSink sink;

  arv::platform::InputSnapshot input;
  input.mouse_left_pressed = true;
  input.cursor_x = 10.0F;
  input.cursor_y = 10.0F;
  arv::ui::UiTree capture_tree =
      make_action_button_tree("key_binds.row.0.primary", "key_bind.capture_primary");
  EXPECT_TRUE(arv::client::route_inventory_mouse_input(
      capture_tree,
      client,
      input,
      false,
      &sink,
      &data));
  ASSERT_TRUE(client.player_menu_key_bind_capture().has_value());
  EXPECT_EQ(client.player_menu_key_bind_capture()->action_name, "Jump");
  EXPECT_EQ(client.player_menu_key_bind_capture()->slot, 0U);

  EXPECT_TRUE(arv::client::route_player_menu_captured_input(
      client,
      &sink,
      arv::ui::CapturedInputView{
          arv::ui::InputBindingDevice::Keyboard,
          "KeyF",
          "F",
          0U}));
  ASSERT_EQ(sink.commands.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::SetKeyBindCommand>(sink.commands[0]));
  const auto& command = std::get<arv::ui::SetKeyBindCommand>(sink.commands[0]);
  EXPECT_EQ(command.action_name, "Jump");
  EXPECT_EQ(command.slot, 0U);
  ASSERT_TRUE(command.binding.has_value());
  EXPECT_EQ(command.binding->input_code, "KeyF");
  EXPECT_FALSE(client.player_menu_key_bind_capture().has_value());
}

TEST(ClientUiRoutingTests, UiMouseFocusRoutesButtonEdges) {
  arv::ui::UiTree tree = make_inventory_slot_tree();
  const arv::ui::UiElement* slot = tree.find_by_stable_id("inventory.slot.3");
  ASSERT_NE(slot, nullptr);

  arv::platform::InputSnapshot input;
  input.mouse_left_pressed = true;
  input.cursor_x = slot->rect.x + 1.0F;
  input.cursor_y = slot->rect.y + 1.0F;
  arv::client::route_ui_mouse_focus(tree, input);
  EXPECT_EQ(tree.active(), slot->node_id);

  input.mouse_left_pressed = false;
  input.mouse_left_released = true;
  arv::client::route_ui_mouse_focus(tree, input);
  EXPECT_EQ(tree.active().value, 0U);
}

TEST(ClientUiRoutingTests, BlockingScreenRoutesPointerScrollAndTextInput) {
  arv::ui::UiTree tree;
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  auto screen = std::make_unique<RecordingScreen>();
  RecordingScreen* recording = screen.get();
  screens.push(std::move(screen));

  arv::platform::InputSnapshot input;
  input.cursor_x = 12.0F;
  input.cursor_y = 18.0F;
  input.mouse_left_held = true;
  input.mouse_wheel_y = 2.0F;
  input.text_input = "abc";
  input.text_backspace_pressed = true;
  input.text_submit_pressed = true;
  input.text_edit_events.push_back(
      {arv::platform::TextEditCommand::MoveRight, true});

  EXPECT_TRUE(arv::client::route_blocking_screen_input(
      screens,
      context,
      input));
  EXPECT_EQ(recording->pointer_count, 1);
  EXPECT_EQ(recording->scroll_count, 1);
  EXPECT_FLOAT_EQ(recording->last_scroll, 2.0F);
  EXPECT_EQ(recording->text_input_count, 1);
  EXPECT_EQ(recording->last_text_input, "abc");
  EXPECT_EQ(recording->text_edit_count, 1);
  EXPECT_EQ(
      recording->last_text_edit,
      arv::ui::UITextEditCommand::MoveRight);
  EXPECT_TRUE(recording->last_text_edit_extend_selection);
  EXPECT_EQ(recording->backspace_count, 1);
  EXPECT_EQ(recording->submit_count, 1);
}

TEST(ClientUiRoutingTests, CapturedScreenRoutesRawInputThroughSharedHelper) {
  arv::ui::UiTree tree;
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  auto screen = std::make_unique<RecordingScreen>();
  RecordingScreen* recording = screen.get();
  screens.push(std::move(screen));

  arv::platform::InputSnapshot input;
  input.raw_input = arv::platform::RawInputEvent{
      arv::platform::InputBinding{
          arv::platform::InputDeviceType::Keyboard,
          "A",
          0U}};

  EXPECT_TRUE(arv::client::route_captured_screen_input(
      screens,
      context,
      input));
  EXPECT_EQ(recording->captured_count, 1);
  EXPECT_EQ(recording->last_captured_code, "A");
}

TEST(ClientUiRoutingTests, CapturedMouseInputIgnoresCancelCaptureHit) {
  arv::ui::UiTree tree = make_cancel_capture_tree();
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  auto screen = std::make_unique<RecordingScreen>();
  RecordingScreen* recording = screen.get();
  screens.push(std::move(screen));

  arv::platform::InputSnapshot input;
  input.cursor_x = 4.0F;
  input.cursor_y = 4.0F;
  input.raw_input = arv::platform::RawInputEvent{
      arv::platform::InputBinding{
          arv::platform::InputDeviceType::MouseButton,
          "MouseLeft",
          0U}};

  EXPECT_FALSE(arv::client::route_captured_screen_input(
      screens,
      context,
      input));
  EXPECT_EQ(recording->captured_count, 0);
}

TEST(ClientUiRoutingTests, GameplayPauseRouteOpensPauseFromGameplayRoot) {
  arv::ui::UiTree tree;
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_gameplay_screen());
  const arv::client::GameplayPauseMenuRoutingCallbacks callbacks{};

  const arv::client::GameplayPauseMenuRoutingResult result =
      arv::client::route_gameplay_pause_menu_input(
          screens,
          context,
          true,
          true,
          true,
          false,
          callbacks);

  EXPECT_TRUE(result.consumed_pause_menu);
  EXPECT_TRUE(result.consumed_text_cancel);
  EXPECT_TRUE(result.routed_cancel);
  EXPECT_TRUE(result.handled_cancel);
  EXPECT_FALSE(result.reset_gameplay_screen_stack);
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::PauseMenu);
  EXPECT_EQ(screens.depth(), 2U);
}

TEST(ClientUiRoutingTests, GameplayPauseRouteSkipsPauseWhenClientOverlayActive) {
  arv::ui::UiTree tree;
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_gameplay_screen());
  const arv::client::GameplayPauseMenuRoutingCallbacks callbacks{};

  const arv::client::GameplayPauseMenuRoutingResult result =
      arv::client::route_gameplay_pause_menu_input(
          screens,
          context,
          true,
          true,
          true,
          true,
          callbacks);

  EXPECT_TRUE(result.consumed_pause_menu);
  EXPECT_FALSE(result.consumed_text_cancel);
  EXPECT_FALSE(result.routed_cancel);
  EXPECT_FALSE(result.handled_cancel);
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::Gameplay);
  EXPECT_EQ(screens.depth(), 1U);
}

TEST(ClientUiRoutingTests, GameplayPauseRouteCancelsNestedScreen) {
  arv::ui::UiTree tree;
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_gameplay_screen());
  screens.push(std::make_unique<RecordingScreen>());
  const arv::client::GameplayPauseMenuRoutingCallbacks callbacks{};

  const arv::client::GameplayPauseMenuRoutingResult result =
      arv::client::route_gameplay_pause_menu_input(
          screens,
          context,
          true,
          true,
          true,
          true,
          callbacks);

  EXPECT_TRUE(result.consumed_pause_menu);
  EXPECT_TRUE(result.consumed_text_cancel);
  EXPECT_TRUE(result.routed_cancel);
  EXPECT_TRUE(result.handled_cancel);
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::Gameplay);
  EXPECT_EQ(screens.depth(), 1U);
}

TEST(ClientUiRoutingTests, GameplayPauseRouteResetsEmptyGameplayStack) {
  arv::ui::UiTree tree;
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  bool reset_called = false;
  const auto reset_gameplay_stack = [&]() {
    reset_called = true;
    screens.push(arv::ui::make_gameplay_screen());
  };
  const arv::client::GameplayPauseMenuRoutingCallbacks callbacks{
      .reset_gameplay_screen_stack = reset_gameplay_stack,
  };

  const arv::client::GameplayPauseMenuRoutingResult result =
      arv::client::route_gameplay_pause_menu_input(
          screens,
          context,
          true,
          true,
          true,
          false,
          callbacks);

  EXPECT_TRUE(reset_called);
  EXPECT_TRUE(result.reset_gameplay_screen_stack);
  EXPECT_TRUE(result.consumed_pause_menu);
  EXPECT_TRUE(result.consumed_text_cancel);
  EXPECT_TRUE(result.routed_cancel);
  EXPECT_TRUE(result.handled_cancel);
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::PauseMenu);
  EXPECT_EQ(screens.depth(), 2U);
}

TEST(ClientUiRoutingTests, GameplayPauseRouteConsumesInactiveSessionWithoutReset) {
  arv::ui::UiTree tree;
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  bool reset_called = false;
  const auto reset_gameplay_stack = [&]() {
    reset_called = true;
    screens.push(arv::ui::make_gameplay_screen());
  };
  const arv::client::GameplayPauseMenuRoutingCallbacks callbacks{
      .reset_gameplay_screen_stack = reset_gameplay_stack,
  };

  const arv::client::GameplayPauseMenuRoutingResult result =
      arv::client::route_gameplay_pause_menu_input(
          screens,
          context,
          true,
          false,
          false,
          false,
          callbacks);

  EXPECT_TRUE(result.consumed_pause_menu);
  EXPECT_FALSE(result.consumed_text_cancel);
  EXPECT_FALSE(result.routed_cancel);
  EXPECT_FALSE(result.handled_cancel);
  EXPECT_FALSE(result.reset_gameplay_screen_stack);
  EXPECT_FALSE(reset_called);
  EXPECT_TRUE(screens.empty());
}

TEST(ClientUiRoutingTests, GameplayPauseRouteIgnoresMissingPauseEdge) {
  arv::ui::UiTree tree;
  arv::ui::UIContext context{tree};
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_gameplay_screen());
  const arv::client::GameplayPauseMenuRoutingCallbacks callbacks{};

  const arv::client::GameplayPauseMenuRoutingResult result =
      arv::client::route_gameplay_pause_menu_input(
          screens,
          context,
          false,
          true,
          true,
          false,
          callbacks);

  EXPECT_FALSE(result.consumed_pause_menu);
  EXPECT_FALSE(result.consumed_text_cancel);
  EXPECT_FALSE(result.routed_cancel);
  EXPECT_FALSE(result.handled_cancel);
  EXPECT_EQ(screens.depth(), 1U);
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::Gameplay);
}

TEST(ClientUiRoutingTests, ChatMouseRouteScrollsAndClampsHistoryOffset) {
  arv::ui::UiTree tree;
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  arv::platform::InputSnapshot input;
  input.mouse_wheel_y = -1.0F;
  arv::usize offset = 99U;
  bool url_opened = false;
  std::string copied_text;
  std::string ran_command;
  const auto open_url = [&](std::string_view) {
    url_opened = true;
    return true;
  };
  const auto copy_to_clipboard = [&](std::string_view text) {
    copied_text = std::string{text};
    return true;
  };
  const auto run_command = [&](std::string_view command) {
    ran_command = std::string{command};
  };
  const arv::client::ChatMouseRoutingCallbacks callbacks{
      .open_url = open_url,
      .copy_to_clipboard = copy_to_clipboard,
      .run_command = run_command,
      .blocked_unsafe_url_message = "blocked url",
      .copy_unavailable_message = "copy unavailable",
  };

  EXPECT_TRUE(arv::client::route_chat_mouse_input(
      tree,
      client,
      input,
      {},
      10U,
      3U,
      offset,
      callbacks));
  EXPECT_EQ(offset, 6U);
}

TEST(ClientUiRoutingTests, ChatMouseRouteSuggestsCommandFromRunClick) {
  arv::ui::UiTree tree =
      make_chat_action_tree("chat.line.0.run.0", "chat_run");
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  std::vector<arv::ui::ChatLineView> lines = make_click_chat_lines(
      arv::text::ClickAction::SuggestCommand,
      "/wave");
  arv::platform::InputSnapshot input;
  input.mouse_left_pressed = true;
  input.cursor_x = 4.0F;
  input.cursor_y = 4.0F;
  arv::usize offset = 0U;
  bool url_opened = false;
  std::string copied_text;
  std::string ran_command;
  const auto open_url = [&](std::string_view) {
    url_opened = true;
    return true;
  };
  const auto copy_to_clipboard = [&](std::string_view text) {
    copied_text = std::string{text};
    return true;
  };
  const auto run_command = [&](std::string_view command) {
    ran_command = std::string{command};
  };
  const arv::client::ChatMouseRoutingCallbacks callbacks{
      .open_url = open_url,
      .copy_to_clipboard = copy_to_clipboard,
      .run_command = run_command,
      .blocked_unsafe_url_message = "blocked url",
      .copy_unavailable_message = "copy unavailable",
  };

  EXPECT_TRUE(arv::client::route_chat_mouse_input(
      tree,
      client,
      input,
      lines,
      1U,
      8U,
      offset,
      callbacks));
  EXPECT_EQ(client.chat_draft(), "/wave");
}

TEST(ClientUiRoutingTests, ChatMouseRouteRunsCommandCallbackAndCopyFallback) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  arv::platform::InputSnapshot input;
  input.mouse_left_pressed = true;
  input.cursor_x = 4.0F;
  input.cursor_y = 4.0F;
  arv::usize offset = 0U;
  bool url_opened = false;
  std::string copied_text;
  std::string ran_command;
  const auto open_url = [&](std::string_view) {
    url_opened = true;
    return true;
  };
  const auto copy_to_clipboard = [&](std::string_view text) {
    copied_text = std::string{text};
    return false;
  };
  const auto run_command = [&](std::string_view command) {
    ran_command = std::string{command};
  };
  const arv::client::ChatMouseRoutingCallbacks callbacks{
      .open_url = open_url,
      .copy_to_clipboard = copy_to_clipboard,
      .run_command = run_command,
      .blocked_unsafe_url_message = "blocked url",
      .copy_unavailable_message = "copy unavailable",
  };

  arv::ui::UiTree run_tree =
      make_chat_action_tree("chat.line.0.run.0", "chat_run");
  std::vector<arv::ui::ChatLineView> run_lines = make_click_chat_lines(
      arv::text::ClickAction::RunCommand,
      "/spawn");
  EXPECT_TRUE(arv::client::route_chat_mouse_input(
      run_tree,
      client,
      input,
      run_lines,
      1U,
      8U,
      offset,
      callbacks));
  EXPECT_EQ(ran_command, "/spawn");

  arv::ui::UiTree copy_tree =
      make_chat_action_tree("chat.line.0.run.0", "chat_run");
  std::vector<arv::ui::ChatLineView> copy_lines = make_click_chat_lines(
      arv::text::ClickAction::CopyToClipboard,
      "seed");
  EXPECT_TRUE(arv::client::route_chat_mouse_input(
      copy_tree,
      client,
      input,
      copy_lines,
      1U,
      8U,
      offset,
      callbacks));
  EXPECT_EQ(copied_text, "seed");
  ASSERT_FALSE(client.chat_messages().empty());
  EXPECT_EQ(client.chat_messages().back(), "copy unavailable");
}

TEST(ClientUiRoutingTests, ChatMouseRouteConfirmsSafeUrlThroughCallback) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  arv::platform::InputSnapshot input;
  input.mouse_left_pressed = true;
  input.cursor_x = 4.0F;
  input.cursor_y = 4.0F;
  arv::usize offset = 0U;
  bool url_opened = false;
  std::string copied_text;
  std::string ran_command;
  const auto open_url = [&](std::string_view url) {
    url_opened = url == "https://example.test";
    return url_opened;
  };
  const auto copy_to_clipboard = [&](std::string_view text) {
    copied_text = std::string{text};
    return true;
  };
  const auto run_command = [&](std::string_view command) {
    ran_command = std::string{command};
  };
  const arv::client::ChatMouseRoutingCallbacks callbacks{
      .open_url = open_url,
      .copy_to_clipboard = copy_to_clipboard,
      .run_command = run_command,
      .blocked_unsafe_url_message = "blocked url",
      .copy_unavailable_message = "copy unavailable",
  };

  arv::ui::UiTree run_tree =
      make_chat_action_tree("chat.line.0.run.0", "chat_run");
  std::vector<arv::ui::ChatLineView> lines = make_click_chat_lines(
      arv::text::ClickAction::OpenUrl,
      "https://example.test");
  EXPECT_TRUE(arv::client::route_chat_mouse_input(
      run_tree,
      client,
      input,
      lines,
      1U,
      8U,
      offset,
      callbacks));
  ASSERT_TRUE(client.pending_chat_url_confirmation().has_value());

  arv::ui::UiTree confirm_tree =
      make_chat_action_tree("chat.confirm", "chat_url_confirm");
  EXPECT_TRUE(arv::client::route_chat_mouse_input(
      confirm_tree,
      client,
      input,
      {},
      1U,
      8U,
      offset,
      callbacks));
  EXPECT_TRUE(url_opened);
  EXPECT_FALSE(client.pending_chat_url_confirmation().has_value());
}

} // namespace
