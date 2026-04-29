#include "arv/player/inventory.hpp"

#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <utility>

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

TEST(ItemRegistryTests, ResolvesExplicitTextureItemsFromJson) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  auto definitions = arv::player::parse_item_definitions_json(R"json(
{
  "items": [
    {
      "name": "arvane:coal",
      "display_name": "Coal",
      "max_stack_size": 64,
      "render_type": "texture",
      "texture": "arvane:texture/item/coal"
    }
  ]
}
)json");
  ASSERT_TRUE(definitions);

  auto registry_result = arv::player::ItemRegistry::build_from_blocks_and_items(
      block_registry,
      std::move(definitions.value()));
  ASSERT_TRUE(registry_result);
  const arv::player::ItemRegistry registry = std::move(registry_result.value());

  const auto coal_id = registry.find_item("arvane:coal");
  ASSERT_TRUE(coal_id);
  const arv::player::ItemRecord* coal = registry.item(coal_id.value());
  ASSERT_NE(coal, nullptr);
  EXPECT_EQ(coal->render_type, arv::player::ItemRenderType::Texture);
  EXPECT_EQ(coal->texture_asset, "arvane:texture/item/coal");
  EXPECT_FALSE(coal->placement_variant.has_value());
}

TEST(ItemRegistryTests, GeneratedBlockItemsResolveToBlockRenderType) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);

  const auto stone_id = item_registry.find_item("arvane:stone");
  ASSERT_TRUE(stone_id);
  const arv::player::ItemRecord* stone = item_registry.item(stone_id.value());
  ASSERT_NE(stone, nullptr);
  EXPECT_EQ(stone->render_type, arv::player::ItemRenderType::Block);
  ASSERT_TRUE(stone->render_block_variant.has_value());
  EXPECT_EQ(stone->render_block_variant.value(), block_registry.find_variant("arvane:stone").value());
}
