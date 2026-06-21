#include "arv/crafting/crafting.hpp"
#include "arv/asset/pack.hpp"

#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"
#include "support/test_registries.hpp"

#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

void write_pack_manifest(
    const std::filesystem::path& packs_root,
    std::string_view id,
    std::string_view namespaces = "  - \"arvane\"\n") {
  write_text(
      packs_root / std::string{id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{id} + "\"\n"
                            "display_name: \"" +
          std::string{id} + "\"\n"
                            "version: \"1.0.0\"\n"
                            "namespaces:\n" +
          std::string{namespaces} +
          "side: \"both\"\n"
          "dependencies: []\n"
          "load_after: []\n"
          "load_before: []\n"
          "capabilities:\n"
          "  assets: true\n"
          "  data: true\n"
          "  native_plugin: false\n");
}

arv::crafting::CraftingRecipe make_recipe(
    std::string id,
    arv::player::ItemID ingredient,
    arv::u16 ingredient_count,
    arv::player::ItemID result,
    arv::u16 result_count) {
  arv::crafting::CraftingRecipe recipe;
  recipe.id = std::move(id);
  recipe.category = "arvane:misc";
  recipe.icon.item = result;
  recipe.result.item = result;
  recipe.result.count = result_count;
  recipe.ingredients.push_back({
      arv::crafting::CraftingIngredientKind::Item,
      {},
      {},
      ingredient,
      {},
      ingredient_count});
  return recipe;
}

} // namespace

TEST(CraftingTests, LoadsPackRecipesCategoriesAndUnlocks) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  const arv::player::ItemID bucket_item =
      item_registry.find_item("arvane:bucket").value();

  ScopedTempDir temp("arvane_crafting_pack_");
  const std::filesystem::path packs = temp.path / "packs";
  write_pack_manifest(packs, "arvane_base");
  write_text(
      packs / "arvane_base/data/arvane/crafting_categories/building.json",
      R"json({
        "id": "arvane:building",
        "name": "Building",
        "icon": {"item": "arvane:stone"},
        "sort_order": 100
      })json");
  write_text(
      packs / "arvane_base/data/arvane/recipes/dirt_from_stone.json",
      R"json({
        "id": "arvane:dirt_from_stone",
        "type": "arvane:crafting",
        "category": "arvane:building",
        "icon": {"item": "arvane:dirt"},
        "ingredients": [{"item": "arvane:stone", "count": 2}],
        "result": {"item": "arvane:dirt", "count": 1},
        "unlock": {"collect_items": ["arvane:stone"]},
        "search_terms": ["soil", "building"]
      })json");
  write_text(
      packs / "arvane_base/data/arvane/recipes/tag_recipe.json",
      R"json({
        "id": "arvane:combat_block",
        "type": "arvane:crafting",
        "category": "arvane:building",
        "ingredients": [{"tag": "arvane:combat/attacking", "count": 1}],
        "result": {"item": "arvane:dirt", "count": 1},
        "unlock": {"default": true}
      })json");
  write_text(
      packs / "arvane_base/data/arvane/recipes/blueprint_recipe.json",
      R"json({
        "id": "arvane:blueprint_block",
        "type": "arvane:crafting",
        "category": "arvane:building",
        "ingredients": [{"item": "arvane:stone", "count": 1}],
        "result": {"item": "arvane:dirt", "count": 1},
        "unlock": {"blueprint": "arvane:bucket"}
      })json");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  auto loaded = arv::crafting::load_crafting_registry_from_packs(
      manager.value(),
      item_registry);
  ASSERT_TRUE(loaded) << loaded.error().message;
  EXPECT_TRUE(loaded.value().diagnostics.empty());

  const arv::crafting::CraftingRecipeRegistry& registry = loaded.value().registry;
  ASSERT_NE(registry.category("arvane:building"), nullptr);
  ASSERT_NE(registry.recipe("arvane:dirt_from_stone"), nullptr);
  const arv::crafting::CraftingRecipe* tag_recipe = registry.recipe("arvane:combat_block");
  ASSERT_NE(tag_recipe, nullptr);
  ASSERT_EQ(tag_recipe->ingredients.size(), 1U);
  EXPECT_EQ(
      tag_recipe->ingredients.front().kind,
      arv::crafting::CraftingIngredientKind::ItemTag);
  EXPECT_FALSE(tag_recipe->ingredients.front().tag_items.empty());

  std::vector<std::string> unlocks =
      arv::crafting::normalized_recipe_unlocks({}, registry);
  EXPECT_TRUE(arv::crafting::recipe_is_unlocked(unlocks, "arvane:combat_block"));
  EXPECT_FALSE(arv::crafting::recipe_is_unlocked(unlocks, "arvane:dirt_from_stone"));
  EXPECT_FALSE(arv::crafting::recipe_is_unlocked(unlocks, "arvane:blueprint_block"));

  std::vector<std::string> unlocked_now =
      arv::crafting::unlock_recipes_for_collected_item(unlocks, stone_item, registry);
  EXPECT_EQ(unlocked_now, std::vector<std::string>{"arvane:dirt_from_stone"});
  EXPECT_TRUE(arv::crafting::recipe_is_unlocked(unlocks, "arvane:dirt_from_stone"));

  unlocked_now =
      arv::crafting::unlock_recipes_for_collected_item(unlocks, bucket_item, registry);
  EXPECT_EQ(unlocked_now, std::vector<std::string>{"arvane:blueprint_block"});
  EXPECT_TRUE(arv::crafting::recipe_is_unlocked(unlocks, "arvane:blueprint_block"));
}

