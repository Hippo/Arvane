#include "arv/render/entity_model.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

} // namespace

TEST(EntityModelTests, ParsesPigSkeletonTexturesAndAnimations) {
  const std::filesystem::path pig_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "pig.json";
  auto parsed = arv::render::parse_entity_model_json(read_text(pig_path));

  ASSERT_TRUE(parsed) << parsed.error().message;
  EXPECT_EQ(parsed.value().name, "pig");
  ASSERT_EQ(parsed.value().bones.size(), 6U);
  EXPECT_EQ(parsed.value().bones.front().name, "body");
  ASSERT_EQ(parsed.value().texture_assets.size(), 1U);
  EXPECT_EQ(parsed.value().texture_assets.front(), "arvane:texture/entity/pig");
  ASSERT_EQ(parsed.value().texture_sizes.size(), 1U);
  EXPECT_EQ(parsed.value().texture_sizes.front(), (arv::Vec2i{64, 64}));
  ASSERT_EQ(parsed.value().animations.size(), 1U);
  EXPECT_EQ(parsed.value().animations.front().name, "walking");
  EXPECT_TRUE(parsed.value().animations.front().loop);
  EXPECT_EQ(parsed.value().animations.front().keyframes.size(), 4U);
}

TEST(EntityModelTests, BuildsPigCubeMeshFromSkeletonBoxes) {
  const std::filesystem::path pig_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "pig.json";
  auto parsed = arv::render::parse_entity_model_json(read_text(pig_path));
  ASSERT_TRUE(parsed) << parsed.error().message;

  const std::vector<arv::render::EntityModelTextureBinding> bindings{{"arvane:texture/entity/pig",
                                                                      {2, 2},
                                                                      {130, 130},
                                                                      128,
                                                                      128}};
  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{7},
      parsed.value(),
      bindings);

  ASSERT_TRUE(model) << model.error().message;
  EXPECT_EQ(model.value().id, (arv::render::EntityModelID{7}));
  EXPECT_EQ(model.value().bones.size(), 6U);
  EXPECT_EQ(model.value().vertices.size(), 168U);
  EXPECT_EQ(model.value().indices.size(), 252U);
  ASSERT_FALSE(model.value().vertices.empty());
  EXPECT_EQ(model.value().vertices.front().bone_index, 0U);
  EXPECT_FLOAT_EQ(model.value().vertices.front().uv.x, 94.0F);
  EXPECT_FLOAT_EQ(model.value().vertices.front().uv.y, 66.0F);
  ASSERT_GT(model.value().vertices.size(), 20U);
  EXPECT_FLOAT_EQ(model.value().vertices[20].uv.x, 94.0F);
  EXPECT_FLOAT_EQ(model.value().vertices[20].uv.y, 34.0F);
}

TEST(EntityModelTests, SkipsFacesWithNegativeTextureIndex) {
  const std::string text = R"json({
    "name": "hidden_face",
    "textures": ["pig.png"],
    "texture_sizes": [[64, 64]],
    "skeleton": [{
      "name": "root",
      "pivot": [0, 0, 0],
      "boxes": [{
        "min": [0, 0, 0],
        "max": [16, 16, 16],
        "north": {"texture": -1}
      }]
    }]
  })json";
  auto parsed = arv::render::parse_entity_model_json(text);
  ASSERT_TRUE(parsed) << parsed.error().message;

  const std::vector<arv::render::EntityModelTextureBinding> bindings{{"arvane:texture/entity/pig",
                                                                      {0, 0},
                                                                      {128, 128},
                                                                      128,
                                                                      128}};
  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{9},
      parsed.value(),
      bindings);

  ASSERT_TRUE(model) << model.error().message;
  EXPECT_TRUE(model.value().vertices.empty());
  EXPECT_TRUE(model.value().indices.empty());
}

