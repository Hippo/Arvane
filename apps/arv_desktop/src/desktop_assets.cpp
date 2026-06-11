#include "desktop_assets.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/client/particles.hpp"
#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/persistence/save.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/text/language.hpp"
#include "arv/world/worldgen/loader.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
    const arv::text::LanguageCatalog& language,
    voxel::BlockRegistry& registry) {
  for (u32 index = 0; index < registry.block_count(); ++index) {
    const voxel::BlockID block_id{index};
    const voxel::BlockRecord* block = registry.block(block_id);
    if (block == nullptr) {
      continue;
    }
    const std::string key = arv::text::block_display_name_key(block->name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return DesktopAssetLoadError{"lang", "missing language key: " + key};
    }
    registry.set_display_name(block_id, std::string{text.value()});
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<DesktopAssetLoadError> apply_item_language(
    const arv::text::LanguageCatalog& language,
    const voxel::BlockRegistry& blocks,
    player::ItemRegistry& registry) {
  for (u32 index = 0; index < registry.item_count(); ++index) {
    const player::ItemID item_id{index};
    const player::ItemRecord* item = registry.item(item_id);
    if (item == nullptr) {
      continue;
    }
    const std::string key = arv::text::item_display_name_key(item->name);
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
    const arv::text::LanguageCatalog& language,
    entity::EntityRegistry& registry) {
  for (const entity::EntityTypeRecord& entity : registry.records()) {
    const std::string key = arv::text::entity_display_name_key(entity.name);
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

[[nodiscard]] arv::text::LanguageResult load_pack_language(
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
  auto fallback = arv::text::load_language_files(fallback_paths, fallback_locale);
  if (!fallback) {
    return fallback;
  }

  arv::text::LanguageCatalog catalog = std::move(fallback.value());
  if (locale != fallback_locale) {
    std::vector<std::filesystem::path> selected_paths =
        paths_only(language_paths_for(locale));
    auto selected = arv::text::load_language_files(selected_paths, locale);
    if (!selected) {
      return selected;
    }
    catalog.merge_from(std::move(selected.value()));
  }
  if (auto messages = packs.load_config_text("arvane:messages", ".yaml");
      messages) {
    auto config_messages = arv::text::parse_language_yaml(
        messages.value(),
        catalog.locale());
    if (!config_messages) {
      return config_messages;
    }
    catalog.merge_from(std::move(config_messages.value()));
  }
  catalog.set_locale(std::move(locale));
  return arv::text::LanguageResult::success(std::move(catalog));
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

[[nodiscard]] std::vector<std::string> block_particle_texture_assets(
    const voxel::BlockRegistry& registry) {
  std::vector<std::string> textures;
  for (u32 index = 0; index < registry.block_count(); ++index) {
    const voxel::BlockRecord* block = registry.block(voxel::BlockID{index});
    if (block == nullptr || block->particle_texture_asset.empty()) {
      continue;
    }
    if (std::find(textures.begin(), textures.end(), block->particle_texture_asset) ==
        textures.end()) {
      textures.push_back(block->particle_texture_asset);
    }
  }
  return textures;
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
    const ui::UiTheme& theme,
    const ui::UiDisplayMetrics& ui_metrics,
    const voxel::BlockBehaviorRegistry& block_behaviors,
    const player::ItemBehaviorRegistry& item_behaviors,
    const world::EntityGoalRegistry& entity_goals,
    DesktopAssetLoadOptions options) {
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
          ui_metrics.font_pixel_size(theme.typography.size_sm),
          loaded.ui_font_stacks.small,
          false);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.default_font,
          ui_metrics.font_pixel_size(theme.typography.size_md),
          loaded.ui_font_stacks.body,
          true);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.heading_font,
          ui_metrics.font_pixel_size(theme.typography.size_lg),
          loaded.ui_font_stacks.heading,
          false);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.heading_font,
          ui_metrics.font_pixel_size(theme.typography.size_xl),
          loaded.ui_font_stacks.title,
          false);
      !font_result) {
    return DesktopLoadedAssetsResult::failure(font_result.error());
  }
  if (auto font_result = load_ui_font_stack(
          resources,
          loaded.font_system,
          theme.typography.monospace_font,
          ui_metrics.font_pixel_size(theme.typography.size_md),
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

  const auto block_behavior_exists = [&block_behaviors](std::string_view id) {
    return block_behaviors.contains(id);
  };
  voxel::BlockRegistryBuildOptions block_options;
  if (options.validate_behavior_ids) {
    block_options.behavior_exists =
        FunctionRef<bool(std::string_view)>{block_behavior_exists};
  }
  auto block_registry_result =
      voxel::BlockRegistry::build(
          std::move(block_definitions.value()),
          block_options);
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
  auto fluid_render_result = base::register_base_fluid_render_data(
      loaded.fluid_render_data,
      loaded.block_registry);
  if (!fluid_render_result) {
    return asset_failure("fluids", fluid_render_result.error());
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

  const auto item_behavior_exists = [&item_behaviors](std::string_view id) {
    return item_behaviors.contains(id);
  };
  player::ItemRegistryBuildOptions item_options;
  if (options.validate_behavior_ids) {
    item_options.behavior_exists =
        FunctionRef<bool(std::string_view)>{item_behavior_exists};
  }
  item_options.default_block_item_behavior_id =
      std::string{base::ids::BlockItemBehaviorID};
  auto item_registry_result = player::ItemRegistry::build_from_blocks_and_items(
      loaded.block_registry,
      std::move(item_definitions.value()),
      item_options);
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
      base::build_base_entity_registry(
          std::move(entity_definitions.value()),
          options.validate_behavior_ids ? &entity_goals : nullptr,
          options.validate_behavior_ids);
  if (!entity_registry_result) {
    return asset_failure("entities", entity_registry_result.error().message);
  }
  loaded.entity_registry = std::move(entity_registry_result.value());
  if (auto error = apply_entity_language(loaded.language, loaded.entity_registry);
      error.has_value()) {
    return DesktopLoadedAssetsResult::failure(error.value());
  }

  auto vehicle_definition_text = packs.load_merged_json_array_text("vehicles", "vehicles");
  if (!vehicle_definition_text) {
    return asset_failure("vehicles", vehicle_definition_text.error().describe());
  }
  auto vehicle_definitions =
      entity::parse_vehicle_definitions_json(vehicle_definition_text.value());
  if (!vehicle_definitions) {
    return asset_failure("vehicles", vehicle_definitions.error().message);
  }
  auto vehicle_registry_result =
      entity::VehicleDefinitionRegistry::build(std::move(vehicle_definitions.value()));
  if (!vehicle_registry_result) {
    return asset_failure("vehicles", vehicle_registry_result.error().message);
  }
  loaded.vehicle_registry = std::move(vehicle_registry_result.value());

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

  std::vector<std::string> block_particle_textures =
      block_particle_texture_assets(loaded.block_registry);
  auto particle_assets_result = client::load_particle_assets_from_packs(
      packs,
      resources,
      client::default_particle_atlas_options(),
      block_particle_textures);
  if (!particle_assets_result) {
    return asset_failure("particles", particle_assets_result.error().describe());
  }
  loaded.particle_assets = std::move(particle_assets_result.value());
  if (loaded.particle_assets.atlas.pages.empty()) {
    return asset_failure("particles", "particle texture atlas did not produce a page");
  }
  renderer.set_particle_texture_atlas(
      texture_array_upload_from_atlas(loaded.particle_assets.atlas));

  if (options.load_worldgen) {
    auto worldgen_registry = world::worldgen::load_worldgen_registry_from_packs(packs);
    if (!worldgen_registry) {
      return asset_failure("worldgen", worldgen_registry.error().describe());
    }

    auto resolved_worldgen = world::worldgen::resolve_dimension(
        worldgen_registry.value(),
        base::ids::OverworldWorldgenDataID,
        loaded.block_registry);
    if (!resolved_worldgen) {
      return asset_failure("worldgen", resolved_worldgen.error().describe());
    }
    loaded.default_generation_config =
        world::config_from_resolved_dimension(std::move(resolved_worldgen.value()));
  }

  return DesktopLoadedAssetsResult::success(std::move(loaded));
}

namespace {

[[nodiscard]] DesktopAssetValidationResult registry_validation_failure(
    std::string message) {
  return DesktopAssetValidationResult::failure(
      {"registry", std::move(message)});
}

[[nodiscard]] const io::DataValue::Array* snapshot_array(
    const io::DataValue& root,
    std::string_view key) {
  const io::DataValue* value = root.get(key);
  return value == nullptr ? nullptr : value->array_if();
}

[[nodiscard]] bool numeric_id_matches(
    const io::DataValue& entry,
    u32 expected) {
  const io::DataValue* numeric_id = entry.get("numeric_id");
  return numeric_id != nullptr &&
         io::is_integer_value(*numeric_id) &&
         numeric_id->as_u64_or(UINT64_MAX) == expected;
}

[[nodiscard]] std::optional<std::string> resource_id_from(
    const io::DataValue& entry) {
  const io::DataValue* resource_id = entry.get("resource_id");
  if (resource_id == nullptr || !io::is_string_value(*resource_id)) {
    return std::nullopt;
  }
  return std::string{resource_id->as_string_or({})};
}

} // namespace

DesktopAssetValidationResult validate_desktop_assets_against_registry_snapshot(
    std::string_view snapshot_json,
    const DesktopLoadedAssets& assets) {
  auto decoded = io::decode_json_value(snapshot_json);
  if (!decoded) {
    return registry_validation_failure(
        "failed to parse registry snapshot: " + decoded.error().describe());
  }
  const io::DataValue& root = decoded.value().root;
  const io::DataValue* format = root.get("format_version");
  if (format == nullptr || !io::is_integer_value(*format) ||
      format->as_u64_or(0U) != 1U) {
    return registry_validation_failure("unsupported registry snapshot format_version");
  }

  const io::DataValue::Array* blocks = snapshot_array(root, "blocks");
  if (blocks == nullptr || blocks->size() != assets.block_registry.block_count()) {
    return registry_validation_failure("block registry snapshot count mismatch");
  }
  for (const io::DataValue& entry : *blocks) {
    const std::optional<std::string> name = resource_id_from(entry);
    if (!name.has_value()) {
      return registry_validation_failure("block snapshot entry is missing resource_id");
    }
    const std::optional<voxel::BlockID> id = assets.block_registry.find_block(name.value());
    if (!id.has_value() || !numeric_id_matches(entry, id->value)) {
      return registry_validation_failure("block registry numeric ID mismatch for " + name.value());
    }
  }

  const io::DataValue::Array* variants = snapshot_array(root, "block_variants");
  if (variants == nullptr || variants->size() != assets.block_registry.variant_count()) {
    return registry_validation_failure("block variant registry snapshot count mismatch");
  }
  for (const io::DataValue& entry : *variants) {
    const io::DataValue* debug_name = entry.get("debug_name");
    if (debug_name == nullptr || !io::is_string_value(*debug_name)) {
      return registry_validation_failure("block variant snapshot entry is missing debug_name");
    }
    const std::string name{debug_name->as_string_or({})};
    const std::optional<voxel::BlockVariantID> id =
        assets.block_registry.find_variant(name);
    if (!id.has_value() || !numeric_id_matches(entry, id->value)) {
      return registry_validation_failure("block variant numeric ID mismatch for " + name);
    }
  }

  const io::DataValue::Array* items = snapshot_array(root, "items");
  if (items == nullptr || items->size() != assets.item_registry.item_count()) {
    return registry_validation_failure("item registry snapshot count mismatch");
  }
  for (const io::DataValue& entry : *items) {
    const std::optional<std::string> name = resource_id_from(entry);
    if (!name.has_value()) {
      return registry_validation_failure("item snapshot entry is missing resource_id");
    }
    const std::optional<player::ItemID> id = assets.item_registry.find_item(name.value());
    if (!id.has_value() || !numeric_id_matches(entry, id->value)) {
      return registry_validation_failure("item registry numeric ID mismatch for " + name.value());
    }
  }

  const io::DataValue::Array* entities = snapshot_array(root, "entities");
  if (entities == nullptr || entities->size() != assets.entity_registry.type_count()) {
    return registry_validation_failure("entity registry snapshot count mismatch");
  }
  for (const io::DataValue& entry : *entities) {
    const std::optional<std::string> name = resource_id_from(entry);
    if (!name.has_value()) {
      return registry_validation_failure("entity snapshot entry is missing resource_id");
    }
    const std::optional<EntityTypeID> id = assets.entity_registry.find_type(name.value());
    if (!id.has_value() || !numeric_id_matches(entry, id->value)) {
      return registry_validation_failure("entity registry numeric ID mismatch for " + name.value());
    }
  }

  return DesktopAssetValidationResult::success();
}

} // namespace arv::desktop
