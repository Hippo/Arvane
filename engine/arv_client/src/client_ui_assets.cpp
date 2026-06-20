#include "arv/client/client_ui_assets.hpp"

#include "arv/client/item_ui_model.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/render/render_types.hpp"
#include "arv/ui/core/ui.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::client {
namespace {

constexpr std::string_view AssetsCategory = "assets";

[[nodiscard]] UiFontAssetLoadError font_error(std::string message) {
  return {.category = std::string{AssetsCategory}, .message = std::move(message)};
}

[[nodiscard]] ClientDisplayNameApplyError missing_language_key_error(
    std::string key) {
  return {
      .category = "lang",
      .message = "missing language key: " + std::move(key)};
}

} // namespace

UiFontStackLoadResult load_ui_font_stack(
    asset::ResourceManager& resources,
    asset::FontSystem& font_system,
    std::string asset_name,
    u32 pixel_size,
    asset::FontStack& stack,
    UiFontStackLoadOptions options) {
  std::vector<asset::FontFaceHandle> faces;
  asset::AssetID primary_id{std::move(asset_name)};
  const std::filesystem::path primary_path =
      resources.resolve_asset_path(primary_id, asset::AssetKind::Font);
  auto primary = font_system.load_face(
      primary_path,
      pixel_size,
      primary_id);
  if (!primary) {
    return UiFontStackLoadResult::failure(font_error(primary.error().message));
  }
  faces.emplace_back(primary.value());

  if (options.include_fallbacks) {
    for (const asset::AssetID fallback : {
             asset::AssetID{"arvane:font/symbol"},
             asset::AssetID{"arvane:font/arabic"},
             asset::AssetID{"arvane:font/emoji"},
         }) {
      const std::filesystem::path path =
          resources.resolve_asset_path(fallback, asset::AssetKind::Font);
      if (!std::filesystem::exists(path)) {
        continue;
      }
      auto face = font_system.load_face(path, pixel_size, fallback);
      if (face) {
        faces.emplace_back(face.value());
        continue;
      }
      if (options.fallback_warning) {
        const UiFontFallbackWarning warning{
            .asset = fallback,
            .message = face.error().message};
        options.fallback_warning(warning);
      }
    }
  }

  stack = font_system.make_stack(faces);
  return UiFontStackLoadResult::success();
}

UiFontStacksLoadResult load_ui_font_stacks(
    asset::ResourceManager& resources,
    asset::FontSystem& font_system,
    const ui::UiTheme& theme,
    const ui::UiDisplayMetrics& metrics,
    FunctionRef<void(const UiFontFallbackWarning&)> fallback_warning) {
  ui::UiFontStacks stacks;
  const ui::UiThemeTypography& type = theme.typography;
  const auto load_stack =
      [&](std::string asset_name,
          u32 pixel_size,
          asset::FontStack& stack,
          bool include_fallbacks) -> UiFontStackLoadResult {
    return load_ui_font_stack(
        resources,
        font_system,
        std::move(asset_name),
        pixel_size,
        stack,
        {.include_fallbacks = include_fallbacks,
         .fallback_warning = fallback_warning});
  };

  if (auto result = load_stack(
          type.default_font,
          metrics.font_pixel_size(type.size_sm),
          stacks.small,
          false);
      !result) {
    return UiFontStacksLoadResult::failure(result.error());
  }
  if (auto result = load_stack(
          type.default_font,
          metrics.font_pixel_size(type.size_md),
          stacks.body,
          true);
      !result) {
    return UiFontStacksLoadResult::failure(result.error());
  }
  if (auto result = load_stack(
          type.heading_font,
          metrics.font_pixel_size(type.size_lg),
          stacks.heading,
          false);
      !result) {
    return UiFontStacksLoadResult::failure(result.error());
  }
  if (auto result = load_stack(
          type.heading_font,
          metrics.font_pixel_size(type.size_xl),
          stacks.title,
          false);
      !result) {
    return UiFontStacksLoadResult::failure(result.error());
  }
  if (auto result = load_stack(
          type.monospace_font,
          metrics.font_pixel_size(type.size_md),
          stacks.mono,
          false);
      !result) {
    return UiFontStacksLoadResult::failure(result.error());
  }

  return UiFontStacksLoadResult::success(std::move(stacks));
}

text::LanguageResult load_pack_language(
    const asset::PackManager& packs,
    std::string locale,
    std::string fallback_locale) {
  const auto language_paths_for =
      [&](std::string_view selected_locale) {
    const std::vector<asset::PackDataFile> files =
        packs.data_files_at("lang/" + std::string{selected_locale} + ".yaml");
    std::vector<std::filesystem::path> paths;
    paths.reserve(files.size());
    for (const asset::PackDataFile& file : files) {
      paths.push_back(file.path);
    }
    return paths;
  };

  auto fallback = text::load_language_files(
      language_paths_for(fallback_locale),
      fallback_locale);
  if (!fallback) {
    return fallback;
  }

  text::LanguageCatalog catalog = std::move(fallback.value());
  if (locale != fallback_locale) {
    auto selected = text::load_language_files(
        language_paths_for(locale),
        locale);
    if (!selected) {
      return selected;
    }
    catalog.merge_from(std::move(selected.value()));
  }
  if (auto messages = packs.load_config_text("arvane:messages", ".yaml");
      messages) {
    auto config_messages = text::parse_language_yaml(
        messages.value(),
        catalog.locale());
    if (!config_messages) {
      return config_messages;
    }
    catalog.merge_from(std::move(config_messages.value()));
  }
  catalog.set_locale(std::move(locale));
  return text::LanguageResult::success(std::move(catalog));
}

