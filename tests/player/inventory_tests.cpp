#include "arv/player/inventory.hpp"

#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] arv::io::DataDocument stack_data(std::string_view value) {
  arv::io::DataDocument document{1U, arv::io::DataValue::object()};
  arv::io::DataValue payload = arv::io::DataValue::object();
  payload.set("value", std::string{value});
  document.root.set("test", std::move(payload));
  return document;
}

} // namespace

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

TEST(InventoryTests, StackDataParticipatesInMergeCompatibility) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();

  arv::player::Inventory inventory(3);
  ASSERT_TRUE(inventory.set_slot(0, arv::player::ItemStack{stone_item, 60, stack_data("same")}));
  ASSERT_TRUE(inventory.set_slot(1, arv::player::ItemStack{stone_item, 8, stack_data("different")}));

  const arv::player::InventoryInsertResult result =
      inventory.insert_stack({stone_item, 10, stack_data("same")}, item_registry);

  EXPECT_TRUE(result.complete());
  ASSERT_TRUE(inventory.slot(0).has_value());
  ASSERT_TRUE(inventory.slot(1).has_value());
  ASSERT_TRUE(inventory.slot(2).has_value());
  EXPECT_EQ(inventory.slot(0)->count, 64U);
  EXPECT_EQ(inventory.slot(1)->count, 8U);
  EXPECT_EQ(inventory.slot(2)->count, 6U);
  EXPECT_TRUE(arv::player::item_stack_data_equal(
      inventory.slot(2)->data,
      std::optional<arv::io::DataDocument>{stack_data("same")}));
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

TEST(InventoryTests, SplitAndSecondaryClickPreserveStackData) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();

  arv::player::Inventory inventory(2);
  ASSERT_TRUE(inventory.set_slot(0, arv::player::ItemStack{stone_item, 9, stack_data("kept")}));

  const std::optional<arv::player::ItemStack> split = inventory.split_stack(0, 4);
  ASSERT_TRUE(split.has_value());
  EXPECT_TRUE(arv::player::item_stack_data_equal(
      split->data,
      std::optional<arv::io::DataDocument>{stack_data("kept")}));
  ASSERT_TRUE(inventory.slot(0).has_value());
  EXPECT_TRUE(arv::player::item_stack_data_equal(
      inventory.slot(0)->data,
      std::optional<arv::io::DataDocument>{stack_data("kept")}));

  std::optional<arv::player::ItemStack> carried = split;
  auto place_one = arv::player::apply_inventory_slot_click(
      inventory,
      1,
      arv::player::InventoryClickButton::Secondary,
      carried,
      item_registry);

  EXPECT_EQ(place_one.kind, arv::player::InventoryOperationResultKind::Success);
  ASSERT_TRUE(inventory.slot(1).has_value());
  EXPECT_EQ(inventory.slot(1)->count, 1U);
  EXPECT_TRUE(arv::player::item_stack_data_equal(
      inventory.slot(1)->data,
      std::optional<arv::io::DataDocument>{stack_data("kept")}));
}

TEST(InventoryTests, EmptyObjectStackDataNormalizesToNoData) {
  const arv::player::ItemStack original{
      arv::player::ItemID{7},
      3,
      arv::io::DataDocument{1U, arv::io::DataValue::object()}};

  arv::player::ItemStack normalized = original;
  arv::player::normalize_item_stack_data(normalized);

  EXPECT_FALSE(normalized.data.has_value());
  EXPECT_TRUE(arv::player::item_stack_data_equal(original.data, normalized.data));
}

