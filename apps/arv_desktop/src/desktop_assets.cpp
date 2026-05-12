#include "desktop_assets.hpp"

#include "arv/core/log.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/io/config.hpp"
#include "arv/lang/language.hpp"
#include "arv/persistence/save.hpp"
#include "arv/render/render_commands.hpp"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace arv::desktop {
namespace {

[[nodiscard]] DesktopLoadedAssetsResult asset_failure(
    std::string category,
    std::string message) {
  return DesktopLoadedAssetsResult::failure(
      {std::move(category), std::move(message)});
}

[[nodiscard]] std::optional<DesktopAssetLoadError> apply_block_language(
    const lang::LanguageCatalog& language,
    voxel::BlockRegistry& registry) {
  for (u32 index = 0; index < registry.block_count(); ++index) {
    const voxel::BlockID block_id{index};
    const voxel::BlockRecord* block = registry.block(block_id);
    if (block == nullptr) {
      continue;
    }
    const std::string key = lang::block_display_name_key(block->name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return DesktopAssetLoadError{"lang", "missing language key: " + key};
    }
    registry.set_display_name(block_id, std::string{text.value()});
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<DesktopAssetLoadError> apply_item_language(
    const lang::LanguageCatalog& language,
    const voxel::BlockRegistry& blocks,
    player::ItemRegistry& registry) {
  for (u32 index = 0; index < registry.item_count(); ++index) {
    const player::ItemID item_id{index};
    const player::ItemRecord* item = registry.item(item_id);
    if (item == nullptr) {
      continue;
    }
    const std::string key = lang::item_display_name_key(item->name);
    if (const std::optional<std::string_view> text = language.find(key);
        text.has_value()) {
      registry.set_display_name(item_id, std::string{text.value()});
      continue;
    }
    if (item->source_block.has_value()) {
      if (const voxel::BlockRecord* block = blocks.block(item->source_block.value());
          block != nullptr && !block->display_name.empty()) {
        registry.set_display_name(item_id, block->display_name);
        continue;
      }
    }
    return DesktopAssetLoadError{"lang", "missing language key: " + key};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<DesktopAssetLoadError> apply_entity_language(
    const lang::LanguageCatalog& language,
    entity::EntityRegistry& registry) {
  for (const entity::EntityTypeRecord& entity : registry.records()) {
    const std::string key = lang::entity_display_name_key(entity.name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return DesktopAssetLoadError{"lang", "missing language key: " + key};
    }
    registry.set_display_name(entity.id, std::string{text.value()});
  }
  return std::nullopt;
}

[[nodiscard]] render::PlayerSkinAtlasConfig player_skin_atlas_config_from(
    const io::RenderConfig::PlayerSkinAtlasConfig& source) noexcept {
  render::PlayerSkinAtlasConfig config;
  config.page_size = source.page_size;
  config.max_pages = source.max_pages;
  config.cleanup.interval_seconds = source.cleanup.interval_seconds;
  config.cleanup.evict_after_unused_seconds =
      source.cleanup.evict_after_unused_seconds;
  config.pressure_cleanup.enabled = source.pressure_cleanup.enabled;
  config.pressure_cleanup.usage_threshold = source.pressure_cleanup.usage_threshold;
  config.pressure_cleanup.target_usage = source.pressure_cleanup.target_usage;
  return config;
}

[[nodiscard]] lang::LanguageResult load_pack_language(
    const asset::PackManager& packs,
    std::string locale,
    std::string fallback_locale) {
  const auto language_paths_for = [&](std::string_view selected_locale) {
    return packs.data_files_at("lang/" + std::string{selected_locale} + ".yaml");
  };
  const auto paths_only = [](const std::vector<asset::PackDataFile>& files) {
    std::vector<std::filesystem::path> paths;
    paths.reserve(files.size());
    for (const asset::PackDataFile& file : files) {
      paths.push_back(file.path);
    }
    return paths;
  };

  std::vector<std::filesystem::path> fallback_paths =
      paths_only(language_paths_for(fallback_locale));
  auto fallback = lang::load_language_files(fallback_paths, fallback_locale);
  if (!fallback) {
    return fallback;
  }

  lang::LanguageCatalog catalog = std::move(fallback.value());
  if (locale != fallback_locale) {
    std::vector<std::filesystem::path> selected_paths =
        paths_only(language_paths_for(locale));
    auto selected = lang::load_language_files(selected_paths, locale);
    if (!selected) {
      return selected;
    }
    catalog.merge_from(std::move(selected.value()));
  }
  catalog.set_locale(std::move(locale));
  return lang::LanguageResult::success(std::move(catalog));
}

[[nodiscard]] asset::TextureAtlasBuildOptions atlas_build_options_from(
    const io::RenderConfig::StaticAtlasConfig& source,
    u32 padding,
    bool require_uniform_non_fallback_size) noexcept {
  return {
      .max_page_size = source.page_size,
      .max_pages = source.max_pages,
      .padding = padding,
      .require_uniform_non_fallback_size = require_uniform_non_fallback_size};
}

using UiFontStackLoadResult = Expected<void, DesktopAssetLoadError>;

[[nodiscard]] UiFontStackLoadResult load_ui_font_stack(
    asset::ResourceManager& resources,
    asset::FontSystem& font_system,
    std::string asset_name,
    u32 pixel_size,
    asset::FontStack& stack,
    bool include_fallbacks) {
  std::vector<asset::FontFaceHandle> faces;
  asset::AssetID primary_id{std::move(asset_name)};
  const std::filesystem::path primary_path =
      resources.resolve_asset_path(primary_id, asset::AssetKind::Font);
  auto primary = font_system.load_face(
      primary_path,
      pixel_size,
      primary_id);
  if (!primary) {
    return UiFontStackLoadResult::failure({"assets", primary.error().message});
  }
  faces.emplace_back(primary.value());

  if (include_fallbacks) {
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
      } else {
        core::warn("assets", face.error().message);
      }
    }
  }

  stack = font_system.make_stack(faces);
  return UiFontStackLoadResult::success();
}

} // namespace

DesktopLoadedAssetsResult load_desktop_assets(
    asset::ResourceManager& resources,
    render::RenderCommandQueue& renderer,
    const io::EngineConfig& config,
    const asset::PackManager& packs,
    const ui::UiTheme& theme) {
  DesktopLoadedAssets loaded;
  renderer.configure_player_skin_atlas(
      player_skin_atlas_config_from(config.render.atlases.player_skins));

  auto language_result = load_pack_language(
      packs,
      config.language.locale,
      config.language.fallback_locale);
  if (!language_result) {
    return asset_failure("lang", language_result.error().message);
  }
  loaded.language = std::move(language_result.value());

  core::info("assets", "loading UI font stacks");
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.default_font,
          theme.typography.size_sm,
          loaded.ui_font_stacks.small,
          false);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.default_font,
          theme.typography.size_md,
          loaded.ui_font_stacks.body,
          true);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.heading_font,
          theme.typography.size_lg,
          loaded.ui_font_stacks.heading,
          false);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.heading_font,
          theme.typography.size_xl,
          loaded.ui_font_stacks.title,
          false);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.monospace_font,
          theme.typography.size_md,
          loaded.ui_font_stacks.mono,
          false);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  core::info("assets", "loaded UI font stacks");

  auto block_definition_text = packs.load_merged_json_array_text("blocks", "blocks");
  if (!block_definition_text) {
    return asset_failure("blocks", block_definition_text.error().describe());
  }

  auto block_definitions = voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    return asset_failure("blocks", block_definitions.error().message);
  }

  auto block_registry_result =
      voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!block_registry_result) {
    return asset_failure("blocks", block_registry_result.error().message);
  }
  loaded.block_registry = std::move(block_registry_result.value());
  if (auto error = apply_block_language(loaded.language, loaded.block_registry);
      error.has_value()) {
    return DesktopLoadedAssetsResult::failure(error.value());
  }

  auto block_models_result = asset::load_block_model_definitions(resources, loaded.block_registry);
  if (!block_models_result) {
    return asset_failure("assets", block_models_result.error().message);
  }
  std::vector<asset::BlockModelAssetData> block_models =
      std::move(block_models_result.value());
  asset::apply_block_model_collision_boxes(loaded.block_registry, block_models);
  loaded.block_registry_hash = persistence::hash_block_registry(loaded.block_registry);

  core::info("assets", "building block texture atlas");
  auto block_atlas_result = asset::build_block_texture_atlas(
      resources,
      loaded.block_registry,
      block_models,
      atlas_build_options_from(config.render.atlases.blocks, 4, true));
  if (!block_atlas_result) {
    return asset_failure("assets", block_atlas_result.error().message);
  }
  loaded.block_atlas = std::move(block_atlas_result.value());
  if (loaded.block_atlas.atlas.pages.empty()) {
    return asset_failure("assets", "block texture atlas did not produce a page");
  }
  render::TextureArrayUploadData terrain_atlas_upload =
      texture_array_upload_from_atlas(loaded.block_atlas.atlas);
  renderer.set_chunk_texture_atlas(std::move(terrain_atlas_upload));

  auto item_definition_text = packs.load_merged_json_array_text("items", "items");
  if (!item_definition_text) {
    return asset_failure("items", item_definition_text.error().describe());
  }
  auto item_definitions = player::parse_item_definitions_json(item_definition_text.value());
  if (!item_definitions) {
    return asset_failure("items", item_definitions.error().message);
  }

  auto item_registry_result = player::ItemRegistry::build_from_blocks_and_items(
      loaded.block_registry,
      std::move(item_definitions.value()));
  if (!item_registry_result) {
    return asset_failure("items", item_registry_result.error().message);
  }
  loaded.item_registry = std::move(item_registry_result.value());
  if (auto error = apply_item_language(
          loaded.language,
          loaded.block_registry,
          loaded.item_registry);
      error.has_value()) {
    return DesktopLoadedAssetsResult::failure(error.value());
  }
  loaded.item_registry_hash = persistence::hash_item_registry(loaded.item_registry);

  auto item_animation_text =
      packs.load_merged_json_array_text("item_animations", "animations");
  if (!item_animation_text) {
    return asset_failure("item_animations", item_animation_text.error().describe());
  }
  auto item_animation_library = client::parse_item_animation_library_json(
      item_animation_text.value());
  if (!item_animation_library) {
    return asset_failure("item_animations", item_animation_library.error().message);
  }
  loaded.item_animation_library = std::move(item_animation_library.value());
  auto item_animation_references = client::validate_item_animation_references(
      loaded.item_registry,
      loaded.item_animation_library);
  if (!item_animation_references) {
    return asset_failure("item_animations", item_animation_references.error().message);
  }

  auto item_atlas_result = build_item_texture_atlas(
      resources,
      loaded.item_registry,
      atlas_build_options_from(config.render.atlases.ui, 2, true));
  if (!item_atlas_result) {
    return asset_failure("assets", item_atlas_result.error().message);
  }
  loaded.item_atlas = std::move(item_atlas_result.value());
  if (loaded.item_atlas.pages.empty()) {
    return asset_failure("assets", "item texture atlas did not produce a page");
  }
  renderer.set_ui_item_atlas(texture_array_upload_from_atlas(loaded.item_atlas));

  auto entity_definition_text = packs.load_merged_json_array_text("entities", "entities");
  if (!entity_definition_text) {
    return asset_failure("entities", entity_definition_text.error().describe());
  }
  auto entity_definitions =
      entity::parse_entity_definitions_json(entity_definition_text.value());
  if (!entity_definitions) {
    return asset_failure("entities", entity_definitions.error().message);
  }
  auto entity_registry_result =
      base::build_base_entity_registry(std::move(entity_definitions.value()));
  if (!entity_registry_result) {
    return asset_failure("entities", entity_registry_result.error().message);
  }
  loaded.entity_registry = std::move(entity_registry_result.value());
  if (auto error = apply_entity_language(loaded.language, loaded.entity_registry);
      error.has_value()) {
    return DesktopLoadedAssetsResult::failure(error.value());
  }

  auto entity_render_assets_result = build_entity_render_assets(
      resources,
      loaded.entity_registry,
      loaded.item_registry,
      loaded.block_registry,
      loaded.block_atlas.render_data,
      block_models,
      BlockOutlineRenderConfig{
          {config.render.block_outline.color_r,
           config.render.block_outline.color_g,
           config.render.block_outline.color_b},
          config.render.block_outline.thickness},
      atlas_build_options_from(config.render.atlases.entities, 2, false));
  if (!entity_render_assets_result) {
    return asset_failure("entities", entity_render_assets_result.error().message);
  }
  loaded.entity_render_assets = std::move(entity_render_assets_result.value());
  if (!loaded.entity_render_assets.models.empty()) {
    if (loaded.entity_render_assets.texture_atlas.pages.empty()) {
      return asset_failure(
          "entities",
          "entity models were loaded without an entity texture atlas");
    }
    render::TextureArrayUploadData entity_atlas_upload =
        texture_array_upload_from_atlas(loaded.entity_render_assets.texture_atlas);
    renderer.set_entity_texture_atlas(std::move(entity_atlas_upload));
    for (const render::EntityModelAsset& model : loaded.entity_render_assets.models) {
      renderer.set_entity_model(model);
    }
    for (const render::EntityAnimationAsset& animation : loaded.entity_render_assets.animations) {
      renderer.set_entity_animation(animation);
    }
  }

  auto worldgen_text = packs.load_data_text(base::ids::OverworldWorldgenDataID, ".yaml");
  if (!worldgen_text) {
    return asset_failure("worldgen", worldgen_text.error().describe());
  }

  auto worldgen_config = world::parse_world_generation_yaml(worldgen_text.value());
  if (!worldgen_config) {
    return asset_failure("worldgen", worldgen_config.error().message);
  }
  loaded.default_generation_config = std::move(worldgen_config.value());

  return DesktopLoadedAssetsResult::success(std::move(loaded));
}

} // namespace arv::desktop
