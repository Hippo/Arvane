#include "arv/asset/resource.hpp"
#include "arv/ui/ui.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <variant>
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
        role == arv::ui::UiTextRole::Title ? 30.0F :
        role == arv::ui::UiTextRole::Heading ? 22.0F :
        role == arv::ui::UiTextRole::Small ? 14.0F : 16.0F;
    return {static_cast<arv::f32>(text.size()) * width, height};
  }

  [[nodiscard]] arv::f32 line_height(
      arv::ui::UiTextRole role = arv::ui::UiTextRole::Body) const override {
    return role == arv::ui::UiTextRole::Title ? 30.0F :
           role == arv::ui::UiTextRole::Heading ? 22.0F :
           role == arv::ui::UiTextRole::Small ? 14.0F : 16.0F;
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

  [[nodiscard]] std::vector<arv::ui::LocalProfileEntry> local_profiles() const override {
    return profiles;
  }

  [[nodiscard]] arv::ui::PlayerSkinTextureView default_player_skin_texture() const override {
    return default_skin;
  }

  [[nodiscard]] std::vector<arv::ui::LocalProfileSkinEntry>
  pending_create_profile_skins() const override {
    return create_skins;
  }

  [[nodiscard]] bool has_pending_edit_profile_skins(
      std::string_view profile_id) const override {
    for (const auto& [id, skins] : edit_skins) {
      (void)skins;
      if (id == profile_id) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::vector<arv::ui::LocalProfileSkinEntry> pending_edit_profile_skins(
      std::string_view profile_id) const override {
    for (const auto& [id, skins] : edit_skins) {
      if (id == profile_id) {
        return skins;
      }
    }
    return {};
  }

  std::vector<arv::ui::SinglePlayerSaveEntry> saves{};
  std::vector<arv::ui::ServerListEntry> servers{};
  std::vector<arv::ui::LocalProfileEntry> profiles{};
  std::vector<arv::ui::LocalProfileSkinEntry> create_skins{};
  std::vector<std::pair<std::string, std::vector<arv::ui::LocalProfileSkinEntry>>> edit_skins{};
  arv::ui::PlayerSkinTextureView default_skin{};
};

[[nodiscard]] arv::ui::LocalProfileSkinEntry make_skin_entry(std::string hash) {
  arv::ui::LocalProfileSkinEntry skin;
  skin.skin_hash = std::move(hash);
  skin.width = 64;
  skin.height = 64;
  return skin;
}

[[nodiscard]] std::string hit_stable_id(
    const arv::ui::UiTree& tree,
    arv::Vec2f point) {
  const std::optional<arv::ui::UiNodeID> hit = tree.hit_test(point);
  if (!hit.has_value()) {
    return {};
  }
  const arv::ui::UiElement* element = tree.element(hit.value());
  return element == nullptr ? std::string{} : element->stable_id;
}

void expect_button_hittable(
    const arv::ui::UiTree& tree,
    std::string_view stable_id) {
  const arv::ui::UiElement* button = tree.find_by_stable_id(stable_id);
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(
      hit_stable_id(
          tree,
          {button->rect.x + button->rect.width * 0.5F,
           button->rect.y + button->rect.height * 0.5F}),
      stable_id);
  EXPECT_EQ(
      hit_stable_id(
          tree,
          {button->rect.x + button->rect.width - 2.0F,
           button->rect.y + button->rect.height * 0.5F}),
      stable_id);
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::trunc);
  output << text;
}

[[nodiscard]] bool is_pixel_snapped(arv::f32 value) {
  return std::abs(value - static_cast<arv::f32>(std::round(value))) <= 0.001F;
}

} // namespace

