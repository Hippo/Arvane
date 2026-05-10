#include "arv/voxel/block.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<arv::voxel::BlockDefinition> minimal_definitions() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));
  return definitions;
}

} // namespace

TEST(BlockRegistryTests, BlockFaceOffsetMatchesFaceDirections) {
  EXPECT_EQ(
      arv::voxel::block_face_offset(arv::voxel::BlockFace::PositiveX),
      (arv::Vec3i{1, 0, 0}));
  EXPECT_EQ(
      arv::voxel::block_face_offset(arv::voxel::BlockFace::NegativeX),
      (arv::Vec3i{-1, 0, 0}));
  EXPECT_EQ(
      arv::voxel::block_face_offset(arv::voxel::BlockFace::PositiveY),
      (arv::Vec3i{0, 1, 0}));
  EXPECT_EQ(
      arv::voxel::block_face_offset(arv::voxel::BlockFace::NegativeY),
      (arv::Vec3i{0, -1, 0}));
  EXPECT_EQ(
      arv::voxel::block_face_offset(arv::voxel::BlockFace::PositiveZ),
      (arv::Vec3i{0, 0, 1}));
  EXPECT_EQ(
      arv::voxel::block_face_offset(arv::voxel::BlockFace::NegativeZ),
      (arv::Vec3i{0, 0, -1}));
}

TEST(BlockRegistryTests, AssignsAirToZeroAndPreservesAirInvariants) {
  auto registry_result = arv::voxel::BlockRegistry::build(minimal_definitions());
  ASSERT_TRUE(registry_result);
  const arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  EXPECT_EQ(registry.air_block_id(), arv::voxel::BlockID{0});
  EXPECT_EQ(registry.air_variant_id(), arv::voxel::BlockVariantID{0});
  EXPECT_TRUE(registry.is_air(arv::voxel::BlockVariantID{0}));

  const arv::voxel::BlockVariantRecord* air = registry.variant(arv::voxel::BlockVariantID{0});
  ASSERT_NE(air, nullptr);
  EXPECT_EQ(air->debug_name, "arvane:air");
  EXPECT_FALSE(air->properties.solid);
  EXPECT_FALSE(air->properties.collidable);
  EXPECT_FALSE(air->properties.opaque);
  EXPECT_FALSE(air->properties.blocks_light);
  EXPECT_EQ(air->render_category, arv::voxel::RenderCategory::None);
}

TEST(BlockRegistryTests, RejectsDuplicateBlockDefinitions) {
  std::vector<arv::voxel::BlockDefinition> definitions = minimal_definitions();
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));

  EXPECT_FALSE(registry_result);
  EXPECT_NE(registry_result.error().message.find("duplicate"), std::string::npos);
}

TEST(BlockRegistryTests, ExpandsStateSchemasIntoResolvedVariants) {
  std::vector<arv::voxel::BlockDefinition> definitions = minimal_definitions();
  arv::voxel::BlockDefinition stairs = arv::voxel::make_full_cube_definition("arvane:stone_stairs");
  stairs.states.push_back({"facing", {"north", "south", "east", "west"}});
  stairs.states.push_back({"half", {"bottom", "top"}});
  definitions.push_back(std::move(stairs));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  ASSERT_TRUE(registry_result);
  const arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  const std::optional<arv::voxel::BlockID> stairs_id = registry.find_block("arvane:stone_stairs");
  ASSERT_TRUE(stairs_id);
  const arv::voxel::BlockRecord* stairs_record = registry.block(stairs_id.value());
  ASSERT_NE(stairs_record, nullptr);
  EXPECT_EQ(stairs_record->variants.size(), 8U);
  const auto north_bottom = registry.find_variant("arvane:stone_stairs[facing=north,half=bottom]");
  const auto east_top = registry.find_variant("arvane:stone_stairs[facing=east,half=top]");
  ASSERT_TRUE(north_bottom);
  ASSERT_TRUE(east_top);
  const arv::voxel::BlockVariantRecord* east_top_record = registry.variant(east_top.value());
  ASSERT_NE(east_top_record, nullptr);
  EXPECT_EQ(east_top_record->model_transform.quarter_turns_y, 1);
  EXPECT_TRUE(east_top_record->model_transform.mirror_y);
}