TEST(EntityModelTests, SamplesLoopingWalkingAnimation) {
  const std::filesystem::path pig_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "pig.json";
  auto parsed = arv::render::parse_entity_model_json(read_text(pig_path));
  ASSERT_TRUE(parsed) << parsed.error().message;

  const std::vector<arv::render::EntityModelTextureBinding> bindings{{"arvane:texture/entity/pig",
                                                                      {0, 0},
                                                                      {64, 64},
                                                                      64,
                                                                      64}};
  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{1},
      parsed.value(),
      bindings);
  ASSERT_TRUE(model) << model.error().message;

  std::vector<arv::Vec3f> rotations(model.value().bones.size());
  arv::render::sample_entity_model_animation(
      model.value(),
      "walking",
      0.5F,
      std::span<arv::Vec3f>{rotations});

  ASSERT_GT(rotations.size(), 3U);
  EXPECT_EQ(rotations[2], (arv::Vec3f{45.0F, 0.0F, 0.0F}));
  EXPECT_EQ(rotations[3], (arv::Vec3f{-45.0F, 0.0F, 0.0F}));

  arv::render::sample_entity_model_animation(
      model.value(),
      "walking",
      2.0F,
      std::span<arv::Vec3f>{rotations});
  EXPECT_EQ(rotations[2], (arv::Vec3f{45.0F, 0.0F, 0.0F}));

  arv::render::sample_entity_model_animation(
      model.value(),
      "",
      0.5F,
      std::span<arv::Vec3f>{rotations});
  EXPECT_EQ(rotations[2], (arv::Vec3f{}));
}

TEST(EntityModelTests, MissingAnimationBoneKeysHoldPreviousRotation) {
  const std::string text = R"json({
    "name": "hold_missing_keys",
    "textures": ["pig.png"],
    "texture_sizes": [[64, 64]],
    "skeleton": [{
      "name": "body",
      "pivot": [0, 0, 0],
      "boxes": []
    }],
    "animations": [{
      "name": "crouched",
      "length": 2,
      "loop": "loop",
      "keyframes": [
        {
          "time": 0,
          "bones": [{
            "name": "body",
            "rotation_euler": [30, 0, 0]
          }]
        },
        {
          "time": 1,
          "bones": []
        },
        {
          "time": 2,
          "bones": []
        }
      ]
    }]
  })json";
  auto parsed = arv::render::parse_entity_model_json(text);
  ASSERT_TRUE(parsed) << parsed.error().message;

  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{13},
      parsed.value(),
      std::span<const arv::render::EntityModelTextureBinding>{});
  ASSERT_TRUE(model) << model.error().message;

  std::vector<arv::Vec3f> rotations(model.value().bones.size());
  arv::render::sample_entity_model_animation(
      model.value(),
      "crouched",
      0.75F,
      std::span<arv::Vec3f>{rotations});
  ASSERT_EQ(rotations.size(), 1U);
  EXPECT_EQ(rotations[0], (arv::Vec3f{30.0F, 0.0F, 0.0F}));

  arv::render::sample_entity_model_animation(
      model.value(),
      "crouched",
      1.5F,
      std::span<arv::Vec3f>{rotations});
  EXPECT_EQ(rotations[0], (arv::Vec3f{30.0F, 0.0F, 0.0F}));

  arv::render::sample_entity_model_animation(
      model.value(),
      "crouched",
      2.25F,
      std::span<arv::Vec3f>{rotations});
  EXPECT_EQ(rotations[0], (arv::Vec3f{30.0F, 0.0F, 0.0F}));
}