TEST(UiThemeTests, LoadsSelectedThemeAndScalesTokens) {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "arvane_ui_theme_loads";
  std::filesystem::remove_all(root);
  write_text_file(
      root / "config/style.yaml",
      "theme: \"warm\"\nui:\n  scale: 1.25\n  font_scale: 1.5\n  animation_scale: 0\n");
  write_text_file(
      root / "themes/warm.yaml",
      "name: \"Warm\"\ncolors:\n  accent: \"#336699\"\n  pause_backdrop: \"#01020380\"\nlayout:\n  spacing_md: 10\n"
      "typography:\n  size_md: 20\nwidgets:\n  button:\n    height: 40\n");

  const arv::ui::UiThemeLoadResult result = arv::ui::load_ui_theme(root);
  EXPECT_EQ(result.loaded_theme, "warm");
  EXPECT_FALSE(result.used_builtin_theme);
  EXPECT_FLOAT_EQ(result.theme.colors.accent.w, 1.0F);
  EXPECT_NEAR(result.theme.colors.pause_backdrop.w, 128.0F / 255.0F, 0.001F);
  EXPECT_FLOAT_EQ(result.theme.layout.spacing_md, 12.5F);
  EXPECT_EQ(result.theme.typography.size_md, 30U);
  EXPECT_FLOAT_EQ(result.config.ui.animation_scale, 0.0F);
  std::filesystem::remove_all(root);
}

TEST(UiThemeTests, InvalidThemeNameFallsBackToDefaultFile) {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "arvane_ui_theme_invalid_name";
  std::filesystem::remove_all(root);
  write_text_file(root / "config/style.yaml", "theme: \"../bad\"\n");
  write_text_file(root / "themes/default.yaml", "name: \"Fallback\"\ncolors:\n  accent: \"#112233\"\n");

  const arv::ui::UiThemeLoadResult result = arv::ui::load_ui_theme(root);
  EXPECT_TRUE(result.used_theme_fallback);
  EXPECT_EQ(result.loaded_theme, "default");
  EXPECT_EQ(result.theme.name, "Fallback");
  EXPECT_FALSE(result.warnings.empty());
  std::filesystem::remove_all(root);
}

TEST(UiThemeTests, MalformedTokensKeepBuiltInFallbacks) {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "arvane_ui_theme_bad_tokens";
  std::filesystem::remove_all(root);
  write_text_file(root / "config/style.yaml", "theme: \"default\"\nui:\n  scale: 100\n");
  write_text_file(
      root / "themes/default.yaml",
      "name: \"Bad Token Test\"\ncolors:\n  accent: \"not-a-color\"\nlayout:\n  radius_md: nope\n");

  const arv::ui::UiThemeLoadResult result = arv::ui::load_ui_theme(root);
  EXPECT_EQ(result.loaded_theme, "default");
  EXPECT_FLOAT_EQ(result.config.ui.scale, 4.0F);
  EXPECT_EQ(result.theme.colors.accent, arv::ui::default_ui_theme().colors.accent);
  EXPECT_FLOAT_EQ(
      result.theme.layout.radius_md,
      arv::ui::default_ui_theme().layout.radius_md * 4.0F);
  EXPECT_FALSE(result.warnings.empty());
  std::filesystem::remove_all(root);
}

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

TEST(UiInputTests, MouseRoutingSeparatesHoverActiveFromKeyboardFocus) {
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
  EXPECT_EQ(tree.keyboard_focus().value, 0U);
}

TEST(UiInputTests, HoveredControlsEmitHoverBackground) {
  arv::ui::UiTree tree;
  arv::ui::UiElement button;
  button.stable_id = "button";
  button.type = arv::ui::UiElementType::Button;
  button.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  button.layout.width = 80.0F;
  button.layout.height = 32.0F;
  button.style.background = {0.1F, 0.1F, 0.1F, 1.0F};
  button.style.background_hover = {0.2F, 0.3F, 0.4F, 1.0F};
  tree.add_child(tree.root_id(), std::move(button));
  tree.layout({200.0F, 100.0F}, nullptr);
  (void)tree.route_mouse_move({12.0F, 10.0F});

  arv::asset::FontSystem fonts;
  arv::ui::UiFontStacks stacks;
  arv::render::FramePacket frame;
  arv::ui::append_ui_draw_commands(tree, fonts, stacks, frame);

  ASSERT_EQ(frame.ui_rects.size(), 1U);
  EXPECT_EQ(frame.ui_rects.front().color, (arv::Vec4f{0.2F, 0.3F, 0.4F, 1.0F}));
}

