#include "arv/client/item_animation.hpp"

#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <utility>

TEST(ItemAnimationTests, ParsesAndSamplesFirstPersonTransformChannels) {
  auto library_result = arv::client::parse_item_animation_library_json(R"json(
{
  "animations": [
    {
      "name": "arvane:swing/default",
      "duration_seconds": 0.20,
      "channels": {
        "first_person": {
          "position": [
            { "time": 0.10, "value": [0.0, 0.0, 1.0] },
            { "time": 0.0, "value": [0.0, 0.0, 0.0] }
          ],
          "rotation_degrees": [
            { "time": 0.0, "value": [0.0, 0.0, 0.0] },
            { "time": 0.10, "value": [10.0, 20.0, 30.0] }
          ]
        }
      }
    }
  ]
}
)json");
  ASSERT_TRUE(library_result) << library_result.error().message;
  const arv::client::ItemAnimationLibrary& library = library_result.value();
  const arv::client::ItemAnimationClip* clip =
      library.find(arv::client::DefaultHeldItemSwingAnimationName);
  ASSERT_NE(clip, nullptr);

  arv::client::HeldItemAnimationPlayer player;
  player.play(*clip, 3.0F);
  const arv::client::HeldItemAnimationPose pose = player.sample(3.05F);

  EXPECT_NEAR(pose.position.z, 0.5F, 0.0001F);
  EXPECT_NEAR(pose.rotation_degrees.x, 5.0F, 0.0001F);
  EXPECT_NEAR(pose.rotation_degrees.y, 10.0F, 0.0001F);
  EXPECT_NEAR(pose.rotation_degrees.z, 15.0F, 0.0001F);
  EXPECT_FLOAT_EQ(pose.scale.x, 1.0F);
  EXPECT_FLOAT_EQ(pose.scale.y, 1.0F);
  EXPECT_FLOAT_EQ(pose.scale.z, 1.0F);
}

TEST(ItemAnimationTests, ResolvesDefaultSwingAndExplicitDisable) {
  auto library_result = arv::client::parse_item_animation_library_json(R"json(
{
  "animations": [
    { "name": "arvane:swing/default", "duration_seconds": 0.20 }
  ]
}
)json");
  ASSERT_TRUE(library_result) << library_result.error().message;
  const arv::client::ItemAnimationLibrary& library = library_result.value();

  arv::player::ItemRecord item;
  item.name = "arvane:test_item";
  EXPECT_EQ(
      arv::client::resolve_held_item_animation(
          &item,
          arv::client::HeldItemActionPrimaryUse,
          library),
      library.find(arv::client::DefaultHeldItemSwingAnimationName));
  EXPECT_EQ(
      arv::client::resolve_held_item_animation(
          &item,
          arv::client::HeldItemActionSecondaryUseStart,
          library),
      library.find(arv::client::DefaultHeldItemSwingAnimationName));

  item.placement_variant = arv::voxel::BlockVariantID{1};
  const arv::client::HeldItemAnimationResolution block_secondary =
      arv::client::resolve_held_item_animation_binding(
          &item,
          arv::client::HeldItemActionSecondaryUseStart,
          library);
  EXPECT_EQ(block_secondary.clip, library.find(arv::client::DefaultHeldItemSwingAnimationName));
  EXPECT_EQ(
      block_secondary.play_condition,
      arv::player::HeldItemAnimationPlayCondition::PlacementSucceeds);

  item.placement_variant.reset();
  arv::player::HeldItemAnimationBinding filtered_default;
  filtered_default.selection = arv::player::HeldItemAnimationSelection::Default;
  filtered_default.play_condition = arv::player::HeldItemAnimationPlayCondition::PlacementSucceeds;
  item.held_animations.insert_or_assign(
      std::string{arv::client::HeldItemActionSecondaryUseStart},
      filtered_default);
  const arv::client::HeldItemAnimationResolution filtered_secondary =
      arv::client::resolve_held_item_animation_binding(
          &item,
          arv::client::HeldItemActionSecondaryUseStart,
          library);
  EXPECT_EQ(filtered_secondary.clip, library.find(arv::client::DefaultHeldItemSwingAnimationName));
  EXPECT_EQ(
      filtered_secondary.play_condition,
      arv::player::HeldItemAnimationPlayCondition::PlacementSucceeds);

  arv::player::HeldItemAnimationBinding disabled;
  disabled.selection = arv::player::HeldItemAnimationSelection::Disabled;
  item.held_animations.insert_or_assign(
      std::string{arv::client::HeldItemActionPrimaryUse},
      disabled);
  item.held_animations.insert_or_assign(
      std::string{arv::client::HeldItemActionSecondaryUseStart},
      disabled);
  EXPECT_EQ(
      arv::client::resolve_held_item_animation(
          &item,
          arv::client::HeldItemActionPrimaryUse,
          library),
      nullptr);
  EXPECT_EQ(
      arv::client::resolve_held_item_animation(
          &item,
          arv::client::HeldItemActionSecondaryUseStart,
          library),
      nullptr);
}

