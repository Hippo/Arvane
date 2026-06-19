#include "arv/client/item_rendering.hpp"

#include <gtest/gtest.h>

namespace {

[[nodiscard]] arv::render::CameraSnapshot make_forward_camera() {
  arv::render::CameraSnapshot camera;
  camera.position = {1.0F, 2.0F, 3.0F};
  camera.forward = {0.0F, 0.0F, 1.0F};
  camera.right = {1.0F, 0.0F, 0.0F};
  camera.up = {0.0F, 1.0F, 0.0F};
  return camera;
}

void expect_vec3_near(arv::Vec3f actual, arv::Vec3f expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.0001F);
  EXPECT_NEAR(actual.y, expected.y, 0.0001F);
  EXPECT_NEAR(actual.z, expected.z, 0.0001F);
}

} // namespace

TEST(ItemRenderingTests, FirstPersonHeldItemPoseUsesRightHandAnchor) {
  const arv::render::CameraSnapshot camera = make_forward_camera();
  const arv::client::FirstPersonHeldItemPose pose =
      arv::client::first_person_held_item_pose(camera);

  EXPECT_EQ(pose.hand_rotation_override.bone_name, "right_arm");
  EXPECT_TRUE(pose.hand_rotation_override.replace);
  expect_vec3_near(
      arv::client::first_person_player_body_position(camera),
      {1.0F, 0.2F, 2.58F});
  EXPECT_EQ(
      arv::client::first_person_player_body_rotation(camera),
      arv::model_forward_yaw_rotation(0.0F));
  EXPECT_GT(pose.item_position.x, camera.position.x);
  EXPECT_LT(pose.item_position.y, camera.position.y);
  EXPECT_GT(pose.item_position.z, camera.position.z);

  arv::render::EntityDrawItem player_draw;
  arv::client::HeldItemAnimationPose animation_pose;
  animation_pose.rotation_degrees = {-20.0F, 4.0F, -40.0F};
  arv::client::apply_first_person_held_item_arm_pose(player_draw, animation_pose);
  ASSERT_EQ(player_draw.bone_rotation_overrides.size(), 1U);
  EXPECT_EQ(player_draw.bone_rotation_overrides.front().bone_name, "right_arm");
  EXPECT_TRUE(player_draw.bone_rotation_overrides.front().replace);
  EXPECT_GT(player_draw.bone_rotation_overrides.front().rotation_degrees.x, 72.0F);
  EXPECT_LT(player_draw.bone_rotation_overrides.front().rotation_degrees.z, -10.0F);
}

TEST(ItemRenderingTests, AppendHeldItemDrawUsesFirstPersonHandPose) {
  arv::client::EntityRenderAssets assets;
  assets.model_by_item.emplace(
      arv::player::ItemID{1U},
      arv::render::EntityModelID{77U});

  arv::render::FramePacket frame;
  const arv::render::CameraSnapshot camera = make_forward_camera();
  arv::client::HeldItemAnimationPose animation_pose;
  animation_pose.position = {0.05F, -0.03F, 0.12F};
  animation_pose.rotation_degrees = {-12.0F, -6.0F, -45.0F};

  arv::client::append_held_item_draw(
      frame,
      assets,
      arv::player::ItemStack{arv::player::ItemID{1U}, 1U},
      camera,
      animation_pose);

  ASSERT_EQ(frame.entities.size(), 1U);
  const arv::client::FirstPersonHeldItemPose pose =
      arv::client::first_person_held_item_pose(camera, animation_pose);
  EXPECT_EQ(frame.entities.front().model_id, (arv::render::EntityModelID{77U}));
  expect_vec3_near(frame.entities.front().position, pose.item_position);
  EXPECT_FLOAT_EQ(frame.entities.front().scale, pose.item_scale);
}
