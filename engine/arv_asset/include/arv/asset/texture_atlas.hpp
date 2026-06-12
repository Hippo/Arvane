#pragma once

#include "arv/asset/resource.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::asset {

struct TextureAtlasEntry {
  AssetID asset_id{};
  u32 page{};
  u32 texture_index{};
  Vec2i uv_min{};
  Vec2i uv_max{};
  Vec2i unpadded_pixel_min{};
  Vec2i unpadded_pixel_max{};
  Vec2i padded_pixel_min{};
  Vec2i padded_pixel_max{};
  u32 width{};
  u32 height{};
  bool fallback{};
};

struct TextureAtlasPage {
  Image image{};
};

struct TextureAtlas {
  std::vector<TextureAtlasPage> pages{};
  std::vector<TextureAtlasEntry> entries{};
  HashMap<std::string, u32> entry_by_asset_id{};
  u32 padding{};
  u32 mip_safe_max_level{};
  u32 version{1};

  [[nodiscard]] const TextureAtlasEntry* find(std::string_view asset_id) const noexcept;
};

struct TextureAtlasInput {
  AssetID asset_id{};
  Image image{};
  bool fallback{};
};

struct TextureAtlasBuildOptions {
  u32 max_page_size{1024};
  u32 max_pages{1};
  u32 padding{4};
  bool require_uniform_non_fallback_size{true};
};

[[nodiscard]] inline TextureAtlasBuildOptions default_block_texture_atlas_options() noexcept {
  TextureAtlasBuildOptions options;
  options.require_uniform_non_fallback_size = false;
  return options;
}

struct BlockTextureAtlas {
  TextureAtlas atlas{};
  voxel::BlockRenderData render_data{};
};

struct BlockModelAssetData {
  voxel::BlockVariantID variant_id{};
  voxel::BlockModelDefinition definition{};
};

using TextureAtlasResult = Expected<TextureAtlas, AssetError>;
using BlockTextureAtlasResult = Expected<BlockTextureAtlas, AssetError>;
using BlockModelAssetDataResult = Expected<std::vector<BlockModelAssetData>, AssetError>;
using BlockModelRenderBoxesResult =
    Expected<std::vector<voxel::BlockModelBoxRenderData>, AssetError>;

[[nodiscard]] TextureAtlasResult build_texture_atlas(
    std::span<const TextureAtlasInput> inputs,
    TextureAtlasBuildOptions options = {});

[[nodiscard]] BlockTextureAtlasResult build_block_texture_atlas(
    ResourceManager& resources,
    const voxel::BlockRegistry& registry,
    TextureAtlasBuildOptions options = default_block_texture_atlas_options());

[[nodiscard]] BlockTextureAtlasResult build_block_texture_atlas(
    ResourceManager& resources,
    const voxel::BlockRegistry& registry,
    std::span<const BlockModelAssetData> block_models,
    TextureAtlasBuildOptions options = default_block_texture_atlas_options());

[[nodiscard]] BlockModelAssetDataResult load_block_model_definitions(
    ResourceManager& resources,
    const voxel::BlockRegistry& registry);

void apply_block_model_collision_boxes(
    voxel::BlockRegistry& registry,
    std::span<const BlockModelAssetData> block_models);

[[nodiscard]] BlockModelRenderBoxesResult build_block_model_render_boxes(
    const TextureAtlas& atlas,
    const voxel::BlockModelDefinition& model,
    std::string_view operation = "build block model render data");

} // namespace arv::asset
