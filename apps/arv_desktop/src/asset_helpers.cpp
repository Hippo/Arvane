#include "asset_helpers.hpp"

#include "arv/client/render_asset_upload.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

std::array<Vec2f, 4> normalized_uvs(
    const std::array<Vec2i, 4>& pixels,
    u32 width,
    u32 height) {
  const f32 inv_width = width == 0U ? 1.0F : 1.0F / static_cast<f32>(width);
  const f32 inv_height = height == 0U ? 1.0F : 1.0F / static_cast<f32>(height);
  std::array<Vec2f, 4> uvs{};
  for (usize index = 0; index < uvs.size(); ++index) {
    uvs[index] = {
        static_cast<f32>(pixels[index].x) * inv_width,
        static_cast<f32>(pixels[index].y) * inv_height};
  }
  return uvs;
}

ui::UiImageData image_data_from_atlas_entry(
    const asset::TextureAtlasEntry& entry,
    u32 page_width,
    u32 page_height,
    render::UiTextureSource source) {
  const std::array<Vec2i, 4> pixels{{
      entry.uv_min,
      {entry.uv_max.x, entry.uv_min.y},
      entry.uv_max,
      {entry.uv_min.x, entry.uv_max.y},
  }};

  ui::UiImageData image;
  image.valid = true;
  image.source = source;
  image.page = entry.page;
  image.uvs = normalized_uvs(pixels, page_width, page_height);
  return image;
}

asset::TextureAtlasResult build_item_texture_atlas(
    asset::ResourceManager& resources,
    const player::ItemRegistry& item_registry,
    asset::TextureAtlasBuildOptions options) {
  std::vector<asset::AssetID> texture_ids;
  HashSet<std::string> seen;
  const auto add_texture = [&](std::string texture) {
    if (!texture.empty() && seen.insert(texture).second) {
      texture_ids.emplace_back(std::move(texture));
    }
  };
  add_texture("arvane:texture/missing");
  for (u32 index = 0; index < item_registry.item_count(); ++index) {
    const player::ItemRecord* item = item_registry.item(player::ItemID{index});
    if (item == nullptr || item->render_type != player::ItemRenderType::Texture) {
      continue;
    }
    add_texture(item->texture_asset);
  }

  std::vector<asset::TextureAtlasInput> inputs;
  inputs.reserve(texture_ids.size());
  for (const asset::AssetID& texture_id : texture_ids) {
    auto handle = texture_id.value == "arvane:texture/missing"
                      ? asset::TextureLoadResult::success(resources.missing_texture())
                      : resources.load_texture(texture_id);
    if (!handle) {
      return asset::TextureAtlasResult::failure(handle.error());
    }

    const asset::TextureResource* resource = resources.texture(handle.value());
    if (resource == nullptr) {
      return asset::TextureAtlasResult::failure({texture_id.value,
                                                 {},
                                                 "build item texture atlas",
                                                 "loaded texture handle did not resolve"});
    }
    inputs.emplace_back(texture_id, resource->image, resource->fallback);
  }

  return asset::build_texture_atlas(inputs, options);
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
