#include "arv/voxel/block.hpp"

#include <gtest/gtest.h>

#include <optional>
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
