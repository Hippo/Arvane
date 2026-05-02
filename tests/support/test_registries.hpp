#pragma once

#include "arv/player/item.hpp"
#include "arv/voxel/block.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace arv::testsupport {

inline voxel::BlockRegistry make_test_block_registry() {
  std::vector<voxel::BlockDefinition> definitions;
  definitions.push_back(voxel::make_air_definition());
  definitions.push_back(voxel::make_full_cube_definition("arvane:stone"));
  definitions.push_back(voxel::make_full_cube_definition("arvane:dirt"));
  definitions.push_back(voxel::make_full_cube_definition("arvane:grass_block"));
  voxel::BlockDefinition water;
  water.name = "arvane:water";
  water.properties.replaceable = true;
  water.properties.fluid = true;
  water.render_category = voxel::RenderCategory::Transparent;
  water.is_item = false;
  water.texture_assets.fill("arvane:texture/block/water");
  definitions.push_back(std::move(water));

  auto result = voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

inline player::ItemRegistry make_test_item_registry(const voxel::BlockRegistry& registry) {
  std::vector<player::ItemDefinition> items;
  player::ItemDefinition bucket;
  bucket.name = "arvane:bucket";
  bucket.display_name = "Bucket";
  bucket.category = "tools";
  bucket.max_stack_size = 16;
  bucket.explicit_render_type = player::ItemRenderType::Texture;
  bucket.texture_asset = "arvane:texture/item/bucket";
  items.push_back(std::move(bucket));

  player::ItemDefinition pickaxe;
  pickaxe.name = "arvane:wood_pickaxe";
  pickaxe.display_name = "Wood Pickaxe";
  pickaxe.category = "tools";
  pickaxe.max_stack_size = 1;
  pickaxe.explicit_render_type = player::ItemRenderType::Texture;
  pickaxe.texture_asset = "arvane:texture/item/pickaxe";
  pickaxe.tool_category = "pickaxe";
  pickaxe.tool_break_speed_multiplier = 4.0F;
  items.push_back(std::move(pickaxe));

  player::ItemDefinition shovel;
  shovel.name = "arvane:wood_shovel";
  shovel.display_name = "Wood Shovel";
  shovel.category = "tools";
  shovel.max_stack_size = 1;
  shovel.explicit_render_type = player::ItemRenderType::Texture;
  shovel.texture_asset = "arvane:texture/item/pickaxe";
  shovel.tool_category = "shovel";
  shovel.tool_break_speed_multiplier = 4.0F;
  items.push_back(std::move(shovel));

  player::ItemDefinition water_bucket;
  water_bucket.name = "arvane:water_bucket";
  water_bucket.display_name = "Water Bucket";
  water_bucket.category = "tools";
  water_bucket.max_stack_size = 1;
  water_bucket.explicit_render_type = player::ItemRenderType::Texture;
  water_bucket.texture_asset = "arvane:texture/item/water_bucket";
  items.push_back(std::move(water_bucket));

  auto result = player::ItemRegistry::build_from_blocks_and_items(registry, std::move(items));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

} // namespace arv::testsupport
