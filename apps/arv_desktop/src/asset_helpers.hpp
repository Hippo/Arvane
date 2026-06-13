#pragma once

#include "arv/client/entity_render_assets.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/render/render_types.hpp"
#include "arv/base/ui.hpp"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace arv::desktop {

using client::BlockOutlineRenderConfig;
using client::BreakStageTextureCount;
using client::EntityRenderAssets;
using client::EntityRenderAssetsResult;

[[nodiscard]] render::TextureUploadData texture_upload_from_image(const asset::Image& image);
[[nodiscard]] render::TextureArrayUploadData texture_array_upload_from_image(
    const asset::Image& image);
[[nodiscard]] render::TextureArrayUploadData texture_array_upload_from_atlas(
    const asset::TextureAtlas& atlas);

[[nodiscard]] std::array<Vec2f, 4> normalized_uvs(
    const std::array<Vec2i, 4>& pixels,
    u32 width,
    u32 height);

[[nodiscard]] ui::UiImageData image_data_from_atlas_entry(
    const asset::TextureAtlasEntry& entry,
    u32 page_width,
    u32 page_height,
    render::UiTextureSource source);

[[nodiscard]] asset::TextureAtlasResult build_item_texture_atlas(
    asset::ResourceManager& resources,
    const player::ItemRegistry& item_registry,
    asset::TextureAtlasBuildOptions options = {.max_page_size = 1024, .max_pages = 1, .padding = 2});

[[nodiscard]] EntityRenderAssetsResult build_entity_render_assets(
    asset::ResourceManager& resources,
    const entity::EntityRegistry& entity_registry,
    const player::ItemRegistry& item_registry,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    std::span<const asset::BlockModelAssetData> block_models = {},
    BlockOutlineRenderConfig outline_config = {},
    asset::TextureAtlasBuildOptions atlas_options =
        {.max_page_size = 1024,
         .max_pages = 1,
         .padding = 2,
         .require_uniform_non_fallback_size = false});

} // namespace arv::desktop
