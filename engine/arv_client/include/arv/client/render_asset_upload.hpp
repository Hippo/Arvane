#pragma once

#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/core/types.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/render/render_types.hpp"

#include <span>
#include <string_view>

namespace arv::client {

[[nodiscard]] render::TextureUploadData texture_upload_from_image(
    const asset::Image& image);

[[nodiscard]] render::TextureArrayUploadData texture_array_upload_from_image(
    const asset::Image& image);

[[nodiscard]] render::TextureArrayUploadData texture_array_upload_from_atlas(
    const asset::TextureAtlas& atlas);

[[nodiscard]] bool upload_texture_array_with_status(
    const render::TextureArrayUploadData& upload,
    std::string_view invalid_status,
    FunctionRef<bool(const render::TextureArrayUploadData&)> upload_backend,
    FunctionRef<void(std::string_view)> report_status = {});

struct EntityRenderAssetUploadCallbacks {
  FunctionRef<void(const render::EntityModelAsset&)> upload_model{};
  FunctionRef<void(const render::EntityAnimationAsset&)> upload_animation{};
};

struct EntityRenderAssetUploadSummary {
  usize models{};
  usize animations{};
};

[[nodiscard]] EntityRenderAssetUploadSummary upload_entity_render_asset_records(
    std::span<const render::EntityModelAsset> models,
    std::span<const render::EntityAnimationAsset> animations,
    const EntityRenderAssetUploadCallbacks& callbacks);

} // namespace arv::client