TEST(InventoryTests, PrimaryClickPicksUpMergesAndSwapsStacks) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  const arv::player::ItemID dirt_item =
      item_registry.find_item("arvane:dirt").value();

  arv::player::Inventory inventory(3);
  ASSERT_TRUE(inventory.set_slot(0, arv::player::ItemStack{stone_item, 10}));
  ASSERT_TRUE(inventory.set_slot(1, arv::player::ItemStack{stone_item, 60}));
  ASSERT_TRUE(inventory.set_slot(2, arv::player::ItemStack{dirt_item, 4}));
  std::optional<arv::player::ItemStack> carried;

  auto pick = arv::player::apply_inventory_slot_click(
      inventory,
      0,
      arv::player::InventoryClickButton::Primary,
      carried,
      item_registry);
  EXPECT_EQ(pick.kind, arv::player::InventoryOperationResultKind::Success);
  ASSERT_TRUE(carried.has_value());
  EXPECT_EQ(carried->count, 10U);
  EXPECT_FALSE(inventory.slot(0).has_value());

  auto merge = arv::player::apply_inventory_slot_click(
      inventory,
      1,
      arv::player::InventoryClickButton::Primary,
      carried,
      item_registry);
  EXPECT_EQ(merge.kind, arv::player::InventoryOperationResultKind::PartialSuccess);
  ASSERT_TRUE(carried.has_value());
  EXPECT_EQ(carried->count, 6U);
  ASSERT_TRUE(inventory.slot(1).has_value());
  EXPECT_EQ(inventory.slot(1)->count, 64U);

  auto swap = arv::player::apply_inventory_slot_click(
      inventory,
      2,
      arv::player::InventoryClickButton::Primary,
      carried,
      item_registry);
  EXPECT_EQ(swap.kind, arv::player::InventoryOperationResultKind::Success);
  ASSERT_TRUE(carried.has_value());
  EXPECT_EQ(carried->item, dirt_item);
  EXPECT_EQ(carried->count, 4U);
  ASSERT_TRUE(inventory.slot(2).has_value());
  EXPECT_EQ(inventory.slot(2)->item, stone_item);
  EXPECT_EQ(inventory.slot(2)->count, 6U);
}

TEST(InventoryTests, SecondaryClickSplitsAndPlacesSingleItems) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();

  arv::player::Inventory inventory(2);
  ASSERT_TRUE(inventory.set_slot(0, arv::player::ItemStack{stone_item, 9}));
  std::optional<arv::player::ItemStack> carried;

  auto split = arv::player::apply_inventory_slot_click(
      inventory,
      0,
      arv::player::InventoryClickButton::Secondary,
      carried,
      item_registry);
  EXPECT_EQ(split.kind, arv::player::InventoryOperationResultKind::Success);
  ASSERT_TRUE(carried.has_value());
  EXPECT_EQ(carried->count, 5U);
  ASSERT_TRUE(inventory.slot(0).has_value());
  EXPECT_EQ(inventory.slot(0)->count, 4U);

  auto place_one_empty = arv::player::apply_inventory_slot_click(
      inventory,
      1,
      arv::player::InventoryClickButton::Secondary,
      carried,
      item_registry);
  EXPECT_EQ(place_one_empty.kind, arv::player::InventoryOperationResultKind::Success);
  ASSERT_TRUE(carried.has_value());
  EXPECT_EQ(carried->count, 4U);
  ASSERT_TRUE(inventory.slot(1).has_value());
  EXPECT_EQ(inventory.slot(1)->count, 1U);

  auto place_one_existing = arv::player::apply_inventory_slot_click(
      inventory,
      1,
      arv::player::InventoryClickButton::Secondary,
      carried,
      item_registry);
  EXPECT_EQ(place_one_existing.kind, arv::player::InventoryOperationResultKind::Success);
  ASSERT_TRUE(carried.has_value());
  EXPECT_EQ(carried->count, 3U);
  ASSERT_TRUE(inventory.slot(1).has_value());
  EXPECT_EQ(inventory.slot(1)->count, 2U);
}