TEST(UiTextRenderingTests, TextDrawCommandsSnapGlyphsToPixelGrid) {
  const std::filesystem::path asset_root{ARVANE_TEST_ASSET_ROOT};
  arv::asset::ResourceManager resources(asset_root);
  arv::asset::FontSystem fonts;
  ASSERT_TRUE(fonts.initialized());

  auto face = fonts.load_face(
      resources.resolve_asset_path({"arvane:font/font"}, arv::asset::AssetKind::Font),
      18,
      {"arvane:font/font"});
  ASSERT_TRUE(face) << face.error().message;

  const arv::asset::FontFaceHandle handles[] = {face.value()};
  const arv::asset::FontStack stack = fonts.make_stack(handles);
  arv::ui::UiFontStacks stacks;
  stacks.body = stack;

  arv::ui::UiTree tree;
  arv::ui::UiElement label;
  label.stable_id = "label";
  label.type = arv::ui::UiElementType::Text;
  label.text = "Clear text";
  label.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  label.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  label.layout.width = 140.0F;
  label.layout.height = 28.0F;
  tree.add_child(tree.root_id(), std::move(label));
  tree.layout({200.0F, 100.0F}, nullptr);

  arv::ui::UiElement* laid_out_label = tree.find_by_stable_id("label");
  ASSERT_NE(laid_out_label, nullptr);
  laid_out_label->rect.x = 10.35F;
  laid_out_label->rect.y = 20.65F;

  arv::render::FramePacket frame;
  arv::ui::append_ui_draw_commands(tree, fonts, stacks, frame);

  ASSERT_FALSE(frame.ui_glyphs.empty());
  for (const arv::render::UIGlyphDrawItem& glyph : frame.ui_glyphs) {
    EXPECT_TRUE(is_pixel_snapped(glyph.min.x));
    EXPECT_TRUE(is_pixel_snapped(glyph.min.y));
    EXPECT_TRUE(is_pixel_snapped(glyph.max.x));
    EXPECT_TRUE(is_pixel_snapped(glyph.max.y));
  }
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

TEST(HotbarHudTests, BlockModelIconsUseProjectedModelQuads) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  std::vector<arv::ui::UiItemStackView> slots(9);

  arv::ui::UiBlockModelQuadView quad;
  quad.image.valid = true;
  quad.image.source = arv::render::UiTextureSource::TerrainAtlas;
  quad.image.custom_points = true;
  quad.image.points = {{
      {3.0F, 9.0F},
      {17.0F, 16.0F},
      {17.0F, 30.0F},
      {3.0F, 23.0F},
  }};

  slots[0].item_name = "arvane:stone_stairs";
  slots[0].display_name = "Stone Stairs";
  slots[0].count = 1;
  slots[0].render_type = arv::player::ItemRenderType::Block;
  slots[0].block_model_quads.push_back(quad);

  arv::ui::build_hotbar_hud(tree, {0, 9, {800.0F, 600.0F}, {slots.data(), slots.size()}});
  tree.layout({800.0F, 600.0F}, &text);

  const auto* model_quad = tree.find_by_stable_id("hud.hotbar.slot.0.block_model.0");
  ASSERT_NE(model_quad, nullptr);
  EXPECT_EQ(model_quad->type, arv::ui::UiElementType::Image);
  EXPECT_TRUE(model_quad->image.custom_points);
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
  const auto* chat_line = tree.find_by_stable_id("chat.line.0");
  const auto* chat_shadow = tree.find_by_stable_id("chat.line.0.shadow");
  ASSERT_NE(chat_line, nullptr);
  ASSERT_NE(chat_shadow, nullptr);
  EXPECT_EQ(chat_line->style.text_role, arv::ui::UiTextRole::Body);
  EXPECT_EQ(chat_shadow->style.text_role, arv::ui::UiTextRole::Body);
  EXPECT_GT(chat_line->style.letter_spacing, 0.0F);
  EXPECT_EQ(chat_line->style.letter_spacing, chat_shadow->style.letter_spacing);
  EXPECT_GT(chat_shadow->style.foreground.w, 0.0F);
  const auto* chat_input = tree.find_by_stable_id("chat.input");
  ASSERT_NE(chat_input, nullptr);
  EXPECT_GE(chat_input->rect.height, tree.theme().widgets.input.height);
  EXPECT_GT(chat_input->style.background.w, 0.0F);
  EXPECT_LT(chat_input->style.background.w, 1.0F);
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

TEST(GameplayUiTests, EmptyPassiveChatDoesNotRenderABox) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;

  arv::ui::build_chat_ui(tree, {true, false, {}, ""});
  tree.layout({800.0F, 600.0F}, &text);
  EXPECT_EQ(tree.find_by_stable_id("chat.stack"), nullptr);

  arv::ui::build_chat_ui(tree, {true, true, {}, ""});
  tree.layout({800.0F, 600.0F}, &text);
  ASSERT_NE(tree.find_by_stable_id("chat.stack"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("chat.input"), nullptr);
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
  const auto* primary = tree.find_by_stable_id("menu.main.single_player");
  const auto* secondary = tree.find_by_stable_id("menu.main.multi_player");
  const auto* primary_label = tree.find_by_stable_id("menu.main.single_player.label");
  const auto* title = tree.find_by_stable_id("main_menu.title");
  ASSERT_NE(primary, nullptr);
  ASSERT_NE(secondary, nullptr);
  ASSERT_NE(primary_label, nullptr);
  ASSERT_NE(title, nullptr);
  EXPECT_GT(primary->style.radius, 0.0F);
  EXPECT_GT(primary->style.border_width, 0.0F);
  EXPECT_EQ(primary->style.control_role, arv::ui::UiControlRole::Primary);
  EXPECT_EQ(primary->style.background, secondary->style.background);
  EXPECT_FLOAT_EQ(primary->rect.x + primary->rect.width * 0.5F, 400.0F);
  EXPECT_FLOAT_EQ(secondary->rect.x + secondary->rect.width * 0.5F, 400.0F);
  EXPECT_FLOAT_EQ(title->rect.x + title->rect.width * 0.5F, 400.0F);
  EXPECT_FLOAT_EQ(
      primary_label->rect.x + primary_label->rect.width * 0.5F,
      primary->rect.x + primary->rect.width * 0.5F);
  EXPECT_FLOAT_EQ(
      primary_label->rect.y + primary_label->rect.height * 0.5F,
      primary->rect.y + primary->rect.height * 0.5F);
  EXPECT_TRUE(screens.pauses_singleplayer());
  EXPECT_FALSE(screens.handle_cancel(ctx));
  EXPECT_EQ(screens.depth(), 1U);

  EXPECT_TRUE(screens.route_action({"menu.main.single_player", "main.single_player", "click"}, ctx));
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::SinglePlayerMenu);
}