TEST(BlockRegistryTests, ParsesDataDrivenJsonDefinitions) {
  auto definitions = arv::voxel::parse_block_definitions_json(R"json(
[
  {
    "name": "arvane:air",
    "render_category": "none",
    "properties": { "replaceable": true }
  },
  {
    "name": "arvane:glass",
    "display_name": "Glass",
    "buoyancy": 0.75,
    "drops": null,
    "render_category": "transparent",
    "tags": ["arvane:transparent"],
    "properties": {
      "solid": true,
      "collidable": true,
      "full_cube": true,
      "drops_with_wrong_tool": true,
      "hardness": 0.3
    }
  }
]
)json");
  ASSERT_TRUE(definitions);

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result);
  const arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  const std::optional<arv::voxel::BlockID> glass_id = registry.find_block("arvane:glass");
  ASSERT_TRUE(glass_id);
  EXPECT_TRUE(registry.has_tag(glass_id.value(), "arvane:transparent"));
  const arv::voxel::BlockVariantRecord* glass =
      registry.variant(registry.find_variant("arvane:glass").value());
  ASSERT_NE(glass, nullptr);
  EXPECT_EQ(glass->render_category, arv::voxel::RenderCategory::Transparent);
  EXPECT_FLOAT_EQ(glass->properties.hardness, 0.3F);
  EXPECT_TRUE(glass->properties.drops_with_wrong_tool);
  const arv::voxel::BlockRecord* glass_block = registry.block(glass_id.value());
  ASSERT_NE(glass_block, nullptr);
  EXPECT_EQ(glass_block->display_name, "Glass");
  EXPECT_FLOAT_EQ(glass_block->item_buoyancy, 0.75F);
  EXPECT_EQ(glass_block->drops.kind, arv::voxel::BlockDropKind::None);
}

TEST(BlockRegistryTests, ParsesBlockModelReferencesAndInputTextures) {
  auto definitions = arv::voxel::parse_block_definitions_json(R"json(
[
  {
    "name": "arvane:air",
    "render_category": "none",
    "properties": { "replaceable": true }
  },
  {
    "name": "arvane:stone_stairs",
    "render_category": "opaque",
    "model": {
      "path": "arvane:model/block/stairs",
      "textures": ["arvane:texture/block/cobblestone"]
    },
    "properties": {
      "solid": true,
      "collidable": true,
      "opaque": true,
      "blocks_light": true,
      "ao_occluder": true
    }
  }
]
)json");
  ASSERT_TRUE(definitions);

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result);
  const arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  const auto stairs_variant = registry.find_variant("arvane:stone_stairs");
  ASSERT_TRUE(stairs_variant.has_value());
  const arv::voxel::BlockVariantRecord* stairs = registry.variant(stairs_variant.value());
  ASSERT_NE(stairs, nullptr);
  EXPECT_EQ(stairs->model.asset, "arvane:model/block/stairs");
  ASSERT_EQ(stairs->model.texture_inputs.size(), 1U);
  EXPECT_EQ(stairs->model.texture_inputs.front(), "arvane:texture/block/cobblestone");
}

TEST(BlockRegistryTests, BlockModelTexturesCanResolveInputsOrSpecificAssets) {
  const std::string input_texture = "arvane:texture/block/cobblestone";
  auto model = arv::voxel::parse_block_model_json(R"json(
{
  "textures": ["$0", "stone"],
  "texture_sizes": [[16, 16], [16, 16]],
  "boxes": [{
    "bone": "",
    "pivot": [0, 0, 0],
    "min": [0, 0, 0],
    "max": [16, 8, 16],
    "up": {"texture": 0, "uv": [0, 0, 16, 16]},
    "down": {"texture": -1}
  }]
}
)json",
                                                  std::span<const std::string>{&input_texture, 1});

  ASSERT_TRUE(model) << model.error().message;
  ASSERT_EQ(model.value().texture_assets.size(), 2U);
  EXPECT_EQ(model.value().texture_assets[0], "arvane:texture/block/cobblestone");
  EXPECT_EQ(model.value().texture_assets[1], "arvane:texture/block/stone");
  ASSERT_EQ(model.value().boxes.size(), 1U);
  EXPECT_TRUE(model.value().boxes.front().faces[arv::voxel::face_index(arv::voxel::BlockFace::PositiveY)].has_value());
  EXPECT_TRUE(model.value().boxes.front().faces[arv::voxel::face_index(arv::voxel::BlockFace::NegativeY)].has_value());
  EXPECT_LT(
      model.value().boxes.front().faces[arv::voxel::face_index(arv::voxel::BlockFace::NegativeY)]->texture_index,
      0);
}

