#include "desktop_assets.hpp"

#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/persistence/save.hpp"
#include "arv/render/opengl_renderer.hpp"

#include <filesystem>
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

} // namespace

DesktopLoadedAssetsResult load_desktop_assets(
    asset::ResourceManager& resources,
    render::OpenGLRenderer& renderer,
    const io::EngineConfig& config) {
  DesktopLoadedAssets loaded;

  auto block_definition_text = io::read_text_file(
      config.paths.config_path() / "blocks.json",
      "startup block definitions");
  if (!block_definition_text) {
    return asset_failure("blocks", block_definition_text.error().message);
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
  loaded.block_registry_hash = persistence::hash_block_registry(loaded.block_registry);

  auto block_atlas_result = asset::build_block_texture_atlas(resources, loaded.block_registry);
  if (!block_atlas_result) {
    return asset_failure("assets", block_atlas_result.error().message);
  }
  loaded.block_atlas = std::move(block_atlas_result.value());
  if (loaded.block_atlas.atlas.pages.empty()) {
    return asset_failure("assets", "block texture atlas did not produce a page");
  }
  render::TextureUploadData terrain_atlas_upload =
      texture_upload_from_image(loaded.block_atlas.atlas.pages.front().image);
  terrain_atlas_upload.limit_mipmap_levels = true;
  terrain_atlas_upload.max_mipmap_level = loaded.block_atlas.atlas.mip_safe_max_level;
  renderer.set_chunk_texture_atlas(std::move(terrain_atlas_upload));

  auto ui_font = loaded.font_system.load_face(
      resources.resolve_asset_path({"arvane:font/font"}, asset::AssetKind::Font),
      18,
      {"arvane:font/font"});
  if (!ui_font) {
    return asset_failure("assets", ui_font.error().message);
  }
  std::vector<asset::FontFaceHandle> ui_faces{ui_font.value()};
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
    auto face = loaded.font_system.load_face(path, 18, fallback);
    if (face) {
      ui_faces.emplace_back(face.value());
    } else {
      core::warn("assets", face.error().message);
    }
  }
  loaded.ui_font_stack = loaded.font_system.make_stack(ui_faces);

  auto item_definition_text = io::read_text_file(
      config.paths.config_path() / "items.json",
      "startup item definitions");
  if (!item_definition_text) {
    return asset_failure("items", item_definition_text.error().message);
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
  loaded.item_registry_hash = persistence::hash_item_registry(loaded.item_registry);

  auto item_atlas_result = build_item_texture_atlas(resources, loaded.item_registry);
  if (!item_atlas_result) {
    return asset_failure("assets", item_atlas_result.error().message);
  }
  loaded.item_atlas = std::move(item_atlas_result.value());
  if (loaded.item_atlas.pages.empty()) {
    return asset_failure("assets", "item texture atlas did not produce a page");
  }
  renderer.set_ui_item_atlas(texture_upload_from_image(loaded.item_atlas.pages.front().image));

  auto entity_definition_text = io::read_text_file(
      config.paths.config_path() / "entities.json",
      "startup entity definitions");
  if (!entity_definition_text) {
    return asset_failure("entities", entity_definition_text.error().message);
  }
  auto entity_definitions =
      entity::parse_entity_definitions_json(entity_definition_text.value());
  if (!entity_definitions) {
    return asset_failure("entities", entity_definitions.error().message);
  }
  auto entity_registry_result =
      entity::EntityRegistry::build_with_builtins(std::move(entity_definitions.value()));
  if (!entity_registry_result) {
    return asset_failure("entities", entity_registry_result.error().message);
  }
  loaded.entity_registry = std::move(entity_registry_result.value());

  auto entity_render_assets_result = build_entity_render_assets(
      resources,
      loaded.entity_registry,
      loaded.item_registry,
      loaded.block_registry);
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
    render::TextureUploadData entity_atlas_upload =
        texture_upload_from_image(loaded.entity_render_assets.texture_atlas.pages.front().image);
    entity_atlas_upload.limit_mipmap_levels = true;
    entity_atlas_upload.max_mipmap_level =
        loaded.entity_render_assets.texture_atlas.mip_safe_max_level;
    renderer.set_entity_texture_atlas(std::move(entity_atlas_upload));
    for (const render::EntityModelAsset& model : loaded.entity_render_assets.models) {
      renderer.set_entity_model(model);
    }
  }

  auto worldgen_text = io::read_text_file(
      config.paths.config_path() / "worldgen/overworld.yaml",
      "overworld generator definition");
  if (!worldgen_text) {
    return asset_failure("worldgen", worldgen_text.error().message);
  }

  auto worldgen_config = world::parse_world_generation_yaml(worldgen_text.value());
  if (!worldgen_config) {
    return asset_failure("worldgen", worldgen_config.error().message);
  }
  loaded.default_generation_config = std::move(worldgen_config.value());

  return DesktopLoadedAssetsResult::success(std::move(loaded));
}

} // namespace arv::desktop
