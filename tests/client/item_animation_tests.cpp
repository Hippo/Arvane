#include "arv/client/item_animation.hpp"

#include "arv/asset/pack.hpp"
#include "support/test_registries.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <gtest/gtest.h>

#include <utility>

namespace {

void write_item_animation_test_pack(
    const std::filesystem::path& packs_root,
    std::string_view item_animation_json) {
  arv::testsupport::write_text(
      packs_root / "test_pack" / "pack.yaml",
      "schema_version: 1\n"
      "id: \"test_pack\"\n"
      "display_name: \"test_pack\"\n"
      "version: \"1.0.0\"\n"
      "namespaces:\n"
      "  - \"test\"\n"
      "side: \"both\"\n"
      "dependencies: []\n"
      "load_after: []\n"
      "load_before: []\n"
      "capabilities:\n"
      "  assets: false\n"
      "  data: true\n"
      "  native_plugin: false\n");
  arv::testsupport::write_text(
      packs_root / "test_pack" / "data" / "test" / "item_animations.json",
      std::string{item_animation_json});
}

[[nodiscard]] arv::player::ItemRegistry test_item_registry() {
  const arv::voxel::BlockRegistry blocks =
      arv::testsupport::make_test_block_registry();
  return arv::testsupport::make_test_item_registry(blocks);
}

} // namespace

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

TEST(ItemAnimationTests, LoadsAndValidatesLibraryFromPacks) {
  auto packs =
      arv::asset::PackManager::load_from_packs_root("arvane_home/packs");
  ASSERT_TRUE(packs) << packs.error().describe();

  auto library = arv::client::load_item_animation_library_from_packs(
      packs.value(),
      test_item_registry());

  ASSERT_TRUE(library) << library.error().operation << ": " << library.error().message;
  EXPECT_NE(
      library.value().find(arv::client::DefaultHeldItemSwingAnimationName),
      nullptr);
}

TEST(ItemAnimationTests, ReportsPackLoadedLibraryValidationFailure) {
  arv::testsupport::ScopedTempDir temp("arvane_item_animation_pack_");
  const std::filesystem::path packs_root = temp.path / "packs";
  write_item_animation_test_pack(
      packs_root,
      R"json({
  "animations": [
    { "name": "test:swing/alternate", "duration_seconds": 0.10 }
  ]
})json");
  auto packs = arv::asset::PackManager::load_from_packs_root(packs_root);
  ASSERT_TRUE(packs) << packs.error().describe();

  auto library = arv::client::load_item_animation_library_from_packs(
      packs.value(),
      test_item_registry());

  ASSERT_FALSE(library);
  EXPECT_EQ(library.error().operation, "validate");
  EXPECT_NE(
      library.error().message.find(
          std::string{arv::client::DefaultHeldItemSwingAnimationName}),
      std::string::npos);
}

TEST(ItemAnimationTests, SharedHeldItemPlaybackStartsHoldsAndEndsSecondaryUse) {
  auto library_result = arv::client::parse_item_animation_library_json(R"json(
{
  "animations": [
    { "name": "arvane:swing/default", "duration_seconds": 0.10 },
    { "name": "arvane:use/start", "duration_seconds": 0.10 },
    { "name": "arvane:use/hold", "duration_seconds": 0.50, "loop": true },
    { "name": "arvane:use/end", "duration_seconds": 0.10 }
  ]
}
)json");
  ASSERT_TRUE(library_result) << library_result.error().message;
  const arv::client::ItemAnimationLibrary& library = library_result.value();

  arv::player::ItemRecord item;
  item.name = "arvane:test_item";
  item.held_animations.insert_or_assign(
      std::string{arv::client::HeldItemActionSecondaryUseStart},
      arv::player::HeldItemAnimationBinding{
          .selection = arv::player::HeldItemAnimationSelection::Named,
          .animation = "arvane:use/start"});
  item.held_animations.insert_or_assign(
      std::string{arv::client::HeldItemActionSecondaryUseHold},
      arv::player::HeldItemAnimationBinding{
          .selection = arv::player::HeldItemAnimationSelection::Named,
          .animation = "arvane:use/hold"});
  item.held_animations.insert_or_assign(
      std::string{arv::client::HeldItemActionSecondaryUseEnd},
      arv::player::HeldItemAnimationBinding{
          .selection = arv::player::HeldItemAnimationSelection::Named,
          .animation = "arvane:use/end"});

  arv::client::HeldItemAnimationPlayer player;
  bool secondary_active = false;
  arv::client::update_held_item_animation_playback(
      player,
      secondary_active,
      {.secondary_pressed = true, .secondary_held = true},
      &item,
      library,
      {},
      1.0F);

  ASSERT_TRUE(secondary_active);
  ASSERT_NE(player.active_clip(), nullptr);
  EXPECT_EQ(player.active_clip()->name, "arvane:use/start");

  arv::client::update_held_item_animation_playback(
      player,
      secondary_active,
      {.secondary_held = true},
      &item,
      library,
      {},
      1.2F);

  ASSERT_TRUE(secondary_active);
  ASSERT_NE(player.active_clip(), nullptr);
  EXPECT_EQ(player.active_clip()->name, "arvane:use/hold");

  arv::client::update_held_item_animation_playback(
      player,
      secondary_active,
      {},
      &item,
      library,
      {},
      1.25F);

  EXPECT_FALSE(secondary_active);
  ASSERT_NE(player.active_clip(), nullptr);
  EXPECT_EQ(player.active_clip()->name, "arvane:use/end");
}

