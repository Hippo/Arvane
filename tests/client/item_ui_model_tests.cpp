#include "arv/client/item_ui_model.hpp"

#include "support/test_registries.hpp"

#include <gtest/gtest.h>

namespace {

struct ProjectionFixture {
  arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  arv::asset::TextureAtlas item_atlas{};
  arv::asset::BlockTextureAtlas block_atlas{};
};

} // namespace

TEST(ItemUiModelTests, ProjectsInventoryContainerAndCursorStacks) {
  ProjectionFixture fixture;
  const std::optional<arv::player::ItemID> bucket_id =
      fixture.item_registry.find_item("arvane:bucket");
  const std::optional<arv::player::ItemID> pickaxe_id =
      fixture.item_registry.find_item("arvane:wood_pickaxe");
  const std::optional<arv::player::ItemID> sword_id =
      fixture.item_registry.find_item("arvane:wood_sword");
  ASSERT_TRUE(bucket_id.has_value());
  ASSERT_TRUE(pickaxe_id.has_value());
  ASSERT_TRUE(sword_id.has_value());

  arv::player::Inventory inventory{3};
  ASSERT_TRUE(inventory.set_slot(
      0,
      arv::player::ItemStack{bucket_id.value(), 4U}));
  arv::player::Inventory container_inventory{2};
  ASSERT_TRUE(container_inventory.set_slot(
      1,
      arv::player::ItemStack{pickaxe_id.value(), 1U}));
  const std::optional<arv::player::ItemStack> cursor_stack =
      arv::player::ItemStack{sword_id.value(), 1U};

  const arv::client::InventoryUiProjection projection =
      arv::client::project_inventory_ui(
          {
              .has_authoritative_inventory = true,
              .container_open = true,
              .inventory = inventory,
              .container_inventory = container_inventory,
              .carried_stack = cursor_stack,
              .item_registry = fixture.item_registry,
              .item_atlas = fixture.item_atlas,
              .block_atlas = fixture.block_atlas,
          });

  ASSERT_EQ(projection.inventory_slots.size(), 3U);
  EXPECT_EQ(projection.inventory_slots[0].item_name, "arvane:bucket");
  EXPECT_EQ(projection.inventory_slots[0].display_name, "Bucket");
  EXPECT_EQ(projection.inventory_slots[0].count, 4U);
  ASSERT_EQ(projection.container_slots.size(), 2U);
  EXPECT_TRUE(projection.container_slots[0].item_name.empty());
  EXPECT_EQ(projection.container_slots[1].item_name, "arvane:wood_pickaxe");
  EXPECT_EQ(projection.container_slots[1].display_name, "Wood Pickaxe");
  ASSERT_TRUE(projection.carried_stack.has_value());
  EXPECT_EQ(projection.carried_stack->item_name, "arvane:wood_sword");
  EXPECT_EQ(projection.carried_stack->display_name, "Wood Sword");
}

TEST(ItemUiModelTests, OmitsContainerProjectionWhenContainerClosed) {
  ProjectionFixture fixture;
  const std::optional<arv::player::ItemID> bucket_id =
      fixture.item_registry.find_item("arvane:bucket");
  ASSERT_TRUE(bucket_id.has_value());

  arv::player::Inventory inventory{1};
  ASSERT_TRUE(inventory.set_slot(
      0,
      arv::player::ItemStack{bucket_id.value(), 2U}));
  arv::player::Inventory container_inventory{1};
  ASSERT_TRUE(container_inventory.set_slot(
      0,
      arv::player::ItemStack{bucket_id.value(), 1U}));
  const std::optional<arv::player::ItemStack> cursor_stack =
      arv::player::ItemStack{bucket_id.value(), 1U};

  const arv::client::InventoryUiProjection projection =
      arv::client::project_inventory_ui(
          {
              .has_authoritative_inventory = true,
              .container_open = false,
              .inventory = inventory,
              .container_inventory = container_inventory,
              .carried_stack = cursor_stack,
              .item_registry = fixture.item_registry,
              .item_atlas = fixture.item_atlas,
              .block_atlas = fixture.block_atlas,
          });

  ASSERT_EQ(projection.inventory_slots.size(), 1U);
  EXPECT_EQ(projection.inventory_slots[0].count, 2U);
  EXPECT_TRUE(projection.container_slots.empty());
  ASSERT_TRUE(projection.carried_stack.has_value());
  EXPECT_EQ(projection.carried_stack->count, 1U);
}

TEST(ItemUiModelTests, OmitsProjectionWithoutAuthoritativeInventory) {
  ProjectionFixture fixture;
  const std::optional<arv::player::ItemID> bucket_id =
      fixture.item_registry.find_item("arvane:bucket");
  ASSERT_TRUE(bucket_id.has_value());

  arv::player::Inventory inventory{1};
  ASSERT_TRUE(inventory.set_slot(
      0,
      arv::player::ItemStack{bucket_id.value(), 2U}));
  arv::player::Inventory container_inventory{1};
  ASSERT_TRUE(container_inventory.set_slot(
      0,
      arv::player::ItemStack{bucket_id.value(), 1U}));
  const std::optional<arv::player::ItemStack> cursor_stack =
      arv::player::ItemStack{bucket_id.value(), 1U};

  const arv::client::InventoryUiProjection projection =
      arv::client::project_inventory_ui(
          {
              .has_authoritative_inventory = false,
              .container_open = true,
              .inventory = inventory,
              .container_inventory = container_inventory,
              .carried_stack = cursor_stack,
              .item_registry = fixture.item_registry,
              .item_atlas = fixture.item_atlas,
              .block_atlas = fixture.block_atlas,
          });

  EXPECT_TRUE(projection.inventory_slots.empty());
  EXPECT_TRUE(projection.container_slots.empty());
  EXPECT_FALSE(projection.carried_stack.has_value());
}
