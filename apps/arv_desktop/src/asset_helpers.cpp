#include "asset_helpers.hpp"

#include "arv/client/render_asset_upload.hpp"

namespace arv::desktop {

render::TextureUploadData texture_upload_from_image(const asset::Image& image) {
  return client::texture_upload_from_image(image);
}

render::TextureArrayUploadData texture_array_upload_from_image(const asset::Image& image) {
  return client::texture_array_upload_from_image(image);
}

render::TextureArrayUploadData texture_array_upload_from_atlas(
    const asset::TextureAtlas& atlas) {
  return client::texture_array_upload_from_atlas(atlas);
}

EntityRenderAssetsResult build_entity_render_assets(
    asset::ResourceManager& resources,
    const entity::EntityRegistry& entity_registry,
    const player::ItemRegistry& item_registry,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    std::span<const asset::BlockModelAssetData> block_models,
    BlockOutlineRenderConfig outline_config,
    asset::TextureAtlasBuildOptions atlas_options) {
  return client::build_entity_render_assets(
      resources,
      entity_registry,
      item_registry,
      block_registry,
      block_render_data,
      block_models,
      outline_config,
      atlas_options);
}

} // namespace arv::desktop
