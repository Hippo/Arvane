#pragma once

#include "arv/render/entity_model.hpp"

#include <array>
#include <span>
#include <string>

namespace arv::render {

struct ItemTextureMeshInput {
  EntityModelID id{};
  std::string name{};
  u32 width{};
  u32 height{};
  std::span<const u8> rgba_pixels{};
  Vec2i atlas_pixel_min{};
  f32 thickness{2.0F};
};

struct ItemBlockModelInput {
  EntityModelID id{};
  std::string name{};
  std::array<std::array<Vec2i, 4>, voxel::BlockFaceCount> face_uvs{};
  std::array<bool, voxel::BlockFaceCount> face_valid{};
};

[[nodiscard]] EntityModelAsset build_extruded_texture_item_model(
    const ItemTextureMeshInput& input);

[[nodiscard]] EntityModelAsset build_block_item_model(
    const ItemBlockModelInput& input);

} // namespace arv::render
