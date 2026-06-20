#include "arv/client/render_asset_upload.hpp"

#include <algorithm>
#include <cstring>

namespace arv::client {

render::TextureUploadData texture_upload_from_image(const asset::Image& image) {
  return {image.width, image.height, image.pixels};
}

render::TextureArrayUploadData texture_array_upload_from_image(
    const asset::Image& image) {
  return {image.width, image.height, image.empty() ? 0U : 1U, image.pixels};
}

render::TextureArrayUploadData texture_array_upload_from_atlas(
    const asset::TextureAtlas& atlas) {
  render::TextureArrayUploadData upload;
  upload.layers = static_cast<u32>(atlas.pages.size());
  upload.limit_mipmap_levels = atlas.mip_safe_max_level > 0U;
  upload.max_mipmap_level = atlas.mip_safe_max_level;
  for (const asset::TextureAtlasPage& page : atlas.pages) {
    upload.width = std::max(upload.width, page.image.width);
    upload.height = std::max(upload.height, page.image.height);
  }
  if (upload.width == 0U || upload.height == 0U || upload.layers == 0U) {
    return {};
  }

  const usize layer_bytes = static_cast<usize>(upload.width) * upload.height * 4U;
  upload.rgba_pixels.assign(layer_bytes * upload.layers, 0U);
  for (u32 layer = 0; layer < upload.layers; ++layer) {
    const asset::Image& image = atlas.pages[layer].image;
    if (image.empty() || image.channels != 4) {
      return {};
    }
    for (u32 y = 0; y < image.height; ++y) {
      const usize dst =
          static_cast<usize>(layer) * layer_bytes +
          static_cast<usize>(y) * upload.width * 4U;
      const usize src = static_cast<usize>(y) * image.width * 4U;
      std::memcpy(
          upload.rgba_pixels.data() + dst,
          image.pixels.data() + src,
          static_cast<usize>(image.width) * 4U);
    }
  }
  return upload;
}

bool upload_texture_array_with_status(
    const render::TextureArrayUploadData& upload,
    std::string_view invalid_status,
    FunctionRef<bool(const render::TextureArrayUploadData&)> upload_backend,
    FunctionRef<void(std::string_view)> report_status) {
  if (!upload.valid()) {
    if (report_status) {
      report_status(invalid_status);
    }
    return false;
  }
  if (!upload_backend) {
    return false;
  }
  return upload_backend(upload);
}

EntityRenderAssetUploadSummary upload_entity_render_asset_records(
    std::span<const render::EntityModelAsset> models,
    std::span<const render::EntityAnimationAsset> animations,
    const EntityRenderAssetUploadCallbacks& callbacks) {
  EntityRenderAssetUploadSummary summary;
  if (callbacks.upload_model) {
    for (const render::EntityModelAsset& model : models) {
      callbacks.upload_model(model);
      ++summary.models;
    }
  }
  if (callbacks.upload_animation) {
    for (const render::EntityAnimationAsset& animation : animations) {
      callbacks.upload_animation(animation);
      ++summary.animations;
    }
  }
  return summary;
}

} // namespace arv::client
