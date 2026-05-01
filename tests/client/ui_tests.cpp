#include "arv/ui/ui.hpp"

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

namespace {

class FixedTextMeasurer final : public arv::ui::IUiTextMeasurer {
public:
  [[nodiscard]] arv::Vec2f measure(std::string_view text) const override {
    return {static_cast<arv::f32>(text.size()) * 8.0F, 16.0F};
  }

  [[nodiscard]] arv::f32 line_height() const override {
    return 16.0F;
  }
};

class CapturingCommandSink final : public arv::ui::UICommandSink {
public:
  void submit(arv::ui::UICommand command) override {
    commands.push_back(std::move(command));
  }

  std::vector<arv::ui::UICommand> commands{};
};

class FixedMenuData final : public arv::ui::UIScreenDataSource {
public:
  [[nodiscard]] std::vector<arv::ui::SinglePlayerSaveEntry> singleplayer_saves() const override {
    return saves;
  }

  [[nodiscard]] std::vector<arv::ui::ServerListEntry> multiplayer_servers() const override {
    return servers;
  }

  std::vector<arv::ui::SinglePlayerSaveEntry> saves{};
  std::vector<arv::ui::ServerListEntry> servers{};
};

} // namespace

TEST(UiLayoutTests, VerticalStackArrangesChildrenDeterministically) {
  arv::ui::UiTree tree;
  arv::ui::UiElement stack;
  stack.stable_id = "stack";
  stack.type = arv::ui::UiElementType::VerticalStack;
  stack.layout.spacing = 4.0F;
  stack.layout.padding = {2.0F, 2.0F, 3.0F, 3.0F};
  const arv::ui::UiNodeID stack_id = tree.add_child(tree.root_id(), std::move(stack));

  arv::ui::UiElement first;
  first.stable_id = "first";
  first.type = arv::ui::UiElementType::Spacer;
  first.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  first.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  first.layout.width = 20.0F;
  first.layout.height = 10.0F;
  tree.add_child(stack_id, std::move(first));

  arv::ui::UiElement second;
  second.stable_id = "second";
  second.type = arv::ui::UiElementType::Spacer;
  second.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  second.layout.height_mode = arv::ui::UiSizeMode::Fixed;
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
  arv::ui::UiTree tree;
  arv::ui::UiElement button;
  button.stable_id = "button";
  button.type = arv::ui::UiElementType::Button;
  button.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.height_mode = arv::ui::UiSizeMode::Fixed;
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

  auto down = tree.route_mouse_down({12.0F, 10.0F}, arv::ui::UiMouseButton::Left);
  ASSERT_TRUE(down.has_value());
  EXPECT_NE(tree.active().value, 0U);
  EXPECT_NE(tree.keyboard_focus().value, 0U);
}

TEST(UiDocumentTests, RejectsUnsafePluginDocumentShapes) {
  arv::ui::UiDocument document;
  document.version = 1;
  document.screen_id = "shop";
  document.owner = "plugin:test";
  document.root = {"root", "root", "", "", {{"unknown_widget", "x", "", "", {}}}};

  const arv::ui::UiDocumentValidation validation = arv::ui::validate_ui_document(document);
  EXPECT_FALSE(validation.accepted);
  ASSERT_FALSE(validation.errors.empty());
}

TEST(HotbarHudTests, BuildsSelectedSlotAsRetainedUi) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  arv::ui::build_hotbar_hud(tree, {2, 9, {800.0F, 600.0F}});
  tree.layout({800.0F, 600.0F}, &text);

  const auto* selected = tree.find_by_stable_id("hud.hotbar.slot.2");
  ASSERT_NE(selected, nullptr);
  EXPECT_TRUE(selected->selected);
  EXPECT_GT(tree.elements().size(), 9U);
}

TEST(HotbarHudTests, RebuildClearsPreviousSelectedSlot) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;

  arv::ui::build_hotbar_hud(tree, {1, 9, {800.0F, 600.0F}});
  tree.layout({800.0F, 600.0F}, &text);
  ASSERT_TRUE(tree.find_by_stable_id("hud.hotbar.slot.1")->selected);

  arv::ui::build_hotbar_hud(tree, {4, 9, {800.0F, 600.0F}});
  tree.layout({800.0F, 600.0F}, &text);

  const auto* old_slot = tree.find_by_stable_id("hud.hotbar.slot.1");
  const auto* new_slot = tree.find_by_stable_id("hud.hotbar.slot.4");
  ASSERT_NE(old_slot, nullptr);
  ASSERT_NE(new_slot, nullptr);
  EXPECT_FALSE(old_slot->selected);
  EXPECT_TRUE(new_slot->selected);
}