TEST(ItemAnimationTests, PlaybackFrameDriverHonorsUnavailablePolicy) {
  auto library_result = arv::client::parse_item_animation_library_json(R"json(
{
  "animations": [
    { "name": "arvane:swing/default", "duration_seconds": 0.10 }
  ]
}
)json");
  ASSERT_TRUE(library_result) << library_result.error().message;
  const arv::client::ItemAnimationLibrary& library = library_result.value();
  const arv::client::ItemAnimationClip* clip =
      library.find(arv::client::DefaultHeldItemSwingAnimationName);
  ASSERT_NE(clip, nullptr);

  arv::client::HeldItemAnimationPlayer player;
  player.play(*clip, 1.0F);
  bool secondary_active = true;

  const arv::client::HeldItemAnimationPlaybackFrameResult clear_result =
      arv::client::update_held_item_animation_playback_from_frame(
          player,
          secondary_active,
          library,
          {
              .animations_available = false,
              .unavailable_policy =
                  arv::client::HeldItemAnimationUnavailablePolicy::ClearSecondaryUseOnly,
              .now_seconds = 1.05F,
          });

  EXPECT_TRUE(clear_result.cleared_secondary_use);
  EXPECT_FALSE(clear_result.stopped);
  EXPECT_FALSE(clear_result.advanced);
  EXPECT_FALSE(secondary_active);
  EXPECT_EQ(player.active_clip(), clip);

  secondary_active = true;
  const arv::client::HeldItemAnimationPlaybackFrameResult stop_result =
      arv::client::update_held_item_animation_playback_from_frame(
          player,
          secondary_active,
          library,
          {
              .animations_available = false,
              .now_seconds = 1.10F,
          });

  EXPECT_FALSE(stop_result.cleared_secondary_use);
  EXPECT_TRUE(stop_result.stopped);
  EXPECT_FALSE(stop_result.advanced);
  EXPECT_FALSE(secondary_active);
  EXPECT_EQ(player.active_clip(), nullptr);
}

TEST(ItemAnimationTests, PlaybackFrameDriverBuildsEdgeInputAndAdvances) {
  auto library_result = arv::client::parse_item_animation_library_json(R"json(
{
  "animations": [
    { "name": "arvane:swing/default", "duration_seconds": 0.10 }
  ]
}
)json");
  ASSERT_TRUE(library_result) << library_result.error().message;
  const arv::client::ItemAnimationLibrary& library = library_result.value();

  arv::player::ItemRecord item;
  item.name = "arvane:test_pickaxe";

  arv::client::HeldItemAnimationPlayer player;
  bool secondary_active = false;
  const arv::client::HeldItemAnimationPlaybackFrameResult result =
      arv::client::update_held_item_animation_playback_from_frame(
          player,
          secondary_active,
          library,
          {
              .primary_held = true,
              .block_break_active = true,
              .held_item = &item,
              .now_seconds = 2.0F,
          });

  EXPECT_FALSE(result.cleared_secondary_use);
  EXPECT_FALSE(result.stopped);
  EXPECT_TRUE(result.advanced);
  ASSERT_NE(player.active_clip(), nullptr);
  EXPECT_EQ(
      player.active_clip()->name,
      std::string{arv::client::DefaultHeldItemSwingAnimationName});
}

TEST(ItemAnimationTests, SharedHeldItemPlaybackSuppressesFilteredPlacementUse) {
  auto library_result = arv::client::parse_item_animation_library_json(R"json(
{
  "animations": [
    { "name": "arvane:swing/default", "duration_seconds": 0.10 }
  ]
}
)json");
  ASSERT_TRUE(library_result) << library_result.error().message;

  arv::player::ItemRecord item;
  item.name = "arvane:test_block";
  item.placement_variant = arv::voxel::BlockVariantID{1};

  arv::client::HeldItemAnimationPlayer player;
  bool secondary_active = false;
  arv::client::update_held_item_animation_playback(
      player,
      secondary_active,
      {.secondary_pressed = true, .secondary_held = true},
      &item,
      library_result.value(),
      {.placement_succeeds = false},
      1.0F);

  EXPECT_FALSE(secondary_active);
  EXPECT_EQ(player.active_clip(), nullptr);
}

TEST(ItemAnimationTests, BuildsHeldItemAnimationInputFromEdges) {
  const arv::client::HeldItemAnimationInputState input =
      arv::client::held_item_animation_input_from_edges(
          true,
          true,
          false,
          true,
          true);

  EXPECT_TRUE(input.primary_pressed);
  EXPECT_TRUE(input.repeat_primary_use);
  EXPECT_FALSE(input.secondary_pressed);
  EXPECT_TRUE(input.secondary_held);

  const arv::client::HeldItemAnimationInputState no_repeat =
      arv::client::held_item_animation_input_from_edges(
          false,
          true,
          true,
          false,
          false);

  EXPECT_FALSE(no_repeat.primary_pressed);
  EXPECT_FALSE(no_repeat.repeat_primary_use);
  EXPECT_TRUE(no_repeat.secondary_pressed);
  EXPECT_FALSE(no_repeat.secondary_held);
}

TEST(ItemAnimationTests, StopHeldItemAnimationPlaybackClearsSecondaryLatch) {
  arv::client::HeldItemAnimationPlayer player;
  bool secondary_active = true;

  arv::client::stop_held_item_animation_playback(
      player,
      secondary_active,
      3.0);

  EXPECT_FALSE(secondary_active);
  EXPECT_EQ(player.active_clip(), nullptr);
}
