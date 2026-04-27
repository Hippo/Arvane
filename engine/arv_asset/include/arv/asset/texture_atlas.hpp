#pragma once

#include "arv/asset/resource.hpp"
#include "arv/voxel/block.hpp"

#include <span>
#include <string>
#include <unordered_map>
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
  std::unordered_map<std::string, u32> entry_by_asset_id{};
  u32 padding{};
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
  u32 padding{4};
};

struct BlockTextureAtlas {
  TextureAtlas atlas{};
  voxel::BlockRenderData render_data{};
};

using TextureAtlasResult = Expected<TextureAtlas, AssetError>;
using BlockTextureAtlasResult = Expected<BlockTextureAtlas, AssetError>;

[[nodiscard]] TextureAtlasResult build_texture_atlas(
    std::span<const TextureAtlasInput> inputs,
    TextureAtlasBuildOptions options = {});

[[nodiscard]] BlockTextureAtlasResult build_block_texture_atlas(
    ResourceManager& resources,
    const voxel::BlockRegistry& registry,
    TextureAtlasBuildOptions options = {});

} // namespace arv::asset
