#include "arv/render/entity_model.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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
  EXPECT_FLOAT_EQ(model.value().vertices.front().uv.x, 58.0F);
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
