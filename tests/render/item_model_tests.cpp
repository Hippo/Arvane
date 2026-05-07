#include "arv/render/item_model.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

TEST(ItemModelTests, ExtrudesOnlyVisibleTexturePixels) {
  std::vector<arv::u8> pixels(2U * 2U * 4U, 0U);
  pixels[3] = 255U;

  const arv::render::EntityModelAsset model = arv::render::build_extruded_texture_item_model({arv::render::EntityModelID{42},
                                                                                              "single_pixel",
                                                                                              2U,
                                                                                              2U,
                                                                                              std::span<const arv::u8>{pixels.data(), pixels.size()},
                                                                                              {10, 20},
                                                                                              0,
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

  const arv::render::EntityModelAsset model = arv::render::build_extruded_texture_item_model({arv::render::EntityModelID{43},
                                                                                              "two_pixels",
                                                                                              2U,
                                                                                              1U,
                                                                                              std::span<const arv::u8>{pixels.data(), pixels.size()},
                                                                                              {0, 0},
                                                                                              0,
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
    input.face_pages[index] = 4;
    input.face_uvs[index] = {{{0, 16}, {16, 16}, {0, 0}, {16, 0}}};
  }

  const arv::render::EntityModelAsset model = arv::render::build_block_item_model(input);

  EXPECT_EQ(model.id, (arv::render::EntityModelID{44}));
  EXPECT_EQ(model.vertices.size(), 24U);
  EXPECT_EQ(model.indices.size(), 36U);
  ASSERT_FALSE(model.vertices.empty());
  EXPECT_EQ(model.vertices.front().atlas_page, 4U);
}

TEST(ItemModelTests, BuildsBlockItemFromModelBoxes) {
  arv::voxel::BlockModelBoxRenderData box;
  box.min = {0, 0, 0};
  box.max = {16, 8, 16};
  for (const arv::voxel::BlockFace face : arv::voxel::AllBlockFaces) {
    arv::voxel::BlockModelFaceRenderData face_data;
    face_data.uvs = {{{0, 16}, {16, 16}, {0, 0}, {16, 0}}};
    box.faces[arv::voxel::face_index(face)] = face_data;
  }
  const std::array<arv::voxel::BlockModelBoxRenderData, 1> boxes{{box}};

  arv::render::ItemBlockModelInput input;
  input.id = arv::render::EntityModelID{45};
  input.name = "model_block_item";
  input.model_boxes =
      std::span<const arv::voxel::BlockModelBoxRenderData>{boxes.data(), boxes.size()};

  const arv::render::EntityModelAsset model = arv::render::build_block_item_model(input);

  EXPECT_EQ(model.id, (arv::render::EntityModelID{45}));
  EXPECT_EQ(model.vertices.size(), 24U);
  EXPECT_EQ(model.indices.size(), 36U);
  for (const arv::render::EntityModelMeshVertex& vertex : model.vertices) {
    EXPECT_GE(vertex.position.y, -8.0F);
    EXPECT_LE(vertex.position.y, 0.0F);
  }
}

TEST(ItemModelTests, BlockOutlineUsesUnionOfTouchingModelBoxes) {
  arv::voxel::BlockModelBoxRenderData left;
  left.min = {0, 0, 0};
  left.max = {8, 16, 16};
  arv::voxel::BlockModelBoxRenderData right;
  right.min = {8, 0, 0};
  right.max = {16, 16, 16};
  const std::array<arv::voxel::BlockModelBoxRenderData, 2> split_cube{{left, right}};

  arv::render::BlockOutlineModelInput split_input;
  split_input.id = arv::render::EntityModelID{46};
  split_input.name = "split_outline";
  split_input.uvs = {{{0, 1}, {1, 1}, {0, 0}, {1, 0}}};
  split_input.model_boxes =
      std::span<const arv::voxel::BlockModelBoxRenderData>{split_cube.data(), split_cube.size()};

  arv::render::BlockOutlineModelInput cube_input;
  cube_input.id = arv::render::EntityModelID{47};
  cube_input.name = "cube_outline";
  cube_input.uvs = split_input.uvs;

  const arv::render::EntityModelAsset split_model =
      arv::render::build_block_outline_model(split_input);
  const arv::render::EntityModelAsset cube_model =
      arv::render::build_block_outline_model(cube_input);

  EXPECT_EQ(split_model.vertices.size(), cube_model.vertices.size());
  EXPECT_EQ(split_model.indices.size(), cube_model.indices.size());
}
