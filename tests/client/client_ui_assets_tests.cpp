#include "arv/client/client_ui_assets.hpp"

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/text/language.hpp"
#include "arv/client/item_ui_model.hpp"
#include "arv/ui/core/ui.hpp"
#include "support/test_registries.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] arv::asset::ResourceManager test_resources() {
  return arv::asset::ResourceManager(
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT});
}

void write_pack_manifest(
    const std::filesystem::path& packs_root,
    std::string_view id,
    std::string_view namespaces) {
  arv::testsupport::write_text(
      packs_root / std::string{id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{id} + "\"\n"
                            "display_name: \"" +
          std::string{id} + "\"\n"
                            "version: \"1.0.0\"\n"
                            "namespaces:\n" +
          std::string{namespaces} +
          "side: \"both\"\n"
          "dependencies: []\n"
          "load_after: []\n"
          "load_before: []\n"
          "capabilities:\n"
          "  assets: false\n"
          "  data: true\n"
          "  config: true\n"
          "  native_plugin: false\n");
}

[[nodiscard]] arv::ui::UiTree make_test_panel_tree(
    const arv::ui::UiDisplayMetrics& metrics) {
  arv::ui::UiTree tree;
  arv::ui::UiElement panel;
  panel.stable_id = "test.panel";
  panel.type = arv::ui::UiElementType::Panel;
  panel.layout.width_mode = arv::ui::UiSizeMode::Fixed;
  panel.layout.height_mode = arv::ui::UiSizeMode::Fixed;
  panel.layout.width = 120.0F;
  panel.layout.height = 32.0F;
  panel.style.background = {0.1F, 0.2F, 0.3F, 1.0F};
  tree.add_child(tree.root_id(), std::move(panel));
  tree.layout_with_metrics(metrics, nullptr);
  return tree;
}

} // namespace

TEST(ClientUiAssetsTests, LoadsPackLanguageFallbackLocaleAndMessages) {
  arv::testsupport::ScopedTempDir temp("arvane_client_language_");
  const std::filesystem::path packs_root = temp.path / "packs";
  write_pack_manifest(packs_root, "arvane_base", "  - \"arvane\"\n");
  arv::testsupport::write_text(
      packs_root / "arvane_base/data/arvane/lang/en_US.yaml",
      "ui:\n"
      "  ok: \"OK\"\n"
      "  cancel: \"Cancel\"\n");
  arv::testsupport::write_text(
      packs_root / "arvane_base/data/arvane/lang/es_ES.yaml",
      "ui:\n"
      "  cancel: \"Cancelar\"\n");
  arv::testsupport::write_text(
      packs_root / "arvane_base/config/messages.yaml",
      "server:\n"
      "  greeting: \"Hola from config\"\n");

  auto packs = arv::asset::PackManager::load_from_packs_root(packs_root);
  ASSERT_TRUE(packs) << packs.error().describe();

  auto language = arv::client::load_pack_language(
      packs.value(),
      "es_ES",
      "en_US");

  ASSERT_TRUE(language) << language.error().message;
  EXPECT_EQ(language.value().locale(), "es_ES");
  EXPECT_EQ(language.value().text("ui.ok"), "OK");
  EXPECT_EQ(language.value().text("ui.cancel"), "Cancelar");
  EXPECT_EQ(language.value().text("server.greeting"), "Hola from config");
}

TEST(ClientUiAssetsTests, ReportsMissingPackLanguageFallbackLocale) {
  arv::testsupport::ScopedTempDir temp("arvane_client_language_missing_");
  const std::filesystem::path packs_root = temp.path / "packs";
  write_pack_manifest(packs_root, "arvane_base", "  - \"arvane\"\n");

  auto packs = arv::asset::PackManager::load_from_packs_root(packs_root);
  ASSERT_TRUE(packs) << packs.error().describe();

  auto language = arv::client::load_pack_language(packs.value());

  ASSERT_FALSE(language);
  EXPECT_NE(
      language.error().message.find("no language files were found"),
      std::string::npos);
}

