#include "arv/client/client_gameplay_assets.hpp"

#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] ClientGameplayAssetsResult gameplay_asset_failure(
    ClientGameplayAssetLoadStage stage,
    std::string category,
    std::string operation,
    std::string message) {
  return ClientGameplayAssetsResult::failure(
      {.stage = stage,
       .category = std::move(category),
       .operation = std::move(operation),
       .message = std::move(message)});
}

} // namespace

ClientGameplayAssetsResult load_client_gameplay_assets(
    const asset::PackManager& packs,
    asset::ResourceManager& resources,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    std::span<const asset::BlockModelAssetData> block_models,
    ClientGameplayAssetLoadOptions options) {
  auto entity_assets = load_client_entity_render_assets(
      packs,
      resources,
      block_registry,
      block_render_data,
      block_models,
      options.entity_assets);
  if (!entity_assets) {
    return gameplay_asset_failure(
        ClientGameplayAssetLoadStage::EntityRenderAssets,
        entity_assets.error().category,
        "load entity assets",
        entity_assets.error().message);
  }

  auto item_animation_library = load_item_animation_library_from_packs(
      packs,
      entity_assets.value().item_registry);
  if (!item_animation_library) {
    return gameplay_asset_failure(
        ClientGameplayAssetLoadStage::ItemAnimations,
        "item_animations",
        item_animation_library.error().operation,
        item_animation_library.error().message);
  }

  auto ui_texture_assets = load_client_ui_texture_assets(
      resources,
      entity_assets.value().item_registry,
      options.ui_texture_atlas_options);
  if (!ui_texture_assets) {
    return gameplay_asset_failure(
        ClientGameplayAssetLoadStage::UiTextureAssets,
        "assets",
        ui_texture_assets.error().operation,
        ui_texture_assets.error().message);
  }

  return ClientGameplayAssetsResult::success(
      {.entity_assets = std::move(entity_assets.value()),
       .item_animation_library = std::move(item_animation_library.value()),
       .ui_texture_assets = std::move(ui_texture_assets.value())});
}

} // namespace arv::client