TEST(MenuScreenTests, ThemedRowsExposeSelectionAndDetailText) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  data.saves.push_back({"Long World", "long-world", "default", "last played today"});
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_single_player_menu_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  screens.update(ctx);
  tree.layout({800.0F, 600.0F}, &text);
  EXPECT_TRUE(screens.route_action(
      {"menu.single_player.world.0", "single_player.select_world", "click"},
      ctx));
  screens.update(ctx);
  tree.layout({800.0F, 600.0F}, &text);

  const auto* row = tree.find_by_stable_id("menu.single_player.world.0");
  const auto* detail = tree.find_by_stable_id("menu.single_player.world.0.detail");
  ASSERT_NE(row, nullptr);
  ASSERT_NE(detail, nullptr);
  EXPECT_TRUE(row->selected);
  EXPECT_EQ(detail->text, "last played today");
  EXPECT_EQ(detail->style.text_role, arv::ui::UiTextRole::Small);
}

TEST(MenuScreenTests, RepresentativeMenusKeepKeyControlsInsideNarrowViewport) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_main_menu_screen());
  arv::ui::UIContext ctx{tree, {480.0F, 360.0F}, &sink, &data, &screens};

  screens.update(ctx);
  tree.layout({480.0F, 360.0F}, &text);

  for (std::string_view id : {
           "menu.main.single_player",
           "menu.main.multi_player",
           "menu.main.settings",
           "menu.main.quit",
       }) {
    const auto* element = tree.find_by_stable_id(id);
    ASSERT_NE(element, nullptr) << id;
    EXPECT_GE(element->rect.x, 0.0F) << id;
    EXPECT_GE(element->rect.y, 0.0F) << id;
    EXPECT_LE(element->rect.x + element->rect.width, 480.0F) << id;
    EXPECT_LE(element->rect.y + element->rect.height, 360.0F) << id;
  }
}

