#include "arv/voxel/block.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
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
  EXPECT_FLOAT_EQ(air->blast.resistance, 0.0F);
  EXPECT_FALSE(air->blast.destroyable_by_explosions);
  EXPECT_FALSE(air->blast.drops_items);
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

TEST(BlockRegistryTests, ParsesContainerAndBlockEntityRenderMode) {
  auto definitions = arv::voxel::parse_block_definitions_json(R"json(
[
  {
    "name": "arvane:air",
    "render_category": "none",
    "properties": { "replaceable": true }
  },
  {
    "name": "arvane:chest",
    "display_name": "Chest",
    "render_category": "opaque",
    "render_mode": "block_entity_model",
    "container": { "type": "arvane:chest" },
    "model": {
      "path": "arvane:model/entity/chest",
      "animation": "arvane:animation/chest"
    },
    "properties": {
      "solid": true,
      "collidable": true,
      "opaque": false,
      "blocks_light": false,
      "full_cube": false,
      "ao_occluder": false
    },
    "states": {
      "facing": ["north", "east", "south", "west"]
    }
  }
]
)json");
  ASSERT_TRUE(definitions) << definitions.error().message;

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  const std::optional<arv::voxel::BlockID> chest_id = registry.find_block("arvane:chest");
  ASSERT_TRUE(chest_id);
  const arv::voxel::BlockRecord* chest = registry.block(chest_id.value());
  ASSERT_NE(chest, nullptr);
  EXPECT_EQ(chest->container_type, "arvane:chest");
  EXPECT_EQ(chest->variants.size(), 4U);

  const std::optional<arv::voxel::BlockVariantID> east =
      registry.find_variant("arvane:chest[facing=east]");
  ASSERT_TRUE(east);
  const arv::voxel::BlockVariantRecord* variant = registry.variant(east.value());
  ASSERT_NE(variant, nullptr);
  EXPECT_EQ(variant->container_type, "arvane:chest");
  EXPECT_EQ(variant->render_mode, arv::voxel::BlockRenderMode::BlockEntityModel);
  EXPECT_EQ(variant->render_category, arv::voxel::RenderCategory::Opaque);
  EXPECT_EQ(variant->model.asset, "arvane:model/entity/chest");
  EXPECT_EQ(variant->model.animation_asset, "arvane:animation/chest");
  EXPECT_EQ(variant->model_transform.quarter_turns_y, 1);
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
    "behavior": "test:block_behavior/glass",
    "buoyancy": 0.75,
    "drops": null,
    "blast": {
      "resistance": 0.3,
      "destroyable_by_explosions": true,
      "absorbs_blast": false,
      "flammable": false,
      "drops_items": false
    },
    "render_category": "transparent",
    "tags": ["arvane:transparent"],
    "properties": {
      "solid": true,
      "collidable": true,
      "full_cube": true,
      "drops_with_wrong_tool": true,
      "hardness": 0.3,
      "slipperiness": 0.98
    }
  },
  {
    "name": "arvane:oak_leaves",
    "render_category": "cutout",
    "render_tint": "foliage",
    "textures": "arvane:texture/block/oak_leaves",
    "properties": {
      "solid": true,
      "collidable": true,
      "full_cube": true,
      "hardness": 0.2
    }
  },
  {
    "name": "arvane:grass_block",
    "render_category": "opaque",
    "textures": {
      "top": "arvane:texture/block/grass_top",
      "side": "arvane:texture/block/grass_side",
      "bottom": "arvane:texture/block/dirt",
      "particle": "arvane:texture/block/dirt"
    },
    "properties": {
      "solid": true,
      "collidable": true,
      "opaque": true,
      "blocks_light": true,
      "full_cube": true,
      "hardness": 0.6
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
  EXPECT_FLOAT_EQ(glass->physics.slipperiness, 0.98F);
  EXPECT_FLOAT_EQ(registry.block_physics(glass->id).slipperiness, 0.98F);
  EXPECT_TRUE(glass->properties.drops_with_wrong_tool);
  const arv::voxel::BlockRecord* glass_block = registry.block(glass_id.value());
  ASSERT_NE(glass_block, nullptr);
  EXPECT_EQ(glass_block->display_name, "Glass");
  EXPECT_EQ(glass_block->behavior_id, "test:block_behavior/glass");
  EXPECT_FLOAT_EQ(glass_block->item_buoyancy, 0.75F);
  EXPECT_EQ(glass_block->drops.kind, arv::voxel::BlockDropKind::None);
  EXPECT_FLOAT_EQ(glass_block->blast.resistance, 0.3F);
  EXPECT_TRUE(glass_block->blast.destroyable_by_explosions);
  EXPECT_FALSE(glass_block->blast.absorbs_blast);
  EXPECT_FALSE(glass_block->blast.flammable);
  EXPECT_FALSE(glass_block->blast.drops_items);
  EXPECT_FLOAT_EQ(glass->blast.resistance, 0.3F);
  EXPECT_FALSE(glass->blast.drops_items);
  const arv::voxel::BlockVariantRecord* leaves =
      registry.variant(registry.find_variant("arvane:oak_leaves").value());
  ASSERT_NE(leaves, nullptr);
  EXPECT_EQ(leaves->render_category, arv::voxel::RenderCategory::Cutout);
  EXPECT_EQ(leaves->render_tint, arv::voxel::BlockRenderTint::Foliage);
  EXPECT_EQ(leaves->particle_texture_asset, "arvane:texture/block/oak_leaves");
  const arv::voxel::BlockVariantRecord* grass =
      registry.variant(registry.find_variant("arvane:grass_block").value());
  ASSERT_NE(grass, nullptr);
  EXPECT_EQ(
      grass->texture_assets[arv::voxel::face_index(arv::voxel::BlockFace::PositiveY)],
      "arvane:texture/block/grass_top");
  EXPECT_EQ(grass->particle_texture_asset, "arvane:texture/block/dirt");
}

TEST(BlockRegistryTests, ValidatesAuthoredBehaviorIdsWhenCallbackIsProvided) {
  std::vector<arv::voxel::BlockDefinition> definitions = minimal_definitions();
  definitions.back().behavior_id = "test:block_behavior/custom";

  auto missing_result = arv::voxel::BlockRegistry::build(
      definitions,
      {.behavior_exists = arv::FunctionRef<bool(std::string_view)>{
           [](std::string_view) { return false; }}});
  ASSERT_FALSE(missing_result);
  EXPECT_NE(
      missing_result.error().message.find("missing block behavior handler"),
      std::string::npos);

  arv::voxel::BlockBehaviorRegistry behaviors;
  ASSERT_TRUE(behaviors.register_handler("test:block_behavior/custom"));
  auto registry_result = arv::voxel::BlockRegistry::build(
      std::move(definitions),
      {.behavior_exists = arv::FunctionRef<bool(std::string_view)>{
           [&behaviors](std::string_view id) { return behaviors.contains(id); }}});
  ASSERT_TRUE(registry_result);
  const arv::voxel::BlockRegistry registry = std::move(registry_result.value());
  const arv::voxel::BlockRecord* stone =
      registry.block(registry.find_block("arvane:stone").value());
  ASSERT_NE(stone, nullptr);
  EXPECT_EQ(stone->behavior_id, "test:block_behavior/custom");
  const arv::voxel::BlockVariantRecord* variant =
      registry.variant(stone->default_variant);
  ASSERT_NE(variant, nullptr);
  EXPECT_EQ(variant->behavior_id, "test:block_behavior/custom");
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
  EXPECT_EQ(stairs->particle_texture_asset, "arvane:texture/block/cobblestone");
}

TEST(BlockRegistryTests, BlockModelTexturesCanResolveInputsOrSpecificAssets) {
  const std::string input_texture = "arvane:texture/block/cobblestone";
  auto model = arv::voxel::parse_block_model_json(R"json(
{
  "textures": ["$0", "stone"],
  "texture_sizes": [[16, 16], [16, 16]],
  "bones": {"root": {}},
  "boxes": [{
    "bone": "root",
    "min": [-8, 0, -8],
    "max": [8, 8, 8],
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
  "bones": {"root": {}},
  "boxes": [{
    "bone": "root",
    "min": [-8, 0, -8],
    "max": [8, 8, 8],
    "up": {"texture": 0, "uv": [0, 0, 16, 16]}
  }, {
    "bone": "root",
    "min": [-8, 8, -8],
    "max": [0, 16, 8],
    "north": {"texture": 0, "uv": [0, 0, 16, 8]}
  }]
}
)json");

  ASSERT_TRUE(model) << model.error().message;
  EXPECT_FLOAT_EQ(model.value().center_origin.x, 0.0F);
  EXPECT_FLOAT_EQ(model.value().center_origin.y, 8.0F);
  EXPECT_FLOAT_EQ(model.value().center_origin.z, 0.0F);
}

TEST(BlockRegistryTests, BlockModelCollisionBoxesApplyStateTransform) {
  auto model = arv::voxel::parse_block_model_json(R"json(
{
  "textures": ["stone"],
  "texture_sizes": [[16, 16]],
  "bones": {"root": {}},
  "boxes": [{
    "bone": "root",
    "min": [-8, 0, -8],
    "max": [8, 8, 8],
    "up": {"texture": 0, "uv": [0, 0, 16, 16]}
  }, {
    "bone": "root",
    "min": [-8, 8, -8],
    "max": [0, 16, 8],
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
  EXPECT_FLOAT_EQ(boxes.back().min.x, 0.0F);
  EXPECT_FLOAT_EQ(boxes.back().max.x, 1.0F);
  EXPECT_FLOAT_EQ(boxes.back().min.y, 0.0F);
  EXPECT_FLOAT_EQ(boxes.back().max.y, 0.5F);
  EXPECT_FLOAT_EQ(boxes.back().min.z, 0.0F);
  EXPECT_FLOAT_EQ(boxes.back().max.z, 0.5F);
}

TEST(BlockRegistryTests, BlockModelRejectsUnsupportedCubeRotation) {
  auto model = arv::voxel::parse_block_model_json(R"json(
{
  "textures": ["stone"],
  "texture_sizes": [[16, 16]],
  "bones": {"root": {}},
  "boxes": [{
    "bone": "root",
    "min": [-8, 0, -8],
    "max": [8, 16, 8],
    "origin": [0, 8, 0],
    "rotation": [0, 45, 0],
    "up": {"texture": 0, "uv": [0, 0, 16, 16]}
  }]
}
)json");

  ASSERT_FALSE(model);
  EXPECT_NE(model.error().message.find("rotation is not supported"), std::string::npos);
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