ClientDisplayNameApplyResult apply_client_block_display_names(
    const text::LanguageCatalog& language,
    voxel::BlockRegistry& registry) {
  for (u32 index = 0; index < registry.block_count(); ++index) {
    const voxel::BlockID block_id{index};
    const voxel::BlockRecord* block = registry.block(block_id);
    if (block == nullptr) {
      continue;
    }
    const std::string key = text::block_display_name_key(block->name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return ClientDisplayNameApplyResult::failure(
          missing_language_key_error(key));
    }
    registry.set_display_name(block_id, std::string{text.value()});
  }
  return ClientDisplayNameApplyResult::success();
}

ClientDisplayNameApplyResult apply_client_item_display_names(
    const text::LanguageCatalog& language,
    const voxel::BlockRegistry& blocks,
    player::ItemRegistry& registry) {
  for (u32 index = 0; index < registry.item_count(); ++index) {
    const player::ItemID item_id{index};
    const player::ItemRecord* item = registry.item(item_id);
    if (item == nullptr) {
      continue;
    }
    const std::string key = text::item_display_name_key(item->name);
    if (const std::optional<std::string_view> text = language.find(key);
        text.has_value()) {
      registry.set_display_name(item_id, std::string{text.value()});
      continue;
    }
    if (item->source_block.has_value()) {
      const voxel::BlockRecord* block = blocks.block(item->source_block.value());
      if (block != nullptr && !block->display_name.empty()) {
        registry.set_display_name(item_id, block->display_name);
        continue;
      }
    }
    return ClientDisplayNameApplyResult::failure(
        missing_language_key_error(key));
  }
  return ClientDisplayNameApplyResult::success();
}

ClientDisplayNameApplyResult apply_client_entity_display_names(
    const text::LanguageCatalog& language,
    entity::EntityRegistry& registry) {
  for (const entity::EntityTypeRecord& entity : registry.records()) {
    const std::string key = text::entity_display_name_key(entity.name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return ClientDisplayNameApplyResult::failure(
          missing_language_key_error(key));
    }
    registry.set_display_name(entity.id, std::string{text.value()});
  }
  return ClientDisplayNameApplyResult::success();
}

ClientUiTextureAssetsResult load_client_ui_texture_assets(
    asset::ResourceManager& resources,
    const player::ItemRegistry& item_registry,
    asset::TextureAtlasBuildOptions item_atlas_options) {
  const std::array<asset::AssetID, 1> ui_texture_assets{{
      asset::AssetID{std::string{GameplayCrosshairTextureAsset}},
  }};
  auto item_atlas = build_item_texture_atlas(
      resources,
      item_registry,
      item_atlas_options,
      ui_texture_assets);
  if (!item_atlas) {
    return ClientUiTextureAssetsResult::failure(std::move(item_atlas.error()));
  }
  if (item_atlas.value().pages.empty()) {
    return ClientUiTextureAssetsResult::failure({
        .asset_id = std::string{GameplayCrosshairTextureAsset},
        .operation = "build client UI texture assets",
        .message = "item texture atlas did not produce a page"});
  }

  ClientUiTextureAssets assets;
  assets.item_atlas = std::move(item_atlas.value());
  if (const asset::TextureAtlasEntry* crosshair =
          assets.item_atlas.find(GameplayCrosshairTextureAsset);
      crosshair != nullptr) {
    assets.crosshair_image = image_data_from_atlas_entry(
        *crosshair,
        assets.item_atlas.pages.front().image.width,
        assets.item_atlas.pages.front().image.height,
        render::UiTextureSource::ItemAtlas);
  }
  return ClientUiTextureAssetsResult::success(std::move(assets));
}

bool sync_ui_font_atlas(
    asset::FontSystem& font_system,
    FunctionRef<bool(const render::TextureArrayUploadData&)> upload) {
  if (!font_system.atlas_dirty()) {
    return true;
  }
  if (!upload) {
    return false;
  }
  const render::TextureArrayUploadData atlas_upload =
      texture_array_upload_from_image(font_system.atlas_image());
  if (!upload(atlas_upload)) {
    return false;
  }
  font_system.clear_atlas_dirty();
  return true;
}

UiDrawCommandAppendResult append_ui_draw_commands_with_font_sync(
    const ui::UiTree& tree,
    asset::FontSystem& font_system,
    const ui::UiFontStacks& font_stacks,
    render::FramePacket& frame,
    const ui::UiDisplayMetrics& metrics,
    f32 time_seconds,
    FunctionRef<bool()> sync_font_atlas) {
  const usize rect_count = frame.ui_rects.size();
  const usize textured_quad_count = frame.ui_textured_quads.size();
  const usize glyph_count = frame.ui_glyphs.size();

  ui::append_ui_draw_commands(
      tree,
      font_system,
      font_stacks,
      frame,
      metrics,
      time_seconds);

  const bool synced = sync_font_atlas && sync_font_atlas();
  if (!synced) {
    frame.ui_rects.resize(rect_count);
    frame.ui_textured_quads.resize(textured_quad_count);
    frame.ui_glyphs.resize(glyph_count);
  }

  return {
      .ui_rects_appended = frame.ui_rects.size() - rect_count,
      .ui_textured_quads_appended =
          frame.ui_textured_quads.size() - textured_quad_count,
      .ui_glyphs_appended = frame.ui_glyphs.size() - glyph_count,
      .font_atlas_synced = synced,
      .rolled_back = !synced};
}

} // namespace arv::client