TEST(EntityModelTests, SamplesAnimationPositionOffsetsIndependentlyFromRotation) {
  const std::string text = R"json({
    "name": "position_channels",
    "textures": ["pig.png"],
    "texture_sizes": [[64, 64]],
    "skeleton": [{
      "name": "body",
      "pivot": [0, 0, 0],
      "boxes": []
    }],
    "animations": [
      {
        "name": "slide",
        "length": 1,
        "loop": "",
        "keyframes": [
          {
            "time": 0,
            "bones": [{
              "name": "body",
              "position": [2, 0, 0]
            }]
          },
          {
            "time": 1,
            "bones": [{
              "name": "body",
              "position": [4, 2, -2]
            }]
          }
        ]
      },
      {
        "name": "hold_position",
        "length": 2,
        "loop": "loop",
        "keyframes": [
          {
            "time": 0,
            "bones": [{
              "name": "body",
              "position": [2, 0, 0]
            }]
          },
          {
            "time": 1,
            "bones": []
          },
          {
            "time": 2,
            "bones": []
          }
        ]
      }
    ]
  })json";
  auto parsed = arv::render::parse_entity_model_json(text);
  ASSERT_TRUE(parsed) << parsed.error().message;

  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{15},
      parsed.value(),
      std::span<const arv::render::EntityModelTextureBinding>{});
  ASSERT_TRUE(model) << model.error().message;

  std::vector<arv::Vec3f> rotations(model.value().bones.size());
  std::vector<arv::Vec3f> positions(model.value().bones.size());
  arv::render::sample_entity_model_animation(
      model.value(),
      "slide",
      0.5F,
      std::span<arv::Vec3f>{rotations},
      std::span<arv::Vec3f>{positions});
  ASSERT_EQ(rotations.size(), 1U);
  ASSERT_EQ(positions.size(), 1U);
  EXPECT_EQ(rotations[0], (arv::Vec3f{}));
  EXPECT_EQ(positions[0], (arv::Vec3f{-3.0F, 1.0F, -1.0F}));

  arv::render::sample_entity_model_animation(
      model.value(),
      "hold_position",
      1.5F,
      std::span<arv::Vec3f>{rotations},
      std::span<arv::Vec3f>{positions});
  EXPECT_EQ(positions[0], (arv::Vec3f{-2.0F, 0.0F, 0.0F}));

  arv::render::sample_entity_model_animation(
      model.value(),
      "hold_position",
      2.25F,
      std::span<arv::Vec3f>{rotations},
      std::span<arv::Vec3f>{positions});
  EXPECT_EQ(positions[0], (arv::Vec3f{-2.0F, 0.0F, 0.0F}));
}

TEST(EntityModelTests, PlayerModelPreservesInputTextureAndMarksSkinLayers) {
  const std::filesystem::path player_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "player.json";
  auto parsed = arv::render::parse_entity_model_json(read_text(player_path));
  ASSERT_TRUE(parsed) << parsed.error().message;

  ASSERT_EQ(parsed.value().texture_assets.size(), 1U);
  EXPECT_EQ(parsed.value().texture_assets.front(), "$0");
  const std::vector<arv::render::EntityModelTextureBinding> bindings{{
      "arvane:texture/default_skin/default",
      {0, 0},
      {64, 64},
      64,
      64}};
  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{11},
      parsed.value(),
      bindings);

  ASSERT_TRUE(model) << model.error().message;
  ASSERT_FALSE(model.value().vertices.empty());
  std::optional<arv::Vec3f> left_arm_pivot;
  std::optional<arv::Vec3f> right_arm_pivot;
  for (const arv::render::EntityModelBone& bone : model.value().bones) {
    if (bone.name == "left_arm") {
      left_arm_pivot = bone.pivot;
    }
    if (bone.name == "right_arm") {
      right_arm_pivot = bone.pivot;
    }
  }
  ASSERT_TRUE(left_arm_pivot.has_value());
  ASSERT_TRUE(right_arm_pivot.has_value());
  EXPECT_EQ(left_arm_pivot.value(), (arv::Vec3f{-6.0F, 24.0F, 1.0F}));
  EXPECT_EQ(right_arm_pivot.value(), (arv::Vec3f{6.0F, 24.0F, 1.0F}));

  EXPECT_EQ(
      model.value().vertices.front().flags,
      arv::render::EntityModelMeshVertexFlagSkinBaseLayer);
  bool saw_decor = false;
  bool saw_base = false;
  arv::Vec3f base_min{};
  arv::Vec3f base_max{};
  arv::Vec3f decor_min{};
  arv::Vec3f decor_max{};
  for (const arv::render::EntityModelMeshVertex& vertex : model.value().vertices) {
    if (vertex.flags == arv::render::EntityModelMeshVertexFlagSkinBaseLayer) {
      if (!saw_base) {
        base_min = vertex.position;
        base_max = vertex.position;
        saw_base = true;
      } else {
        base_min.x = std::min(base_min.x, vertex.position.x);
        base_min.y = std::min(base_min.y, vertex.position.y);
        base_min.z = std::min(base_min.z, vertex.position.z);
        base_max.x = std::max(base_max.x, vertex.position.x);
        base_max.y = std::max(base_max.y, vertex.position.y);
        base_max.z = std::max(base_max.z, vertex.position.z);
      }
    }
    if (vertex.flags == arv::render::EntityModelMeshVertexFlagSkinDecorLayer) {
      if (!saw_decor) {
        decor_min = vertex.position;
        decor_max = vertex.position;
        saw_decor = true;
      } else {
        decor_min.x = std::min(decor_min.x, vertex.position.x);
        decor_min.y = std::min(decor_min.y, vertex.position.y);
        decor_min.z = std::min(decor_min.z, vertex.position.z);
        decor_max.x = std::max(decor_max.x, vertex.position.x);
        decor_max.y = std::max(decor_max.y, vertex.position.y);
        decor_max.z = std::max(decor_max.z, vertex.position.z);
      }
    }
  }
  EXPECT_TRUE(saw_base);
  EXPECT_TRUE(saw_decor);
  EXPECT_LT(decor_min.x, base_min.x);
  EXPECT_LT(decor_min.y, base_min.y);
  EXPECT_LT(decor_min.z, base_min.z);
  EXPECT_GT(decor_max.x, base_max.x);
  EXPECT_GT(decor_max.y, base_max.y);
  EXPECT_GT(decor_max.z, base_max.z);
}