TEST(ItemRegistryTests, ResolvesExplicitTextureItemsFromJson) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  auto definitions = arv::player::parse_item_definitions_json(R"json(
{
  "items": [
    {
      "name": "arvane:coal",
      "display_name": "Coal",
      "behavior": "test:item_behavior/resource",
      "tags": ["arvane:test/resource"],
      "max_stack_size": 64,
      "buoyancy": 0.5,
      "render_type": "texture",
      "texture": "arvane:texture/item/coal",
      "tool": {
        "category": "pickaxe",
        "break_speed_multiplier": 3.5
      },
      "components": {
        "damage": {
          "amount": 4.0,
          "range": 3.0,
          "horizontal_knockback": 4.25,
          "vertical_knockback": 1.5,
          "cooldown_seconds": 0.5
        },
        "knockback": {
          "sprint_bonus": 0.75
        }
      }
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
  EXPECT_EQ(coal->behavior_id, "test:item_behavior/resource");
  ASSERT_EQ(coal->tags.size(), 1U);
  EXPECT_EQ(coal->tags.front(), "arvane:test/resource");
  EXPECT_EQ(coal->render_type, arv::player::ItemRenderType::Texture);
  EXPECT_EQ(coal->texture_asset, "arvane:texture/item/coal");
  EXPECT_FLOAT_EQ(coal->buoyancy, 0.5F);
  EXPECT_EQ(coal->tool_category, "pickaxe");
  EXPECT_FLOAT_EQ(coal->tool_break_speed_multiplier, 3.5F);
  ASSERT_TRUE(coal->attack_data.has_value());
  EXPECT_FLOAT_EQ(coal->attack_data->damage_amount, 4.0F);
  EXPECT_FLOAT_EQ(coal->attack_data->attack_range, 3.0F);
  EXPECT_EQ(coal->attack_data->cooldown_ticks, 30U);
  EXPECT_FLOAT_EQ(coal->attack_data->horizontal_knockback, 4.25F);
  EXPECT_FLOAT_EQ(coal->attack_data->vertical_knockback, 1.5F);
  EXPECT_FLOAT_EQ(coal->attack_data->sprint_knockback_bonus, 0.75F);
  EXPECT_FALSE(coal->placement_variant.has_value());
}

TEST(ItemRegistryTests, ValidatesAuthoredBehaviorIdsWhenCallbackIsProvided) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  auto definitions = arv::player::parse_item_definitions_json(R"json(
{
  "items": [
    {
      "name": "arvane:wand",
      "behavior": "test:item_behavior/wand",
      "render_type": "texture",
      "texture": "arvane:texture/item/wand"
    }
  ]
}
)json");
  ASSERT_TRUE(definitions);

  auto missing_result = arv::player::ItemRegistry::build_from_blocks_and_items(
      block_registry,
      definitions.value(),
      {.behavior_exists = arv::FunctionRef<bool(std::string_view)>{
           [](std::string_view) { return false; }}});
  ASSERT_FALSE(missing_result);
  EXPECT_NE(
      missing_result.error().message.find("missing item behavior handler"),
      std::string::npos);

  arv::player::ItemBehaviorRegistry behaviors;
  ASSERT_TRUE(behaviors.register_handler("test:item_behavior/wand"));
  auto registry_result = arv::player::ItemRegistry::build_from_blocks_and_items(
      block_registry,
      std::move(definitions.value()),
      {.behavior_exists = arv::FunctionRef<bool(std::string_view)>{
           [&behaviors](std::string_view id) { return behaviors.contains(id); }}});
  ASSERT_TRUE(registry_result);

  const auto wand_id = registry_result.value().find_item("arvane:wand");
  ASSERT_TRUE(wand_id);
  const arv::player::ItemRecord* wand = registry_result.value().item(wand_id.value());
  ASSERT_NE(wand, nullptr);
  EXPECT_EQ(wand->behavior_id, "test:item_behavior/wand");
}

TEST(ItemRegistryTests, GeneratedBlockItemsResolveToBlockRenderType) {
  std::vector<arv::voxel::BlockDefinition> blocks;
  blocks.push_back(arv::voxel::make_air_definition());
  arv::voxel::BlockDefinition stone =
      arv::voxel::make_full_cube_definition("arvane:stone");
  stone.display_name = "Stone";
  blocks.push_back(std::move(stone));

  auto block_registry_result = arv::voxel::BlockRegistry::build(std::move(blocks));
  ASSERT_TRUE(block_registry_result);
  const arv::voxel::BlockRegistry block_registry =
      std::move(block_registry_result.value());
  arv::player::ItemBehaviorRegistry behaviors;
  ASSERT_TRUE(behaviors.register_handler("test:item_behavior/block"));
  auto item_registry_result =
      arv::player::ItemRegistry::build_default_block_items(
          block_registry,
          {.behavior_exists = arv::FunctionRef<bool(std::string_view)>{
               [&behaviors](std::string_view id) { return behaviors.contains(id); }},
           .default_block_item_behavior_id = "test:item_behavior/block"});
  ASSERT_TRUE(item_registry_result);
  const arv::player::ItemRegistry item_registry =
      std::move(item_registry_result.value());

  const auto stone_id = item_registry.find_item("arvane:stone");
  ASSERT_TRUE(stone_id);
  const arv::player::ItemRecord* stone_item = item_registry.item(stone_id.value());
  ASSERT_NE(stone_item, nullptr);
  EXPECT_EQ(stone_item->display_name, "Stone");
  EXPECT_EQ(stone_item->render_type, arv::player::ItemRenderType::Block);
  EXPECT_EQ(stone_item->behavior_id, "test:item_behavior/block");
  ASSERT_TRUE(stone_item->render_block_variant.has_value());
  EXPECT_EQ(
      stone_item->render_block_variant.value(),
      block_registry.find_variant("arvane:stone").value());
}

TEST(ItemRegistryTests, GeneratedBlockItemsUseBlockBuoyancy) {
  std::vector<arv::voxel::BlockDefinition> blocks;
  blocks.push_back(arv::voxel::make_air_definition());
  arv::voxel::BlockDefinition cork =
      arv::voxel::make_full_cube_definition("arvane:cork");
  cork.item_buoyancy = 2.25F;
  blocks.push_back(std::move(cork));

  auto block_registry_result = arv::voxel::BlockRegistry::build(std::move(blocks));
  ASSERT_TRUE(block_registry_result);
  auto item_registry_result =
      arv::player::ItemRegistry::build_default_block_items(block_registry_result.value());
  ASSERT_TRUE(item_registry_result);

  const auto cork_item_id = item_registry_result.value().find_item("arvane:cork");
  ASSERT_TRUE(cork_item_id);
  const arv::player::ItemRecord* cork_item =
      item_registry_result.value().item(cork_item_id.value());
  ASSERT_NE(cork_item, nullptr);
  EXPECT_FLOAT_EQ(cork_item->buoyancy, 2.25F);
}

TEST(ItemRegistryTests, RejectsBlockDropOverrideMissingItem) {
  std::vector<arv::voxel::BlockDefinition> blocks;
  blocks.push_back(arv::voxel::make_air_definition());
  arv::voxel::BlockDefinition mystery =
      arv::voxel::make_full_cube_definition("arvane:mystery");
  mystery.drops.kind = arv::voxel::BlockDropKind::Item;
  mystery.drops.item_name = "arvane:not_registered";
  blocks.push_back(std::move(mystery));

  auto block_registry_result = arv::voxel::BlockRegistry::build(std::move(blocks));
  ASSERT_TRUE(block_registry_result);
  auto item_registry_result =
      arv::player::ItemRegistry::build_default_block_items(block_registry_result.value());

  ASSERT_FALSE(item_registry_result);
  EXPECT_NE(
      item_registry_result.error().message.find("drops missing item"),
      std::string::npos);
}