TEST(MenuScreenTests, MainMenuAccountDropdownSelectsAndEditsProfiles) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  data.profiles.push_back({"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                           "Hippo",
                           "",
                           {},
                           true});
  data.profiles.push_back({"0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                           "Alt",
                           "",
                           {},
                           false});
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_main_menu_screen());
  arv::ui::UIContext ctx{tree, {900.0F, 640.0F}, &sink, &data, &screens};

  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  ASSERT_NE(tree.find_by_stable_id("menu.account.toggle"), nullptr);

  EXPECT_TRUE(screens.route_action({"menu.account.toggle", "account.toggle", "click"}, ctx));
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  ASSERT_NE(tree.find_by_stable_id("menu.account.profile.1"), nullptr);
  const auto* id_text = tree.find_by_stable_id("menu.account.profile.1.id");
  ASSERT_NE(id_text, nullptr);
  EXPECT_LT(id_text->text.size(), data.profiles[1].profile_id.size());
  const auto* name_clip = tree.find_by_stable_id("menu.account.profile.1.name.clip");
  ASSERT_NE(name_clip, nullptr);
  EXPECT_TRUE(name_clip->clip_children);

  EXPECT_TRUE(screens.route_action(
      {"menu.account.profile.1", "account.select", "click"},
      ctx));
  ASSERT_EQ(sink.commands.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::SelectLocalProfileCommand>(sink.commands[0]));
  EXPECT_EQ(
      std::get<arv::ui::SelectLocalProfileCommand>(sink.commands[0]).profile_id,
      data.profiles[1].profile_id);

  EXPECT_TRUE(screens.route_action({"menu.account.toggle", "account.toggle", "click"}, ctx));
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  EXPECT_TRUE(screens.route_action(
      {"menu.account.profile.0.edit", "account.edit", "click"},
      ctx));
  ASSERT_NE(screens.current(), nullptr);
  EXPECT_EQ(screens.current()->type(), arv::ui::MenuScreenType::AccountEdit);
}

TEST(MenuScreenTests, AccountEditSaveAndCancelAreExplicit) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  data.profiles.push_back({"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                           "Hippo",
                           "",
                           {},
                           true});
  arv::ui::UIScreenManager screens;
  arv::ui::UIContext ctx{tree, {900.0F, 640.0F}, &sink, &data, &screens};

  screens.push(arv::ui::make_account_edit_screen(data.profiles[0].profile_id));
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  ASSERT_NE(tree.find_by_stable_id("menu.account_edit.cancel"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("menu.account_edit.save"), nullptr);
  EXPECT_TRUE(screens.handle_text_input("2", ctx));
  EXPECT_TRUE(screens.route_action({"menu.account_edit.cancel", "account.cancel", "click"}, ctx));
  ASSERT_EQ(sink.commands.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<arv::ui::DiscardLocalProfileEditCommand>(sink.commands[0]));
  EXPECT_TRUE(screens.empty());

  screens.push(arv::ui::make_account_edit_screen(data.profiles[0].profile_id));
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  EXPECT_TRUE(screens.handle_text_input("2", ctx));
  EXPECT_TRUE(screens.route_action({"menu.account_edit.save", "account.save", "click"}, ctx));
  ASSERT_EQ(sink.commands.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::UpdateLocalProfileCommand>(sink.commands[1]));
  EXPECT_EQ(std::get<arv::ui::UpdateLocalProfileCommand>(sink.commands[1]).display_name, "Hippo2");
  EXPECT_TRUE(screens.empty());

  screens.push(arv::ui::make_account_edit_screen(data.profiles[0].profile_id));
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  EXPECT_TRUE(screens.handle_text_input("3", ctx));
  EXPECT_TRUE(screens.handle_cancel(ctx));
  ASSERT_EQ(sink.commands.size(), 3U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::UpdateLocalProfileCommand>(sink.commands[2]));
  EXPECT_EQ(std::get<arv::ui::UpdateLocalProfileCommand>(sink.commands[2]).display_name, "Hippo3");
}