TEST(BlockRegistryTests, AuthoredModelDerivesCenterOriginFromBoxBounds) {
  auto model = arv::voxel::parse_block_model_json(R"json(
{
  "textures": ["stone"],
  "texture_sizes": [[16, 16]],
  "boxes": [{
    "bone": "",
    "pivot": [0, 0, 0],
    "min": [0, 0, 0],
    "max": [16, 8, 16],
    "up": {"texture": 0, "uv": [0, 0, 16, 16]}
  }, {
    "bone": "",
    "pivot": [0, 0, 0],
    "min": [0, 8, 0],
    "max": [16, 16, 8],
    "north": {"texture": 0, "uv": [0, 0, 16, 8]}
  }]
}
)json");

  ASSERT_TRUE(model) << model.error().message;
  EXPECT_FLOAT_EQ(model.value().center_origin.x, 8.0F);
  EXPECT_FLOAT_EQ(model.value().center_origin.y, 8.0F);
  EXPECT_FLOAT_EQ(model.value().center_origin.z, 8.0F);
}

TEST(BlockRegistryTests, BlockModelCollisionBoxesApplyStateTransform) {
  auto model = arv::voxel::parse_block_model_json(R"json(
{
  "textures": ["stone"],
  "texture_sizes": [[16, 16]],
  "boxes": [{
    "bone": "",
    "pivot": [0, 0, 0],
    "min": [0, 0, 0],
    "max": [16, 8, 16],
    "up": {"texture": 0, "uv": [0, 0, 16, 16]}
  }, {
    "bone": "",
    "pivot": [0, 0, 0],
    "min": [0, 8, 0],
    "max": [16, 16, 8],
    "north": {"texture": 0, "uv": [0, 0, 16, 8]}
  }]
}
)json");

  ASSERT_TRUE(model) << model.error().message;
  const std::vector<arv::Aabb3f> boxes = arv::voxel::make_block_model_collision_boxes(
      model.value(),
      {1, true});

  ASSERT_EQ(boxes.size(), 2U);
  EXPECT_FLOAT_EQ(boxes.front().min.y, 0.5F);
  EXPECT_FLOAT_EQ(boxes.front().max.y, 1.0F);
  EXPECT_FLOAT_EQ(boxes.back().min.x, 0.5F);
  EXPECT_FLOAT_EQ(boxes.back().max.x, 1.0F);
  EXPECT_FLOAT_EQ(boxes.back().min.y, 0.0F);
  EXPECT_FLOAT_EQ(boxes.back().max.y, 0.5F);
}

TEST(BlockRegistryTests, BlockModelRenderBoxTransformCarriesUvsWithRotatedFace) {
  arv::voxel::BlockModelBoxRenderData box;
  box.min = {0, 8, 0};
  box.max = {16, 16, 8};

  arv::voxel::BlockModelFaceRenderData north_face;
  north_face.page = 7;
  north_face.texture_index = 3;
  north_face.uvs = {{{1, 2}, {3, 4}, {5, 6}, {7, 8}}};
  box.faces[arv::voxel::face_index(arv::voxel::BlockFace::NegativeZ)] = north_face;

  const arv::voxel::BlockModelBoxRenderData transformed =
      arv::voxel::transform_model_box(box, {8, 8, 8}, {1, false});

  EXPECT_EQ(transformed.min.x, 8);
  EXPECT_EQ(transformed.min.y, 8);
  EXPECT_EQ(transformed.min.z, 0);
  EXPECT_EQ(transformed.max.x, 16);
  EXPECT_EQ(transformed.max.y, 16);
  EXPECT_EQ(transformed.max.z, 16);

  const auto& transformed_east =
      transformed.faces[arv::voxel::face_index(arv::voxel::BlockFace::PositiveX)];
  ASSERT_TRUE(transformed_east.has_value());
  EXPECT_EQ(transformed_east->page, north_face.page);
  EXPECT_EQ(transformed_east->texture_index, north_face.texture_index);
  EXPECT_EQ(transformed_east->uvs[2].x, 5);
  EXPECT_EQ(transformed_east->uvs[2].y, 6);
  EXPECT_FALSE(
      transformed.faces[arv::voxel::face_index(arv::voxel::BlockFace::NegativeZ)].has_value());
}