TEST(HotbarHudTests, BuildsTextureItemIconsFromResolvedRenderMetadata) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  std::vector<arv::ui::UiItemStackView> slots(9);
  slots[0].item_name = "arvane:coal";
  slots[0].display_name = "Coal";
  slots[0].count = 32;
  slots[0].render_type = arv::player::ItemRenderType::Texture;
  slots[0].texture_icon.valid = true;
  slots[0].texture_icon.source = arv::render::UiTextureSource::ItemAtlas;

  arv::ui::build_hotbar_hud(tree, {0, 9, {800.0F, 600.0F}, {slots.data(), slots.size()}});
  tree.layout({800.0F, 600.0F}, &text);

  const auto* icon = tree.find_by_stable_id("hud.hotbar.slot.0.icon");
  ASSERT_NE(icon, nullptr);
  EXPECT_EQ(icon->type, arv::ui::UiElementType::Image);
  EXPECT_TRUE(icon->image.valid);
}

TEST(HotbarHudTests, BlockItemIconsKeepSideTextureTopAtTop) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  std::vector<arv::ui::UiItemStackView> slots(9);

  arv::ui::UiImageData mesh_ordered_face;
  mesh_ordered_face.valid = true;
  mesh_ordered_face.source = arv::render::UiTextureSource::TerrainAtlas;
  mesh_ordered_face.uvs = {{
      {0.0F, 1.0F},
      {1.0F, 1.0F},
      {0.0F, 0.0F},
      {1.0F, 0.0F},
  }};

  slots[0].item_name = "arvane:grass_block";
  slots[0].display_name = "Grass Block";
  slots[0].count = 1;
  slots[0].render_type = arv::player::ItemRenderType::Block;
  slots[0].block_faces[arv::voxel::face_index(arv::voxel::BlockFace::PositiveY)] =
      mesh_ordered_face;
  slots[0].block_faces[arv::voxel::face_index(arv::voxel::BlockFace::PositiveX)] =
      mesh_ordered_face;
  slots[0].block_faces[arv::voxel::face_index(arv::voxel::BlockFace::PositiveZ)] =
      mesh_ordered_face;

  arv::ui::build_hotbar_hud(tree, {0, 9, {800.0F, 600.0F}, {slots.data(), slots.size()}});
  tree.layout({800.0F, 600.0F}, &text);

  const auto* front = tree.find_by_stable_id("hud.hotbar.slot.0.block.front");
  const auto* right = tree.find_by_stable_id("hud.hotbar.slot.0.block.right");
  ASSERT_NE(front, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_FLOAT_EQ(front->image.uvs[0].y, 0.0F);
  EXPECT_FLOAT_EQ(front->image.uvs[2].y, 1.0F);
  EXPECT_FLOAT_EQ(right->image.uvs[0].y, 0.0F);
  EXPECT_FLOAT_EQ(right->image.uvs[2].y, 1.0F);
}

TEST(GameplayUiTests, BuildsInventoryChatTabAndScoreboardSurfaces) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;

  std::vector<arv::ui::UiItemStackView> slots(36);
  slots[0] = {"arvane:stone", "Stone", 32};
  std::vector<arv::ui::ChatLineView> chat{{"hello", 1.0F}};
  std::vector<arv::ui::PlayerListEntryView> players{{"local_player", 12}};
  std::vector<std::string> scoreboard_lines{"Health: {player_health}", ""};
  arv::ui::ScoreboardVariableMap vars{{"player_health", "20"}};

  arv::ui::build_gameplay_ui(
      tree,
      {
          {0, 9, {800.0F, 600.0F}, {slots.data(), 9}},
          {true, "Inventory", 0, 9, {800.0F, 600.0F}, {slots.data(), slots.size()}},
          {true, true, {chat.data(), chat.size()}, "test"},
          {true, "Players", "footer", {players.data(), players.size()}},
          {true, "Stats", {scoreboard_lines.data(), scoreboard_lines.size()}, &vars},
          {true, 144},
      });
  tree.layout({800.0F, 600.0F}, &text);

  ASSERT_NE(tree.find_by_stable_id("inventory.panel"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("chat.input"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("tab.panel"), nullptr);
  const auto* scoreboard_line = tree.find_by_stable_id("scoreboard.line.0");
  ASSERT_NE(scoreboard_line, nullptr);
  EXPECT_EQ(scoreboard_line->text, "Health: 20");
  const auto* fps = tree.find_by_stable_id("hud.debug.fps");
  ASSERT_NE(fps, nullptr);
  EXPECT_EQ(fps->text, "FPS: 144");
  EXPECT_FLOAT_EQ(fps->rect.x, 8.0F);
  EXPECT_FLOAT_EQ(fps->rect.y, 8.0F);
}

TEST(MenuScreenTests, MainMenuIsRootAndNavigatesToSinglePlayer) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  data.saves.push_back({"default", "server-save-1", "default", "saved"});
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_main_menu_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  screens.update(ctx);
  tree.layout({800.0F, 600.0F}, &text);

  ASSERT_NE(tree.find_by_stable_id("menu.main.single_player"), nullptr);
  EXPECT_TRUE(screens.pauses_singleplayer());
  EXPECT_FALSE(screens.handle_cancel(ctx));
  EXPECT_EQ(screens.depth(), 1U);

  EXPECT_TRUE(screens.route_action({"menu.main.single_player", "main.single_player", "click"}, ctx));
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::SinglePlayerMenu);
}