TEST(MenuScreenTests, AccountCreateCancelDiscardsPendingCreateState) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  arv::ui::UIScreenManager screens;
  arv::ui::UIContext ctx{tree, {900.0F, 640.0F}, &sink, &data, &screens};

  screens.push(arv::ui::make_account_create_screen());
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  ASSERT_NE(tree.find_by_stable_id("menu.account_create.cancel"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("menu.account_create.complete"), nullptr);

  EXPECT_TRUE(screens.route_action({"menu.account_create.cancel", "account.cancel", "click"}, ctx));
  ASSERT_EQ(sink.commands.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<arv::ui::DiscardLocalProfileCreateCommand>(sink.commands[0]));
  EXPECT_TRUE(screens.empty());
}

TEST(MenuScreenTests, AccountEditSkinManagerStagesOrderAndPickerCommands) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  arv::ui::LocalProfileEntry profile;
  profile.profile_id = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  profile.display_name = "Hippo";
  profile.active = true;
  profile.skins.push_back(make_skin_entry("skin-a"));
  profile.skins.push_back(make_skin_entry("skin-b"));
  profile.skins.push_back(make_skin_entry("skin-c"));
  data.profiles.push_back(std::move(profile));

  arv::ui::UIScreenManager screens;
  arv::ui::UIContext ctx{tree, {900.0F, 640.0F}, &sink, &data, &screens};
  screens.push(arv::ui::make_account_edit_screen(data.profiles[0].profile_id));
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);

  EXPECT_TRUE(screens.route_action({"menu.account_edit.skin", "account.manage_skins", "click"}, ctx));
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  ASSERT_NE(tree.find_by_stable_id("menu.account_edit.skin_dialog.panel"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("menu.account_edit.skin_dialog.row.1.up"), nullptr);
  ASSERT_NE(tree.find_by_stable_id("menu.account_edit.skin_dialog.row.1.slim"), nullptr);
  expect_button_hittable(tree, "menu.account_edit.skin_dialog.add");
  expect_button_hittable(tree, "menu.account_edit.skin_dialog.cancel");
  expect_button_hittable(tree, "menu.account_edit.skin_dialog.done");

  EXPECT_TRUE(screens.route_action(
      {"menu.account_edit.skin_dialog.row.1.slim", "account.skin_toggle_slim", "click"},
      ctx));
  ASSERT_EQ(sink.commands.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::SetLocalProfileSkinSlimCommand>(sink.commands[0]));
  const auto& slim =
      std::get<arv::ui::SetLocalProfileSkinSlimCommand>(sink.commands[0]);
  ASSERT_TRUE(slim.profile_id.has_value());
  EXPECT_EQ(slim.profile_id.value(), data.profiles[0].profile_id);
  EXPECT_EQ(slim.skin_hash, "skin-b");
  EXPECT_TRUE(slim.slim_model);

  EXPECT_TRUE(screens.route_action(
      {"menu.account_edit.skin_dialog.row.1.up", "account.skin_up", "click"},
      ctx));
  ASSERT_EQ(sink.commands.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::SetLocalProfileSkinOrderCommand>(sink.commands[1]));
  const auto& reorder =
      std::get<arv::ui::SetLocalProfileSkinOrderCommand>(sink.commands[1]);
  ASSERT_TRUE(reorder.profile_id.has_value());
  EXPECT_EQ(reorder.profile_id.value(), data.profiles[0].profile_id);
  EXPECT_EQ(reorder.skin_hashes, (std::vector<std::string>{"skin-b", "skin-a", "skin-c"}));

  EXPECT_TRUE(screens.route_action(
      {"menu.account_edit.skin_dialog.add", "account.skin_add", "click"},
      ctx));
  ASSERT_EQ(sink.commands.size(), 3U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::PickLocalProfileSkinCommand>(sink.commands[2]));
  EXPECT_EQ(
      std::get<arv::ui::PickLocalProfileSkinCommand>(sink.commands[2]).profile_id.value(),
      data.profiles[0].profile_id);
}

