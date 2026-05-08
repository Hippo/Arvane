#include "arv/render/entity_model.hpp"
#include "arv/voxel/block.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::optional<arv::usize> find_bone_index(
    const arv::render::EntityModelAsset& model,
    std::string_view name) {
  for (arv::usize index = 0; index < model.bones.size(); ++index) {
    if (model.bones[index].name == name) {
      return index;
    }
  }
  return std::nullopt;
}

} // namespace

TEST(EntityModelTests, LoadsBlockModelWithSharedSchema) {
  const std::filesystem::path stairs_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "block" / "stairs.json";
  const std::vector<std::string> inputs{"arvane:texture/block/cobblestone"};
  auto parsed = arv::voxel::parse_block_model_json(read_text(stairs_path), inputs);

  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed.value().texture_assets.size(), 1U);
  EXPECT_EQ(parsed.value().texture_assets.front(), "arvane:texture/block/cobblestone");
  ASSERT_EQ(parsed.value().boxes.size(), 2U);
  EXPECT_EQ(parsed.value().boxes.front().bone, "");
  EXPECT_EQ(parsed.value().boxes.front().pivot, (arv::Vec3f{0.0F, 0.0F, 0.0F}));
  EXPECT_EQ(parsed.value().boxes.front().min, (arv::Vec3f{0.0F, 0.0F, 0.0F}));
  EXPECT_EQ(parsed.value().boxes.front().max, (arv::Vec3f{16.0F, 8.0F, 16.0F}));
}

TEST(EntityModelTests, LoadsEntityModelWithSharedSchemaAndBoneNames) {
  const std::filesystem::path pig_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "pig.json";
  auto parsed = arv::render::parse_entity_model_json(read_text(pig_path));

  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed.value().texture_assets.size(), 1U);
  EXPECT_EQ(parsed.value().texture_assets.front(), "arvane:texture/entity/pig");
  ASSERT_EQ(parsed.value().texture_sizes.size(), 1U);
  EXPECT_EQ(parsed.value().texture_sizes.front(), (arv::Vec2i{64, 64}));
  ASSERT_EQ(parsed.value().boxes.size(), 7U);
  EXPECT_EQ(parsed.value().boxes.front().bone, "body");
  EXPECT_EQ(parsed.value().boxes.front().pivot, (arv::Vec3f{0.0F, 0.0F, 0.0F}));

  const auto head = std::find_if(
      parsed.value().boxes.begin(),
      parsed.value().boxes.end(),
      [](const arv::render::EntityModelBoxDefinition& box) {
        return box.bone == "head";
      });
  ASSERT_NE(head, parsed.value().boxes.end());
  EXPECT_EQ(head->pivot, (arv::Vec3f{0.0F, 8.0F, -4.0F}));
}

TEST(EntityModelTests, BuildsEntityMeshAndPivotsFromBoxBones) {
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

  const std::optional<arv::usize> head_index = find_bone_index(model.value(), "head");
  ASSERT_TRUE(head_index.has_value());
  EXPECT_EQ(model.value().bones[head_index.value()].pivot, (arv::Vec3f{0.0F, 8.0F, -4.0F}));
}

TEST(EntityModelTests, LoadsEmptyBoneRootBoxes) {
  const std::string text = R"json({
    "textures": ["pig.png"],
    "texture_sizes": [[64, 64]],
    "boxes": [{
      "bone": "",
      "pivot": [0, 0, 0],
      "min": [0, 0, 0],
      "max": [16, 16, 16],
      "inflate": 0,
      "north": {"texture": -1}
    }]
  })json";
  auto parsed = arv::render::parse_entity_model_json(text);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed.value().boxes.size(), 1U);
  EXPECT_EQ(parsed.value().boxes.front().bone, "");

  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{9},
      parsed.value(),
      std::span<const arv::render::EntityModelTextureBinding>{});
  ASSERT_TRUE(model) << model.error().message;
  ASSERT_EQ(model.value().bones.size(), 1U);
  EXPECT_EQ(model.value().bones.front().name, "");
  EXPECT_TRUE(model.value().vertices.empty());
  EXPECT_TRUE(model.value().indices.empty());
}

TEST(EntityModelTests, RejectsOldEntityModelFields) {
  for (const std::string& field : {"skeleton", "bones", "animations"}) {
    const std::string text = R"json({
      "textures": ["pig.png"],
      "texture_sizes": [[64, 64]],
      "boxes": []
    })json";
    std::string with_old_field = text;
    with_old_field.insert(with_old_field.rfind('}'), ",\"" + field + "\":[]");
    auto parsed = arv::render::parse_entity_model_json(with_old_field);
    ASSERT_FALSE(parsed) << field;
    EXPECT_NE(parsed.error().message.find(field), std::string::npos);
  }
}

TEST(EntityModelTests, LoadsAnimationAssetFromAnimationFolder) {
  const std::filesystem::path pig_animation_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "animation" / "pig.json";
  auto parsed = arv::render::parse_entity_animation_json(
      read_text(pig_animation_path),
      pig_animation_path.string());

  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed.value().clips.size(), 1U);
  EXPECT_EQ(parsed.value().clips.front().name, "walk");
  EXPECT_TRUE(parsed.value().clips.front().loop);
  EXPECT_EQ(parsed.value().clips.front().keyframes.size(), 16U);

  auto asset = arv::render::build_entity_animation_asset(
      arv::render::EntityAnimationID{2},
      "arvane:animation/pig",
      std::move(parsed.value()));
  ASSERT_TRUE(asset) << asset.error().message;
  EXPECT_EQ(asset.value().id, (arv::render::EntityAnimationID{2}));
}