TEST(ClientUiAssetsTests, AppliesLanguageDisplayNamesForHudItemViews) {
  arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const std::optional<arv::player::ItemID> dirt_id =
      item_registry.find_item("arvane:dirt");
  ASSERT_TRUE(dirt_id.has_value());
  ASSERT_NE(item_registry.item(dirt_id.value()), nullptr);
  EXPECT_EQ(item_registry.item(dirt_id.value())->display_name, "arvane:dirt");

  arv::text::LanguageCatalog language;
  for (arv::u32 index = 0; index < block_registry.block_count(); ++index) {
    const arv::voxel::BlockRecord* block =
        block_registry.block(arv::voxel::BlockID{index});
    ASSERT_NE(block, nullptr);
    language.set(
        arv::text::block_display_name_key(block->name),
        block->name == "arvane:dirt" ? "Dirt" : block->display_name);
  }
  for (arv::u32 index = 0; index < item_registry.item_count(); ++index) {
    const arv::player::ItemRecord* item =
        item_registry.item(arv::player::ItemID{index});
    ASSERT_NE(item, nullptr);
    if (item->source_block.has_value()) {
      continue;
    }
    language.set(arv::text::item_display_name_key(item->name), item->display_name);
  }

  auto block_names = arv::client::apply_client_block_display_names(
      language,
      block_registry);
  ASSERT_TRUE(block_names) << block_names.error().message;
  auto item_names = arv::client::apply_client_item_display_names(
      language,
      block_registry,
      item_registry);
  ASSERT_TRUE(item_names) << item_names.error().message;
  EXPECT_EQ(item_registry.item(dirt_id.value())->display_name, "Dirt");

  arv::player::Inventory inventory{1};
  ASSERT_TRUE(inventory.set_slot(
      0,
      arv::player::ItemStack{dirt_id.value(), 3U}));
  const arv::player::Inventory container_inventory{0};
  const std::optional<arv::player::ItemStack> carried_stack;
  const arv::asset::TextureAtlas item_atlas;
  const arv::asset::BlockTextureAtlas block_atlas;
  const arv::client::InventoryUiProjection projection =
      arv::client::project_inventory_ui(
          {
              .has_authoritative_inventory = true,
              .container_open = false,
              .inventory = inventory,
              .container_inventory = container_inventory,
              .carried_stack = carried_stack,
              .item_registry = item_registry,
              .item_atlas = item_atlas,
              .block_atlas = block_atlas,
          });

  ASSERT_EQ(projection.inventory_slots.size(), 1U);
  EXPECT_EQ(projection.inventory_slots[0].item_name, "arvane:dirt");
  EXPECT_EQ(projection.inventory_slots[0].display_name, "Dirt");
}

TEST(ClientUiAssetsTests, LoadsDefaultUiFontStacksFromTheme) {
  arv::asset::ResourceManager resources = test_resources();
  arv::asset::FontSystem fonts;
  const arv::ui::UiTheme theme = arv::ui::make_default_ui_theme();
  const arv::ui::UiDisplayMetrics metrics =
      arv::ui::UiDisplayMetrics::from_window_and_framebuffer(
          {800.0F, 600.0F},
          {1600.0F, 1200.0F});

  auto stacks = arv::client::load_ui_font_stacks(
      resources,
      fonts,
      theme,
      metrics);

  ASSERT_TRUE(stacks) << stacks.error().message;
  EXPECT_EQ(stacks.value().small.faces.size(), 1U);
  EXPECT_GE(stacks.value().body.faces.size(), 4U);
  EXPECT_EQ(stacks.value().heading.faces.size(), 1U);
  EXPECT_EQ(stacks.value().title.faces.size(), 1U);
  EXPECT_EQ(stacks.value().mono.faces.size(), 1U);
}

TEST(ClientUiAssetsTests, ReportsPrimaryFontLoadFailure) {
  arv::asset::ResourceManager resources = test_resources();
  arv::asset::FontSystem fonts;
  arv::ui::UiTheme theme = arv::ui::make_default_ui_theme();
  theme.typography.default_font = "arvane:font/not_real";

  auto stacks = arv::client::load_ui_font_stacks(
      resources,
      fonts,
      theme,
      arv::ui::UiDisplayMetrics::from_logical_size({800.0F, 600.0F}));

  ASSERT_FALSE(stacks);
  EXPECT_EQ(stacks.error().category, "assets");
  EXPECT_FALSE(stacks.error().message.empty());
}

TEST(ClientUiAssetsTests, SyncsDirtyFontAtlasAfterSuccessfulUpload) {
  arv::asset::ResourceManager resources = test_resources();
  arv::asset::FontSystem fonts;
  const arv::ui::UiTheme theme = arv::ui::make_default_ui_theme();
  auto stacks = arv::client::load_ui_font_stacks(
      resources,
      fonts,
      theme,
      arv::ui::UiDisplayMetrics::from_logical_size({800.0F, 600.0F}));
  ASSERT_TRUE(stacks) << stacks.error().message;

  auto layout = fonts.layout_text(
      "Sync",
      stacks.value().body,
      {0.0F, 0.0F},
      {1.0F, 1.0F, 1.0F, 1.0F});
  ASSERT_TRUE(layout) << layout.error().message;
  ASSERT_TRUE(fonts.atlas_dirty());

  int upload_calls = 0;
  const auto upload_font_atlas =
      [&upload_calls](const arv::render::TextureArrayUploadData& upload) -> bool {
    ++upload_calls;
    return upload.valid();
  };
  EXPECT_TRUE(arv::client::sync_ui_font_atlas(fonts, upload_font_atlas));
  EXPECT_EQ(upload_calls, 1);
  EXPECT_FALSE(fonts.atlas_dirty());
  EXPECT_TRUE(arv::client::sync_ui_font_atlas(fonts, upload_font_atlas));
  EXPECT_EQ(upload_calls, 1);
}