TEST(MenuScreenTests, AccountCreateSkinManagerCanCancelBackToSnapshot) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  data.create_skins.push_back(make_skin_entry("skin-a"));
  data.create_skins.push_back(make_skin_entry("skin-b"));

  arv::ui::UIScreenManager screens;
  arv::ui::UIContext ctx{tree, {900.0F, 640.0F}, &sink, &data, &screens};
  screens.push(arv::ui::make_account_create_screen());
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);

  EXPECT_TRUE(screens.route_action({"menu.account_create.skin", "account.manage_skins", "click"}, ctx));
  screens.update(ctx);
  tree.layout({900.0F, 640.0F}, &text);
  ASSERT_NE(tree.find_by_stable_id("menu.account_create.skin_dialog.row.1.up"), nullptr);
  expect_button_hittable(tree, "menu.account_create.skin_dialog.add");
  expect_button_hittable(tree, "menu.account_create.skin_dialog.cancel");
  expect_button_hittable(tree, "menu.account_create.skin_dialog.done");

  EXPECT_TRUE(screens.route_action(
      {"menu.account_create.skin_dialog.row.1.up", "account.skin_up", "click"},
      ctx));
  ASSERT_EQ(sink.commands.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::SetLocalProfileSkinOrderCommand>(sink.commands[0]));
  const auto& reorder =
      std::get<arv::ui::SetLocalProfileSkinOrderCommand>(sink.commands[0]);
  EXPECT_FALSE(reorder.profile_id.has_value());
  EXPECT_EQ(reorder.skin_hashes, (std::vector<std::string>{"skin-b", "skin-a"}));

  EXPECT_TRUE(screens.route_action(
      {"menu.account_create.skin_dialog.cancel", "account.skin_cancel", "click"},
      ctx));
  ASSERT_EQ(sink.commands.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<arv::ui::SetLocalProfileSkinOrderCommand>(sink.commands[1]));
  const auto& cancel =
      std::get<arv::ui::SetLocalProfileSkinOrderCommand>(sink.commands[1]);
  EXPECT_FALSE(cancel.profile_id.has_value());
  EXPECT_EQ(cancel.skin_hashes, (std::vector<std::string>{"skin-a", "skin-b"}));
}

TEST(MenuScreenTests, PauseResumeEmitsCommandAndClosesScreen) {
  FixedTextMeasurer text;
  arv::ui::UiTree tree;
  CapturingCommandSink sink;
  FixedMenuData data;
  arv::ui::UIScreenManager screens;
  screens.push(arv::ui::make_pause_menu_screen());
  arv::ui::UIContext ctx{tree, {800.0F, 600.0F}, &sink, &data, &screens};

  screens.update(ctx);
  tree.layout({800.0F, 600.0F}, &text);

  EXPECT_EQ(tree.find_by_stable_id("pause_menu.panel"), nullptr);
  const auto* backdrop = tree.find_by_stable_id("pause_menu.backdrop");
  ASSERT_NE(backdrop, nullptr);
  EXPECT_GT(backdrop->style.background.w, 0.0F);
  EXPECT_LT(backdrop->style.background.w, 1.0F);
  const auto* stack = tree.find_by_stable_id("pause_menu.stack");
  ASSERT_NE(stack, nullptr);
  EXPECT_FLOAT_EQ(stack->rect.x + stack->rect.width * 0.5F, 400.0F);
  EXPECT_FLOAT_EQ(stack->rect.y + stack->rect.height * 0.5F, 300.0F);

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