TEST(ItemAnimationTests, ParsesHeldAnimationBindingObjects) {
  auto definitions = arv::player::parse_item_definitions_json(R"json(
{
  "items": [
    {
      "name": "arvane:water_bucket",
      "display_name": "Water Bucket",
      "render_type": "texture",
      "texture": "arvane:texture/item/water_bucket",
      "held_animations": {
        "primary_use": null,
        "secondary_use_start": {
          "play_when": "placement_succeeds"
        },
        "inspect": {
          "animation": "arvane:swing/default",
          "play_when": "always"
        }
      }
    }
  ]
}
)json");
  ASSERT_TRUE(definitions) << definitions.error().message;
  ASSERT_EQ(definitions.value().size(), 1U);

  const auto& bindings = definitions.value().front().held_animations;
  ASSERT_EQ(bindings.size(), 3U);

  const auto primary = bindings.find("primary_use");
  ASSERT_NE(primary, bindings.end());
  EXPECT_EQ(primary->second.selection, arv::player::HeldItemAnimationSelection::Disabled);

  const auto secondary = bindings.find("secondary_use_start");
  ASSERT_NE(secondary, bindings.end());
  EXPECT_EQ(secondary->second.selection, arv::player::HeldItemAnimationSelection::Default);
  EXPECT_EQ(
      secondary->second.play_condition,
      arv::player::HeldItemAnimationPlayCondition::PlacementSucceeds);

  const auto inspect = bindings.find("inspect");
  ASSERT_NE(inspect, bindings.end());
  EXPECT_EQ(inspect->second.selection, arv::player::HeldItemAnimationSelection::Named);
  EXPECT_EQ(inspect->second.animation, "arvane:swing/default");
}

TEST(ItemAnimationTests, ValidatesItemAnimationReferences) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  auto definitions = arv::player::parse_item_definitions_json(R"json(
{
  "items": [
    {
      "name": "arvane:coal",
      "display_name": "Coal",
      "render_type": "texture",
      "texture": "arvane:texture/item/coal",
      "held_animations": {
        "primary_use": "arvane:swing/missing"
      }
    }
  ]
}
)json");
  ASSERT_TRUE(definitions) << definitions.error().message;
  auto registry_result = arv::player::ItemRegistry::build_from_blocks_and_items(
      block_registry,
      std::move(definitions.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;

  auto library_result = arv::client::parse_item_animation_library_json(R"json(
{
  "animations": [
    { "name": "arvane:swing/default", "duration_seconds": 0.20 }
  ]
}
)json");
  ASSERT_TRUE(library_result) << library_result.error().message;

  auto validation = arv::client::validate_item_animation_references(
      registry_result.value(),
      library_result.value());
  EXPECT_FALSE(validation);
}
