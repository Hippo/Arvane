#include "arv/render/item_model.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

TEST(ItemModelTests, ExtrudesOnlyVisibleTexturePixels) {
  std::vector<arv::u8> pixels(2U * 2U * 4U, 0U);
  pixels[3] = 255U;

  const arv::render::EntityModelAsset model = arv::render::build_extruded_texture_item_model({
      arv::render::EntityModelID{42},
      "single_pixel",
      2U,
      2U,
      std::span<const arv::u8>{pixels.data(), pixels.size()},
      {10, 20},
      2.0F});

  EXPECT_EQ(model.id, (arv::render::EntityModelID{42}));
  EXPECT_EQ(model.vertices.size(), 24U);
  EXPECT_EQ(model.indices.size(), 36U);
  for (const arv::render::EntityModelMeshVertex& vertex : model.vertices) {
    EXPECT_GE(vertex.position.x, -8.0F);
    EXPECT_LE(vertex.position.x, 0.0F);
    EXPECT_GE(vertex.position.y, 0.0F);
    EXPECT_LE(vertex.position.y, 8.0F);
    EXPECT_GE(vertex.uv.x, 10.0F);
    EXPECT_LE(vertex.uv.x, 11.0F);
    EXPECT_GE(vertex.uv.y, 20.0F);
    EXPECT_LE(vertex.uv.y, 21.0F);
  }
}

TEST(ItemModelTests, DoesNotEmitInteriorSideFacesBetweenVisiblePixels) {
  std::vector<arv::u8> pixels(2U * 1U * 4U, 0U);
  pixels[3] = 255U;
  pixels[7] = 255U;

  const arv::render::EntityModelAsset model = arv::render::build_extruded_texture_item_model({
      arv::render::EntityModelID{43},
      "two_pixels",
      2U,
      1U,
      std::span<const arv::u8>{pixels.data(), pixels.size()},
      {0, 0},
      2.0F});

  EXPECT_EQ(model.vertices.size(), 40U);
  EXPECT_EQ(model.indices.size(), 60U);
}

TEST(ItemModelTests, BuildsBlockItemCubeFromFaceUvs) {
  arv::render::ItemBlockModelInput input;
  input.id = arv::render::EntityModelID{44};
  input.name = "block_item";
  for (const arv::voxel::BlockFace face : arv::voxel::AllBlockFaces) {
    const arv::usize index = arv::voxel::face_index(face);
    input.face_valid[index] = true;
    input.face_uvs[index] = {{{0, 16}, {16, 16}, {0, 0}, {16, 0}}};
  }

  const arv::render::EntityModelAsset model = arv::render::build_block_item_model(input);

  EXPECT_EQ(model.id, (arv::render::EntityModelID{44}));
  EXPECT_EQ(model.vertices.size(), 24U);
  EXPECT_EQ(model.indices.size(), 36U);
}
