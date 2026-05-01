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
  definitions.push_back(std::move(stairs));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  ASSERT_TRUE(registry_result);
  const arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  const std::optional<arv::voxel::BlockID> stairs_id = registry.find_block("arvane:stone_stairs");
  ASSERT_TRUE(stairs_id);
  const arv::voxel::BlockRecord* stairs_record = registry.block(stairs_id.value());
  ASSERT_NE(stairs_record, nullptr);
  EXPECT_EQ(stairs_record->variants.size(), 4U);
  EXPECT_TRUE(registry.find_variant("arvane:stone_stairs[facing=north]"));
  EXPECT_TRUE(registry.find_variant("arvane:stone_stairs[facing=west]"));
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
    "render_category": "transparent",
    "tags": ["arvane:transparent"],
    "properties": {
      "solid": true,
      "collidable": true,
      "full_cube": true
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
