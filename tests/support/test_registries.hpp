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

  auto result = voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

inline player::ItemRegistry make_test_item_registry(const voxel::BlockRegistry& registry) {
  auto result = player::ItemRegistry::build_default_block_items(registry);
  EXPECT_TRUE(result);
  return std::move(result.value());
}

} // namespace arv::testsupport