TEST(EntityModelTests, RejectsUnsupportedAnimationChannels) {
  auto parsed = arv::render::parse_entity_animation_json(R"json({
    "animations": [{
      "name": "bad",
      "length": 1,
      "loop": false,
      "keyframes": [{
        "time": 0,
        "bone": "body",
        "channel": "opacity",
        "value": [0, 0, 0]
      }]
    }]
  })json", "arvane_home/assets/animation/bad.json");

  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("arvane_home/assets/animation/bad.json"), std::string::npos);
  EXPECT_NE(parsed.error().message.find("unsupported animation channel"), std::string::npos);
}

TEST(EntityModelTests, AcceptsBlockbenchLoopStrings) {
  auto parsed = arv::render::parse_entity_animation_json(R"json({
    "animations": [
      {
        "name": "once_clip",
        "length": 1,
        "loop": "once",
        "keyframes": [{
          "time": 0,
          "bone": "body",
          "channel": "rotation",
          "value": [0, 0, 0]
        }]
      },
      {
        "name": "hold_clip",
        "length": 1,
        "loop": "hold",
        "keyframes": [{
          "time": 0,
          "bone": "body",
          "channel": "rotation",
          "value": [0, 0, 0]
        }]
      }
    ]
  })json", "arvane_home/assets/animation/blockbench.json");

  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed.value().clips.size(), 2U);
  EXPECT_FALSE(parsed.value().clips[0].loop);
  EXPECT_FALSE(parsed.value().clips[1].loop);
}

TEST(EntityModelTests, SamplesSeparatedAnimationByBoxBoneName) {
  const std::filesystem::path pig_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "pig.json";
  const std::filesystem::path pig_animation_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "animation" / "pig.json";

  auto parsed_model = arv::render::parse_entity_model_json(read_text(pig_path));
  ASSERT_TRUE(parsed_model) << parsed_model.error().message;
  const std::vector<arv::render::EntityModelTextureBinding> bindings{{"arvane:texture/entity/pig",
                                                                      {0, 0},
                                                                      {64, 64},
                                                                      64,
                                                                      64}};
  auto model = arv::render::build_entity_model_asset(
      arv::render::EntityModelID{1},
      parsed_model.value(),
      bindings);
  ASSERT_TRUE(model) << model.error().message;

  auto parsed_animation = arv::render::parse_entity_animation_json(
      read_text(pig_animation_path),
      pig_animation_path.string());
  ASSERT_TRUE(parsed_animation) << parsed_animation.error().message;
  auto animation = arv::render::build_entity_animation_asset(
      arv::render::EntityAnimationID{1},
      "arvane:animation/pig",
      std::move(parsed_animation.value()));
  ASSERT_TRUE(animation) << animation.error().message;
  EXPECT_FALSE(arv::render::validate_entity_animation_bindings(
                   model.value(),
                   animation.value())
                   .has_value());

  std::vector<arv::Vec3f> rotations(model.value().bones.size());
  arv::render::sample_entity_model_animation(
      model.value(),
      &animation.value(),
      "walk",
      0.5F,
      std::span<arv::Vec3f>{rotations});

  const std::optional<arv::usize> front_left = find_bone_index(model.value(), "front_left_leg");
  const std::optional<arv::usize> front_right = find_bone_index(model.value(), "front_right_leg");
  ASSERT_TRUE(front_left.has_value());
  ASSERT_TRUE(front_right.has_value());
  EXPECT_EQ(rotations[front_left.value()], (arv::Vec3f{45.0F, 0.0F, 0.0F}));
  EXPECT_EQ(rotations[front_right.value()], (arv::Vec3f{-45.0F, 0.0F, 0.0F}));

  arv::render::sample_entity_model_animation(
      model.value(),
      nullptr,
      "walk",
      0.5F,
      std::span<arv::Vec3f>{rotations});
  EXPECT_EQ(rotations[front_left.value()], (arv::Vec3f{}));
}

TEST(EntityModelTests, PlayerModelPreservesInputTextureAndSkinLayerFlags) {
  const std::filesystem::path player_path =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} / "models" / "entity" / "player.json";
  auto parsed = arv::render::parse_entity_model_json(read_text(player_path));
  ASSERT_TRUE(parsed) << parsed.error().message;
  parsed.value().name = "player";

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
  const std::optional<arv::usize> left_arm = find_bone_index(model.value(), "left_arm");
  const std::optional<arv::usize> right_arm = find_bone_index(model.value(), "right_arm");
  ASSERT_TRUE(left_arm.has_value());
  ASSERT_TRUE(right_arm.has_value());
  EXPECT_EQ(model.value().bones[left_arm.value()].pivot, (arv::Vec3f{-6.0F, 24.0F, 1.0F}));
  EXPECT_EQ(model.value().bones[right_arm.value()].pivot, (arv::Vec3f{6.0F, 24.0F, 1.0F}));

  bool saw_decor = false;
  bool saw_base = false;
  for (const arv::render::EntityModelMeshVertex& vertex : model.value().vertices) {
    saw_base = saw_base ||
               vertex.flags == arv::render::EntityModelMeshVertexFlagSkinBaseLayer;
    saw_decor = saw_decor ||
                vertex.flags == arv::render::EntityModelMeshVertexFlagSkinDecorLayer;
  }
  EXPECT_TRUE(saw_base);
  EXPECT_TRUE(saw_decor);
}
