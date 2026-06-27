#include "desktop_assets.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/client/block_render_assets.hpp"
#include "arv/client/client_gameplay_assets.hpp"
#include "arv/client/client_ui_assets.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/client/particles.hpp"
#include "arv/core/log.hpp"
#include "arv/crafting/smelting.hpp"
#include "arv/io/config.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/persistence/save.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/text/language.hpp"
#include "arv/world/space_definition.hpp"
#include "arv/world/worldgen/loader.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
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

  auto language_result = client::load_pack_language(
      packs,
      config.language.locale,
      config.language.fallback_locale);
  if (!language_result) {
    return asset_failure("lang", language_result.error().message);
  }
  loaded.language = std::move(language_result.value());

  core::info("assets", "loading UI font stacks");
  const auto font_warning = [](const client::UiFontFallbackWarning& warning) {
    core::warn("assets", warning.message);
  };
  auto font_stacks_result = client::load_ui_font_stacks(
      resources,
      loaded.font_system,
      theme,
      ui_metrics,
      FunctionRef<void(const client::UiFontFallbackWarning&)>{font_warning});
  if (!font_stacks_result) {
    return DesktopLoadedAssetsResult::failure(
        {font_stacks_result.error().category, font_stacks_result.error().message});
  }
  loaded.ui_font_stacks = std::move(font_stacks_result.value());
  core::info("assets", "loaded UI font stacks");

  const auto block_behavior_exists = [&block_behaviors](std::string_view id) {
    return block_behaviors.contains(id);
  };
  const auto register_fluid_render_data =
      [](render::FluidRenderData& fluid_render_data,
         const voxel::BlockRegistry& block_registry)
      -> Expected<void, std::string> {
    return base::register_base_fluid_render_data(
        fluid_render_data,
        block_registry);
  };
  client::ClientBlockRenderAssetLoadOptions block_asset_options;
  if (options.validate_behavior_ids) {
    block_asset_options.block_behavior_exists =
        FunctionRef<bool(std::string_view)>{block_behavior_exists};
  }
  block_asset_options.register_fluid_render_data =
      FunctionRef<Expected<void, std::string>(
          render::FluidRenderData&,
          const voxel::BlockRegistry&)>{register_fluid_render_data};
  block_asset_options.atlas_options =
      atlas_build_options_from(config.render.atlases.blocks, 4, false);

  core::info("assets", "loading block render assets");
  auto block_assets_result = client::load_client_block_render_assets(
      packs,
      resources,
      block_asset_options);
  if (!block_assets_result) {
    return asset_failure(
        block_assets_result.error().category,
        block_assets_result.error().message);
  }
  client::ClientBlockRenderAssets block_assets =
      std::move(block_assets_result.value());
  loaded.block_registry = std::move(block_assets.block_registry);
  if (auto names = client::apply_client_block_display_names(
          loaded.language,
          loaded.block_registry);
      !names) {
    return asset_failure(names.error().category, names.error().message);
  }
  std::vector<asset::BlockModelAssetData> block_models =
      std::move(block_assets.block_models);
  loaded.container_types = std::move(block_assets.container_types);
  loaded.container_layouts = std::move(block_assets.container_layouts);
  loaded.block_atlas = std::move(block_assets.block_atlas);
  loaded.fluid_render_data = std::move(block_assets.fluid_render_data);
  loaded.block_registry_hash = persistence::hash_block_registry(loaded.block_registry);
  render::TextureArrayUploadData terrain_atlas_upload =
      client::texture_array_upload_from_atlas(loaded.block_atlas.atlas);
  renderer.set_chunk_texture_atlas(std::move(terrain_atlas_upload));
  core::info("assets", "loaded block render assets");

  const auto item_behavior_exists = [&item_behaviors](std::string_view id) {
    return item_behaviors.contains(id);
  };
  const bool validate_behavior_ids = options.validate_behavior_ids;
  const auto build_entity_registry =
      [&entity_goals, validate_behavior_ids](
          std::vector<entity::EntityDefinition> definitions)
      -> entity::EntityRegistry::Result {
    return base::build_base_entity_registry(
        std::move(definitions),
        validate_behavior_ids ? &entity_goals : nullptr,
        validate_behavior_ids);
  };
  client::ClientEntityRenderAssetLoadOptions entity_asset_options;
  if (options.validate_behavior_ids) {
    entity_asset_options.item_behavior_exists =
        FunctionRef<bool(std::string_view)>{item_behavior_exists};
  }
  entity_asset_options.build_entity_registry =
      FunctionRef<entity::EntityRegistry::Result(
          std::vector<entity::EntityDefinition>)>{build_entity_registry};
  entity_asset_options.default_block_item_behavior_id =
      std::string{base::ids::BlockItemBehaviorID};
  entity_asset_options.outline_config = client::BlockOutlineRenderConfig{
      {config.render.block_outline.color_r,
       config.render.block_outline.color_g,
       config.render.block_outline.color_b},
      config.render.block_outline.thickness};
  entity_asset_options.atlas_options =
      atlas_build_options_from(config.render.atlases.entities, 2, false);

  client::ClientGameplayAssetLoadOptions gameplay_asset_options;
  gameplay_asset_options.entity_assets = entity_asset_options;
  gameplay_asset_options.ui_texture_atlas_options =
      atlas_build_options_from(config.render.atlases.ui, 2, true);
  auto gameplay_assets_result = client::load_client_gameplay_assets(
      packs,
      resources,
      loaded.block_registry,
      loaded.block_atlas.render_data,
      block_models,
      gameplay_asset_options);
  if (!gameplay_assets_result) {
    return asset_failure(
        gameplay_assets_result.error().category,
        gameplay_assets_result.error().message);
  }
  client::ClientGameplayAssets gameplay_assets =
      std::move(gameplay_assets_result.value());
  client::ClientEntityRenderAssets entity_assets =
      std::move(gameplay_assets.entity_assets);
  loaded.item_registry = std::move(entity_assets.item_registry);
  if (auto names = client::apply_client_item_display_names(
          loaded.language,
          loaded.block_registry,
          loaded.item_registry);
      !names) {
    return asset_failure(names.error().category, names.error().message);
  }
  loaded.item_registry_hash = persistence::hash_item_registry(loaded.item_registry);

  auto crafting_result =
      crafting::load_crafting_registry_from_packs(packs, loaded.item_registry);
  if (!crafting_result) {
    return asset_failure("crafting", crafting_result.error().message);
  }
  for (const crafting::CraftingLoadDiagnostic& diagnostic :
       crafting_result.value().diagnostics) {
    core::warn("assets", diagnostic.describe());
  }
  loaded.crafting_registry = std::move(crafting_result.value().registry);
  auto smelting_result =
      crafting::load_smelting_rule_registry_from_packs(packs, loaded.item_registry);
  if (!smelting_result) {
    return asset_failure("smelting", smelting_result.error().message);
  }
  loaded.smelting_registry = std::move(smelting_result.value());

  loaded.item_animation_library =
      std::move(gameplay_assets.item_animation_library);

  client::ClientUiTextureAssets ui_assets =
      std::move(gameplay_assets.ui_texture_assets);
  loaded.item_atlas = std::move(ui_assets.item_atlas);
  loaded.crosshair_image = ui_assets.crosshair_image;
  renderer.set_ui_item_atlas(
      client::texture_array_upload_from_atlas(loaded.item_atlas));

  loaded.entity_registry = std::move(entity_assets.entity_registry);
  if (auto names = client::apply_client_entity_display_names(
          loaded.language,
          loaded.entity_registry);
      !names) {
    return asset_failure(names.error().category, names.error().message);
  }
  loaded.vehicle_registry = std::move(entity_assets.vehicle_registry);
  loaded.entity_render_assets = std::move(entity_assets.render_assets);
  if (!loaded.entity_render_assets.models.empty()) {
    if (loaded.entity_render_assets.texture_atlas.pages.empty()) {
      return asset_failure(
          "entities",
          "entity models were loaded without an entity texture atlas");
    }
    render::TextureArrayUploadData entity_atlas_upload =
        client::texture_array_upload_from_atlas(
            loaded.entity_render_assets.texture_atlas);
    renderer.set_entity_texture_atlas(std::move(entity_atlas_upload));
    const auto upload_model = [&renderer](const render::EntityModelAsset& model) {
      renderer.set_entity_model(model);
    };
    const auto upload_animation =
        [&renderer](const render::EntityAnimationAsset& animation) {
      renderer.set_entity_animation(animation);
    };
    (void)client::upload_entity_render_asset_records(
        loaded.entity_render_assets.models,
        loaded.entity_render_assets.animations,
        {.upload_model =
             FunctionRef<void(const render::EntityModelAsset&)>{upload_model},
         .upload_animation =
             FunctionRef<void(const render::EntityAnimationAsset&)>{
                 upload_animation}});
  }

  std::vector<std::string> block_particle_textures =
      client::block_particle_texture_assets(loaded.block_registry);
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
      client::texture_array_upload_from_atlas(loaded.particle_assets.atlas));

  if (options.load_worldgen) {
    auto worldgen_registry = world::worldgen::load_worldgen_registry_from_packs(packs);
    if (!worldgen_registry) {
      return asset_failure("worldgen", worldgen_registry.error().describe());
    }

    auto registry_ptr =
        std::make_shared<world::worldgen::WorldGenRegistry>(
            std::move(worldgen_registry.value()));
    auto resolved_worldgen = world::worldgen::resolve_dimension(
        *registry_ptr,
        base::ids::OverworldWorldgenDataID,
        loaded.block_registry);
    if (!resolved_worldgen) {
      return asset_failure("worldgen", resolved_worldgen.error().describe());
    }
    loaded.default_generation_config =
        world::config_from_resolved_dimension(std::move(resolved_worldgen.value()));
    loaded.worldgen_registry = std::move(registry_ptr);
  }

  auto space_definitions = world::load_space_definition_registry_from_packs(packs);
  if (!space_definitions) {
    return asset_failure("space", space_definitions.error().describe());
  }
  loaded.space_definitions = std::move(space_definitions.value());
  core::info(
      "assets",
      "loaded space definitions: worlds={} environments={} bodies={}",
      loaded.space_definitions.world_count(),
      loaded.space_definitions.environment_count(),
      loaded.space_definitions.space_body_count());

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