TEST(MenuScreenTests, PauseResumeEmitsCommandAndClosesScreen) {
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_pause_menu_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  EXPECT_TRUE(screens.pauses_singleplayer());
  EXPECT_TRUE(screens.route_action({"menu.pause.resume", "pause.resume", "click"}, ctx));

  EXPECT_TRUE(screens.empty());
  ASSERT_EQ(sink.commands.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<arv::ui::ResumeGameCommand>(sink.commands[0]));
}

TEST(MenuScreenTests, SinglePlayerEmptyListStaysOnWorldListUntilCreate) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_single_player_menu_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  screens.update(ctx);
  tree.layout({800.0F, 600.0F}, &text);

  ASSERT_NE(tree.find_by_stable_id("menu.single_player.empty"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("menu.single_player.create"), nullptr);
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::SinglePlayerMenu);
  EXPECT_TRUE(sink.commands.empty());

  EXPECT_TRUE(screens.route_action({"menu.single_player.create", "single_player.create", "click"}, ctx));
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::CreateWorld);
  EXPECT_TRUE(sink.commands.empty());
}

TEST(MenuScreenTests, SinglePlayerSelectedWorldEmitsServerSaveAndWorldName) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  data.saves.push_back({"Server One", "server-one", "default", "server-save: server-one"});
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_single_player_menu_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  screens.update(ctx);
  tree.layout({800.0F, 600.0F}, &text);
  const auto* label = tree.find_by_stable_id("menu.single_player.world.0.label");
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->text, "Server One");

  EXPECT_TRUE(screens.route_action(
      {"menu.single_player.world.0", "single_player.select_world", "click"},
      ctx));
  EXPECT_TRUE(sink.commands.empty());
  EXPECT_TRUE(screens.route_action(
      {"menu.single_player.world.0", "single_player.select_world", "click"},
      ctx));

  ASSERT_EQ(sink.commands.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::StartSinglePlayerCommand>(sink.commands[0]));
  const auto& command = std::get<arv::ui::StartSinglePlayerCommand>(sink.commands[0]);
  EXPECT_EQ(command.server_save_name, "server-one");
  EXPECT_EQ(command.world_name, "default");
  EXPECT_TRUE(screens.empty());
}

TEST(MenuScreenTests, CreateWorldValidatesBeforeEmittingCommand) {
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  data.saves.push_back({"Existing", "existing", "default", "saved"});
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_create_world_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  EXPECT_TRUE(screens.handle_text_input("Existing", ctx));
  EXPECT_TRUE(screens.handle_submit(ctx));
  EXPECT_TRUE(sink.commands.empty());
  EXPECT_EQ(screens.depth(), 1U);

  screens.clear();
  screens.push(arv::ui::make_create_world_screen());
  EXPECT_TRUE(screens.handle_text_input("Fresh", ctx));
  EXPECT_TRUE(screens.handle_submit(ctx));

  ASSERT_EQ(sink.commands.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::CreateWorldCommand>(sink.commands[0]));
  EXPECT_EQ(std::get<arv::ui::CreateWorldCommand>(sink.commands[0]).name, "Fresh");
}

TEST(MenuScreenTests, AddServerAcceptsAddressWithoutPort) {
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_add_server_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  EXPECT_TRUE(screens.handle_text_input("Local", ctx));
  EXPECT_TRUE(screens.route_action({"menu.server.address", "server.focus_address", "click"}, ctx));
  EXPECT_TRUE(screens.handle_text_input("localhost", ctx));
  EXPECT_TRUE(screens.handle_submit(ctx));

  ASSERT_EQ(sink.commands.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::SaveServerEntryCommand>(sink.commands[0]));
  const auto& command = std::get<arv::ui::SaveServerEntryCommand>(sink.commands[0]);
  EXPECT_EQ(command.server.name, "Local");
  EXPECT_EQ(command.server.address, "localhost");
}

TEST(MenuScreenTests, AddServerRejectsMalformedExplicitPort) {
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_add_server_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  EXPECT_TRUE(screens.handle_text_input("Local", ctx));
  EXPECT_TRUE(screens.route_action({"menu.server.address", "server.focus_address", "click"}, ctx));
  EXPECT_TRUE(screens.handle_text_input("localhost:nope", ctx));
  EXPECT_TRUE(screens.handle_submit(ctx));

  EXPECT_TRUE(sink.commands.empty());
  EXPECT_EQ(screens.depth(), 1U);
}
