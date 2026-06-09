#pragma once

#include "arv/render/entity_model.hpp"
#include "arv/voxel/block.hpp"

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
  u16 atlas_page{};
  f32 thickness{2.0F};
};

struct ItemBlockModelInput {
  EntityModelID id{};
  std::string name{};
  bool translucent{};
  std::array<std::array<Vec2i, 4>, voxel::BlockFaceCount> face_uvs{};
  std::array<u16, voxel::BlockFaceCount> face_pages{};
  std::array<bool, voxel::BlockFaceCount> face_valid{};
  std::span<const voxel::BlockModelBoxRenderData> model_boxes{};
};

struct BlockBreakOverlayModelInput {
  EntityModelID id{};
  std::string name{};
  std::array<std::array<Vec2i, 4>, voxel::BlockFaceCount> face_uvs{};
  std::array<u16, voxel::BlockFaceCount> face_pages{};
  std::array<bool, voxel::BlockFaceCount> face_valid{};
  std::span<const voxel::BlockModelBoxRenderData> model_boxes{};
  f32 inflate_sixteenths{0.08F};
};

struct BlockOutlineModelInput {
  EntityModelID id{};
  std::string name{};
  std::array<Vec2i, 4> uvs{};
  u16 atlas_page{};
  Vec3f color{0.02F, 0.02F, 0.02F};
  std::span<const voxel::BlockModelBoxRenderData> model_boxes{};
  f32 thickness_sixteenths{0.55F};
  f32 inflate_sixteenths{0.12F};
};

[[nodiscard]] EntityModelAsset build_extruded_texture_item_model(
    const ItemTextureMeshInput& input);

[[nodiscard]] EntityModelAsset build_block_item_model(
    const ItemBlockModelInput& input);

[[nodiscard]] EntityModelAsset build_block_break_overlay_model(
    const BlockBreakOverlayModelInput& input);

[[nodiscard]] EntityModelAsset build_block_outline_model(
    const BlockOutlineModelInput& input);

} // namespace arv::render