TEST(CraftingTests, RejectsBadRecipeAndContinuesLoading) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);

  ScopedTempDir temp("arvane_crafting_bad_recipe_");
  const std::filesystem::path packs = temp.path / "packs";
  write_pack_manifest(packs, "arvane_base");
  write_text(
      packs / "arvane_base/data/arvane/crafting_categories/misc.json",
      R"json({"id":"arvane:misc","name":"Misc","icon":{"item":"arvane:stone"},"sort_order":1000})json");
  write_text(
      packs / "arvane_base/data/arvane/recipes/bad.json",
      R"json({
        "id": "arvane:bad",
        "type": "arvane:crafting",
        "category": "arvane:misc",
        "ingredients": [{"item": "arvane:stone", "count": 1}],
        "result": {"item": "arvane:not_real", "count": 1}
      })json");
  write_text(
      packs / "arvane_base/data/arvane/recipes/good.json",
      R"json({
        "id": "arvane:good",
        "type": "arvane:crafting",
        "category": "arvane:misc",
        "ingredients": [{"item": "arvane:stone", "count": 1}],
        "result": {"item": "arvane:dirt", "count": 1},
        "unlock": {"default": true}
      })json");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  auto loaded = arv::crafting::load_crafting_registry_from_packs(
      manager.value(),
      item_registry);
  ASSERT_TRUE(loaded) << loaded.error().message;
  ASSERT_FALSE(loaded.value().diagnostics.empty());
  EXPECT_NE(loaded.value().diagnostics.front().describe().find("arvane:not_real"), std::string::npos);
  EXPECT_EQ(loaded.value().registry.recipe("arvane:bad"), nullptr);
  EXPECT_NE(loaded.value().registry.recipe("arvane:good"), nullptr);
}

TEST(CraftingTests, CraftRecipeConsumesIngredientsOnlyOnSuccess) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  const arv::player::ItemID dirt_item =
      item_registry.find_item("arvane:dirt").value();
  const arv::player::ItemID bucket_item =
      item_registry.find_item("arvane:bucket").value();

  arv::player::Inventory inventory(3);
  ASSERT_TRUE(inventory.set_slot(0, arv::player::ItemStack{stone_item, 2}));
  const arv::crafting::CraftingRecipe dirt_recipe =
      make_recipe("arvane:dirt_from_stone", stone_item, 2, dirt_item, 1);

  const arv::crafting::CraftingAttemptResult success =
      arv::crafting::craft_recipe_once(inventory, dirt_recipe, item_registry);
  EXPECT_TRUE(success.success());
  ASSERT_TRUE(inventory.slot(0).has_value());
  EXPECT_EQ(inventory.slot(0)->item, dirt_item);
  EXPECT_EQ(inventory.slot(0)->count, 1U);

  arv::player::Inventory missing(2);
  ASSERT_TRUE(missing.set_slot(0, arv::player::ItemStack{stone_item, 1}));
  const arv::crafting::CraftingAttemptResult missing_result =
      arv::crafting::craft_recipe_once(missing, dirt_recipe, item_registry);
  EXPECT_EQ(missing_result.reason, arv::crafting::CraftingFailureReason::MissingIngredients);
  ASSERT_TRUE(missing.slot(0).has_value());
  EXPECT_EQ(missing.slot(0)->item, stone_item);
  EXPECT_EQ(missing.slot(0)->count, 1U);

  arv::player::Inventory full(2);
  ASSERT_TRUE(full.set_slot(0, arv::player::ItemStack{stone_item, 3}));
  ASSERT_TRUE(full.set_slot(1, arv::player::ItemStack{dirt_item, 64}));
  const arv::crafting::CraftingRecipe bucket_recipe =
      make_recipe("arvane:bucket_from_stone", stone_item, 1, bucket_item, 1);
  const arv::crafting::CraftingAttemptResult full_result =
      arv::crafting::craft_recipe_once(full, bucket_recipe, item_registry);
  EXPECT_EQ(full_result.reason, arv::crafting::CraftingFailureReason::InventoryFull);
  ASSERT_TRUE(full.slot(0).has_value());
  EXPECT_EQ(full.slot(0)->item, stone_item);
  EXPECT_EQ(full.slot(0)->count, 3U);
  ASSERT_TRUE(full.slot(1).has_value());
  EXPECT_EQ(full.slot(1)->item, dirt_item);
  EXPECT_EQ(full.slot(1)->count, 64U);
}
