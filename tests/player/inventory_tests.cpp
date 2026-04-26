#include "arv/player/inventory.hpp"

#include "support/test_registries.hpp"

#include <gtest/gtest.h>

TEST(InventoryTests, InsertStackMergesIntoExistingStacksBeforeUsingEmptySlots) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();

  arv::player::Inventory inventory(3);
  ASSERT_TRUE(inventory.set_slot(0, arv::player::ItemStack{stone_item, 32}));

  const arv::player::InventoryInsertResult result =
      inventory.insert_stack({stone_item, 40}, item_registry);

  EXPECT_TRUE(result.complete());
  ASSERT_TRUE(inventory.slot(0).has_value());
  ASSERT_TRUE(inventory.slot(1).has_value());
  EXPECT_EQ(inventory.slot(0)->count, 64U);
  EXPECT_EQ(inventory.slot(1)->count, 8U);
}

TEST(InventoryTests, SplitStackReturnsRequestedAmountAndLeavesRemainder) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();

  arv::player::Inventory inventory(1);
  ASSERT_TRUE(inventory.set_slot(0, arv::player::ItemStack{stone_item, 10}));

  const std::optional<arv::player::ItemStack> split = inventory.split_stack(0, 4);

  ASSERT_TRUE(split.has_value());
  EXPECT_EQ(split->item, stone_item);
  EXPECT_EQ(split->count, 4U);
  ASSERT_TRUE(inventory.slot(0).has_value());
  EXPECT_EQ(inventory.slot(0)->count, 6U);
}
