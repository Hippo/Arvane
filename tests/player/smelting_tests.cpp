#include "arv/crafting/smelting.hpp"
#include "arv/asset/pack.hpp"

#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"
#include "support/test_registries.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

void write_pack_manifest(
    const std::filesystem::path& packs_root,
    std::string_view id) {
  write_text(
      packs_root / std::string{id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{id} + "\"\n"
                            "display_name: \"" +
          std::string{id} + "\"\n"
                            "version: \"1.0.0\"\n"
                            "namespaces:\n"
                            "  - \"arvane\"\n"
                            "side: \"both\"\n"
                            "dependencies: []\n"
                            "load_after: []\n"
                            "load_before: []\n"
                            "capabilities:\n"
                            "  assets: true\n"
                            "  data: true\n"
                            "  native_plugin: false\n");
}

[[nodiscard]] arv::player::ItemRegistry make_smelting_item_registry(
    const arv::voxel::BlockRegistry& block_registry) {
  std::vector<arv::player::ItemDefinition> items;
  const auto add_item = [&items](std::string name) {
    arv::player::ItemDefinition item;
    item.name = std::move(name);
    item.max_stack_size = 64;
    item.explicit_render_type = arv::player::ItemRenderType::Texture;
    item.texture_asset = "arvane:texture/item/test";
    items.push_back(std::move(item));
  };
  add_item("arvane:coal");
  add_item("arvane:copper");
  add_item("arvane:copper_bar");
  add_item("arvane:iron");
  add_item("arvane:iron_bar");

  auto result = arv::player::ItemRegistry::build_from_blocks_and_items(
      block_registry,
      std::move(items));
  EXPECT_TRUE(result) << result.error().message;
  return std::move(result.value());
}

} // namespace

TEST(SmeltingTests, LoadsRulesAndAppliesDefaults) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      make_smelting_item_registry(block_registry);

  ScopedTempDir temp("arvane_smelting_pack_");
  const std::filesystem::path packs = temp.path / "packs";
  write_pack_manifest(packs, "arvane_base");
  write_text(
      packs / "arvane_base/data/arvane/smelting.yaml",
      "smelting:\n"
      "  - input: arvane:iron\n"
      "    input_count: 3\n"
      "    output: arvane:iron_bar\n"
      "    output_count: 1\n"
      "    duration_secs: 20\n"
      "  - input: arvane:copper\n"
      "    output: arvane:copper_bar\n");

  auto manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(manager) << manager.error().describe();
  auto loaded = arv::crafting::load_smelting_rule_registry_from_packs(
      manager.value(),
      item_registry);
  ASSERT_TRUE(loaded) << loaded.error().message;

  const arv::player::ItemID iron = item_registry.find_item("arvane:iron").value();
  const arv::player::ItemID iron_bar = item_registry.find_item("arvane:iron_bar").value();
  const arv::player::ItemID copper = item_registry.find_item("arvane:copper").value();
  const arv::player::ItemID copper_bar =
      item_registry.find_item("arvane:copper_bar").value();

  ASSERT_EQ(loaded.value().rules().size(), 2U);
  const arv::crafting::SmeltingRule* iron_rule = loaded.value().find_by_input(iron);
  ASSERT_NE(iron_rule, nullptr);
  EXPECT_EQ(iron_rule->input_count, 3U);
  EXPECT_EQ(iron_rule->output, iron_bar);
  EXPECT_EQ(iron_rule->output_count, 1U);
  EXPECT_FLOAT_EQ(iron_rule->duration_secs, 20.0F);

  const arv::crafting::SmeltingRule* copper_rule = loaded.value().find_by_input(copper);
  ASSERT_NE(copper_rule, nullptr);
  EXPECT_EQ(copper_rule->input_count, 1U);
  EXPECT_EQ(copper_rule->output, copper_bar);
  EXPECT_EQ(copper_rule->output_count, 1U);
  EXPECT_FLOAT_EQ(copper_rule->duration_secs, 10.0F);
}

TEST(SmeltingTests, RejectsUnknownItemsAndNonPositiveValues) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      make_smelting_item_registry(block_registry);

  auto bad_count = arv::crafting::parse_smelting_rules_yaml(
      "smelting:\n"
      "  - input: arvane:iron\n"
      "    input_count: 0\n"
      "    output: arvane:iron_bar\n",
      item_registry);
  ASSERT_FALSE(bad_count);
  EXPECT_NE(bad_count.error().message.find("input_count"), std::string::npos);

  auto bad_duration = arv::crafting::parse_smelting_rules_yaml(
      "smelting:\n"
      "  - input: arvane:iron\n"
      "    output: arvane:iron_bar\n"
      "    duration_secs: -1\n",
      item_registry);
  ASSERT_FALSE(bad_duration);
  EXPECT_NE(bad_duration.error().message.find("duration_secs"), std::string::npos);

  auto unknown = arv::crafting::parse_smelting_rules_yaml(
      "smelting:\n"
      "  - input: arvane:not_real\n"
      "    output: arvane:iron_bar\n",
      item_registry);
  ASSERT_FALSE(unknown);
  EXPECT_NE(unknown.error().message.find("unknown item"), std::string::npos);
}

TEST(SmeltingTests, RejectsDuplicateInputRules) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      make_smelting_item_registry(block_registry);

  auto duplicate = arv::crafting::parse_smelting_rules_yaml(
      "smelting:\n"
      "  - input: arvane:iron\n"
      "    output: arvane:iron_bar\n"
      "  - input: arvane:iron\n"
      "    output: arvane:copper_bar\n",
      item_registry);
  ASSERT_FALSE(duplicate);
  EXPECT_NE(duplicate.error().message.find("duplicate"), std::string::npos);
}
