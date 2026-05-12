#include "arv/asset/resource.hpp"
#include "arv/base/base_module.hpp"
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
      "components": {
        "arvane:transform": {},
        "arvane:collision": { "box": [0.9, 0.9, 0.9] },
        "arvane:movement": { "walk_speed": 1.2 },
        "arvane:health": { "max": 10, "current": 6, "invulnerable": true },
        "arvane:brain": {
          "behavior": "arvane:pig_behavior",
          "tick_rate": 5,
          "goals": [
            {
              "type": "arvane:wander",
              "priority": 8,
              "radius": 12,
              "speed_multiplier": 0.8,
              "animation": "walking"
            },
            {
              "type": "arvane:idle",
              "priority": 10
            }
          ]
        }
      },
      "presentation": {
        "model": "pig.json",
        "animation": "arvane:animation/pig"
      },
      "spawnable": true,
      "persistent_by_default": true,
      "replicated_by_default": true
    }
  ]
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;

  auto registry_result =
      arv::base::build_base_entity_registry(std::move(parsed.value()));
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
  EXPECT_EQ(pig->animation_asset, "arvane:animation/pig");
  ASSERT_TRUE(pig->components.collision.has_value());
  EXPECT_EQ(pig->components.collision->box, (arv::Vec3f{0.9F, 0.9F, 0.9F}));
  ASSERT_TRUE(pig->components.movement.has_value());
  EXPECT_FLOAT_EQ(pig->components.movement->walk_speed, 1.2F);
  ASSERT_TRUE(pig->components.health.has_value());
  EXPECT_FLOAT_EQ(pig->components.health->max, 10.0F);
  ASSERT_TRUE(pig->components.health->current.has_value());
  EXPECT_FLOAT_EQ(pig->components.health->current.value(), 6.0F);
  EXPECT_TRUE(pig->components.health->invulnerable);
  ASSERT_TRUE(pig->components.brain.has_value());
  EXPECT_EQ(pig->components.brain->behavior, "arvane:pig_behavior");
  EXPECT_EQ(pig->components.brain->tick_interval, 5U);
  ASSERT_EQ(pig->components.brain->goals.size(), 2U);
  EXPECT_EQ(pig->components.brain->goals.front().type, "arvane:wander");
  EXPECT_EQ(pig->components.brain->goals.front().animation, "walking");
  EXPECT_TRUE(pig->spawnable);
  EXPECT_TRUE(pig->persistent_by_default);
  EXPECT_TRUE(pig->replicated_by_default);

  const arv::asset::ResourceManager resources("/tmp/arvane_assets");
  EXPECT_EQ(
      resources.resolve_asset_path({pig->model_asset}, arv::asset::AssetKind::Model),
      std::filesystem::path{"/tmp/arvane_assets/models/entity/pig.json"});
  EXPECT_EQ(
      resources.resolve_asset_path({pig->animation_asset}, arv::asset::AssetKind::Animation),
      std::filesystem::path{"/tmp/arvane_assets/animations/pig.json"});
}

TEST(EntityRegistryTests, AcceptsSingleEntityDefinitionObject) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "display_name": "Pig",
  "presentation": {
    "model": "pig.json"
  }
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed.value().size(), 1U);
  EXPECT_EQ(parsed.value().front().name, "arvane:pig");
  EXPECT_EQ(parsed.value().front().model_asset, "pig.json");
  EXPECT_TRUE(parsed.value().front().animation_asset.empty());
}

TEST(EntityRegistryTests, ParsesOptionalPresentationAnimation) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "presentation": {
    "model": "arvane:model/entity/pig",
    "animation": "arvane:animation/pig"
  }
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result =
      arv::base::build_base_entity_registry(std::move(parsed.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const std::optional<arv::EntityTypeID> pig_id =
      registry_result.value().find_type("arvane:pig");
  ASSERT_TRUE(pig_id.has_value());
  const arv::entity::EntityTypeRecord* pig = registry_result.value().type(pig_id.value());
  ASSERT_NE(pig, nullptr);
  EXPECT_EQ(pig->animation_asset, "arvane:animation/pig");
}

TEST(EntityRegistryTests, RejectsInvalidPresentationAnimationAssetKind) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "presentation": {
    "model": "arvane:model/entity/pig",
    "animation": "arvane:model/entity/pig"
  }
}
)json");

  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("presentation.animation"), std::string::npos);
}

TEST(EntityRegistryTests, RejectsUnknownAuthoredComponentIDs) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "components": {
    "arvane:not_a_component": {}
  }
}
)json");

  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("unknown entity component ID"), std::string::npos);
}

TEST(EntityRegistryTests, RejectsUnknownBehaviorGoalIDs) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "components": {
    "arvane:brain": {
      "goals": [
        { "type": "arvane:does_not_exist", "priority": 1 }
      ]
    }
  }
}
)json");

  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result =
      arv::base::build_base_entity_registry(std::move(parsed.value()));
  ASSERT_FALSE(registry_result);
  EXPECT_NE(registry_result.error().message.find("unknown behavior goal ID"), std::string::npos);
}

TEST(EntityRegistryTests, RejectsDuplicateExplicitEntityNames) {
  std::vector<arv::entity::EntityDefinition> definitions;
  definitions.push_back({.name = "arvane:pig"});
  definitions.push_back({.name = "arvane:pig"});

  auto registry_result =
      arv::base::build_base_entity_registry(std::move(definitions));
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
      arv::base::build_base_entity_registry(std::move(definitions));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const arv::entity::EntityTypeRecord* player_record =
      registry_result.value().type(arv::entity::PlayerEntityTypeID);
  ASSERT_NE(player_record, nullptr);
  EXPECT_EQ(player_record->display_name, "Custom Player");
  EXPECT_FALSE(player_record->spawnable);
  EXPECT_TRUE(player_record->persistent_by_default);
  EXPECT_TRUE(player_record->replicated_by_default);
}
