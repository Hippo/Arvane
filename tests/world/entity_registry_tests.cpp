#include "arv/asset/resource.hpp"
#include "arv/entity/components.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

TEST(EntityRegistryTests, BuildsBuiltinsAndConfiguredEntityTypes) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "entities": [
    {
      "name": "arvane:pig",
      "display_name": "Pig",
      "category": "passive",
      "model": "pig.json",
      "spawnable": true,
      "persistent_by_default": true,
      "replicated_by_default": true
    }
  ]
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;

  auto registry_result =
      arv::entity::EntityRegistry::build_with_builtins(std::move(parsed.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const arv::entity::EntityRegistry registry = std::move(registry_result.value());

  const arv::entity::EntityTypeRecord* player =
      registry.type(arv::entity::PlayerEntityTypeID);
  ASSERT_NE(player, nullptr);
  EXPECT_EQ(player->name, arv::entity::PlayerEntityTypeName);
  EXPECT_FALSE(player->spawnable);

  const std::optional<arv::EntityTypeID> pig_id = registry.find_type("arvane:pig");
  ASSERT_TRUE(pig_id.has_value());
  const arv::entity::EntityTypeRecord* pig = registry.type(pig_id.value());
  ASSERT_NE(pig, nullptr);
  EXPECT_EQ(pig->display_name, "Pig");
  EXPECT_EQ(pig->category, "passive");
  EXPECT_EQ(pig->model_asset, "arvane:model/entity/pig.json");
  EXPECT_TRUE(pig->spawnable);
  EXPECT_TRUE(pig->persistent_by_default);
  EXPECT_TRUE(pig->replicated_by_default);

  const arv::asset::ResourceManager resources("/tmp/arvane_assets");
  EXPECT_EQ(
      resources.resolve_asset_path({pig->model_asset}, arv::asset::AssetKind::Model),
      std::filesystem::path{"/tmp/arvane_assets/models/entity/pig.json"});
}

TEST(EntityRegistryTests, AcceptsSingleEntityDefinitionObject) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "display_name": "Pig",
  "model": "pig.json"
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed.value().size(), 1U);
  EXPECT_EQ(parsed.value().front().name, "arvane:pig");
  EXPECT_EQ(parsed.value().front().model_asset, "pig.json");
}

TEST(EntityRegistryTests, RejectsDuplicateExplicitEntityNames) {
  std::vector<arv::entity::EntityDefinition> definitions;
  definitions.push_back({.name = "arvane:pig"});
  definitions.push_back({.name = "arvane:pig"});

  auto registry_result =
      arv::entity::EntityRegistry::build_with_builtins(std::move(definitions));
  ASSERT_FALSE(registry_result);
  EXPECT_NE(registry_result.error().message.find("duplicate"), std::string::npos);
}

TEST(EntityRegistryTests, KeepsPlayerDefinitionReservedWhenConfigured) {
  std::vector<arv::entity::EntityDefinition> definitions;
  arv::entity::EntityDefinition player;
  player.name = std::string{arv::entity::PlayerEntityTypeName};
  player.display_name = "Custom Player";
  player.spawnable = true;
  player.persistent_by_default = false;
  player.replicated_by_default = false;
  definitions.push_back(std::move(player));

  auto registry_result =
      arv::entity::EntityRegistry::build_with_builtins(std::move(definitions));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const arv::entity::EntityTypeRecord* player_record =
      registry_result.value().type(arv::entity::PlayerEntityTypeID);
  ASSERT_NE(player_record, nullptr);
  EXPECT_EQ(player_record->display_name, "Custom Player");
  EXPECT_FALSE(player_record->spawnable);
  EXPECT_TRUE(player_record->persistent_by_default);
  EXPECT_TRUE(player_record->replicated_by_default);
}