TEST(EntityModelTests, PlayerSneakAnimationsKeepBodyCrouchedForward) {
  const std::filesystem::path player_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "player.json";
  auto parsed = arv::render::parse_entity_model_json(read_text(player_path));
  ASSERT_TRUE(parsed) << parsed.error().message;

  const std::vector<arv::render::EntityModelTextureBinding> bindings{{
      "arvane:texture/default_skin/default",
      {0, 0},
      {64, 64},
      64,
      64}};
  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{14},
      parsed.value(),
      bindings);
  ASSERT_TRUE(model) << model.error().message;

  std::optional<arv::usize> body_index;
  for (arv::usize index = 0; index < model.value().bones.size(); ++index) {
    if (model.value().bones[index].name == "body") {
      body_index = index;
      break;
    }
  }
  ASSERT_TRUE(body_index.has_value());

  std::vector<arv::Vec3f> rotations(model.value().bones.size());
  arv::render::sample_entity_model_animation(
      model.value(),
      "sneak",
      0.25F,
      std::span<arv::Vec3f>{rotations});
  const arv::Vec3f sneak_final_body_rotation = rotations[body_index.value()];

  arv::render::sample_entity_model_animation(
      model.value(),
      "sneak",
      10.0F,
      std::span<arv::Vec3f>{rotations});
  EXPECT_EQ(rotations[body_index.value()], sneak_final_body_rotation);

  arv::render::sample_entity_model_animation(
      model.value(),
      "sneak_idle",
      1.5F,
      std::span<arv::Vec3f>{rotations});
  EXPECT_EQ(rotations[body_index.value()], (arv::Vec3f{-30.0F, 0.0F, 0.0F}));

  arv::render::sample_entity_model_animation(
      model.value(),
      "sneak_walk",
      1.5F,
      std::span<arv::Vec3f>{rotations});
  EXPECT_EQ(rotations[body_index.value()], (arv::Vec3f{-30.0F, 0.0F, 0.0F}));
}

TEST(EntityModelTests, PlayerModelSkinVariantsKeepSkinLayerFlags) {
  const std::filesystem::path player_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "player.json";
  auto parsed = arv::render::parse_entity_model_json(read_text(player_path));
  ASSERT_TRUE(parsed) << parsed.error().message;
  parsed.value().name = "player#first_person";

  const std::vector<arv::render::EntityModelTextureBinding> bindings{{
      "arvane:texture/default_skin/default",
      {0, 0},
      {64, 64},
      64,
      64}};
  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{12},
      parsed.value(),
      bindings);

  ASSERT_TRUE(model) << model.error().message;
  ASSERT_FALSE(model.value().vertices.empty());
  EXPECT_EQ(
      model.value().vertices.front().flags,
      arv::render::EntityModelMeshVertexFlagSkinBaseLayer);
  bool saw_decor = false;
  for (const arv::render::EntityModelMeshVertex& vertex : model.value().vertices) {
    saw_decor = saw_decor ||
                vertex.flags == arv::render::EntityModelMeshVertexFlagSkinDecorLayer;
  }
  EXPECT_TRUE(saw_decor);
}