TEST(ClientUiAssetsTests, KeepsFontAtlasDirtyAfterFailedUpload) {
  arv::asset::ResourceManager resources = test_resources();
  arv::asset::FontSystem fonts;
  const arv::ui::UiTheme theme = arv::ui::make_default_ui_theme();
  auto stacks = arv::client::load_ui_font_stacks(
      resources,
      fonts,
      theme,
      arv::ui::UiDisplayMetrics::from_logical_size({800.0F, 600.0F}));
  ASSERT_TRUE(stacks) << stacks.error().message;

  auto layout = fonts.layout_text(
      "Retry",
      stacks.value().body,
      {0.0F, 0.0F},
      {1.0F, 1.0F, 1.0F, 1.0F});
  ASSERT_TRUE(layout) << layout.error().message;
  ASSERT_TRUE(fonts.atlas_dirty());

  int upload_calls = 0;
  const auto fail_upload =
      [&upload_calls](const arv::render::TextureArrayUploadData&) -> bool {
    ++upload_calls;
    return false;
  };
  EXPECT_FALSE(arv::client::sync_ui_font_atlas(fonts, fail_upload));
  EXPECT_EQ(upload_calls, 1);
  EXPECT_TRUE(fonts.atlas_dirty());
}

TEST(ClientUiAssetsTests, AppendsUiDrawCommandsAndSyncsFontAtlas) {
  const arv::ui::UiDisplayMetrics metrics =
      arv::ui::UiDisplayMetrics::from_logical_size({800.0F, 600.0F});
  const arv::ui::UiTree tree = make_test_panel_tree(metrics);
  arv::asset::FontSystem fonts;
  arv::ui::UiFontStacks stacks;
  arv::render::FramePacket frame;

  int sync_calls = 0;
  const arv::client::UiDrawCommandAppendResult result =
      arv::client::append_ui_draw_commands_with_font_sync(
          tree,
          fonts,
          stacks,
          frame,
          metrics,
          0.0F,
          [&sync_calls]() -> bool {
            ++sync_calls;
            return true;
          });

  EXPECT_EQ(sync_calls, 1);
  EXPECT_TRUE(result.font_atlas_synced);
  EXPECT_FALSE(result.rolled_back);
  EXPECT_EQ(result.ui_rects_appended, 1U);
  EXPECT_EQ(frame.ui_rects.size(), 1U);
}

TEST(ClientUiAssetsTests, RollsBackUiDrawCommandsWhenFontAtlasSyncFails) {
  const arv::ui::UiDisplayMetrics metrics =
      arv::ui::UiDisplayMetrics::from_logical_size({800.0F, 600.0F});
  const arv::ui::UiTree tree = make_test_panel_tree(metrics);
  arv::asset::FontSystem fonts;
  arv::ui::UiFontStacks stacks;
  arv::render::FramePacket frame;
  frame.ui_rects.push_back({});

  int sync_calls = 0;
  const arv::client::UiDrawCommandAppendResult result =
      arv::client::append_ui_draw_commands_with_font_sync(
          tree,
          fonts,
          stacks,
          frame,
          metrics,
          0.0F,
          [&sync_calls]() -> bool {
            ++sync_calls;
            return false;
          });

  EXPECT_EQ(sync_calls, 1);
  EXPECT_FALSE(result.font_atlas_synced);
  EXPECT_TRUE(result.rolled_back);
  EXPECT_EQ(result.ui_rects_appended, 0U);
  EXPECT_EQ(frame.ui_rects.size(), 1U);
  EXPECT_TRUE(frame.ui_textured_quads.empty());
  EXPECT_TRUE(frame.ui_glyphs.empty());
}

TEST(ClientUiAssetsTests, BuildsItemAtlasAndCrosshairImage) {
  arv::asset::ResourceManager resources = test_resources();
  const arv::player::ItemRegistry item_registry;

  auto assets = arv::client::load_client_ui_texture_assets(
      resources,
      item_registry,
      {.max_page_size = 1024,
       .max_pages = 2,
       .padding = 2,
       .require_uniform_non_fallback_size = false});

  ASSERT_TRUE(assets) << assets.error().message;
  EXPECT_FALSE(assets.value().item_atlas.pages.empty());
  EXPECT_TRUE(assets.value().crosshair_image.valid);
  EXPECT_EQ(
      assets.value().crosshair_image.source,
      arv::render::UiTextureSource::ItemAtlas);
}
