#include "arv/client/item_rendering.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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

TEST(ItemRenderingTests, MakeReplicatedEntityDrawItemCopiesWorldMirrorState) {
  arv::client::EntityRenderAssets assets;
  assets.animation_by_type.emplace(
      arv::EntityTypeID{2U},
      arv::render::EntityAnimationID{44U});

  const std::vector<arv::protocol::EntityAnimationPlayback> animations{
      {"walk", 1.25F},
      {"blink", 0.5F}};
  arv::client::ReplicatedEntityDrawInput input;
  input.entity_type = arv::EntityTypeID{2U};
  input.render_position = {3.0F, 4.0F, 5.0F};
  input.render_rotation = arv::yaw_rotation(0.75F);
  input.animations = animations;
  input.head_rotation_degrees = {5.0F, 12.0F, 0.0F};
  input.has_owning_player = true;
  input.model_id = arv::render::EntityModelID{22U};
  input.player_skin_region = arv::render::TextureRegionHandle{9U, 3U};

  const arv::render::EntityDrawItem draw =
      arv::client::make_replicated_entity_draw_item(assets, input);

  EXPECT_EQ(draw.model_id, (arv::render::EntityModelID{22U}));
  EXPECT_EQ(draw.animation_id, (arv::render::EntityAnimationID{44U}));
  expect_vec3_near(draw.position, {3.0F, 4.0F, 5.0F});
  EXPECT_EQ(draw.rotation, arv::yaw_rotation(0.75F));
  EXPECT_FLOAT_EQ(draw.scale, 1.0F);
  EXPECT_EQ(draw.player_skin_region, (arv::render::TextureRegionHandle{9U, 3U}));
  ASSERT_EQ(draw.animations.size(), 2U);
  EXPECT_EQ(draw.animations[0].name, "walk");
  EXPECT_FLOAT_EQ(draw.animations[0].time_seconds, 1.25F);
  EXPECT_EQ(draw.animations[1].name, "blink");
  EXPECT_FLOAT_EQ(draw.animations[1].time_seconds, 0.5F);
  ASSERT_EQ(draw.bone_rotation_overrides.size(), 1U);
  EXPECT_EQ(draw.bone_rotation_overrides.front().bone_name, "head");
  expect_vec3_near(draw.bone_rotation_overrides.front().rotation_degrees,
                   {5.0F, 12.0F, 0.0F});
}

TEST(ItemRenderingTests, MakeReplicatedEntityDrawItemUsesFirstPersonBodyAndArmPose) {
  arv::client::EntityRenderAssets assets;
  const arv::render::CameraSnapshot camera = make_forward_camera();
  arv::client::HeldItemAnimationPose held_pose;
  held_pose.rotation_degrees = {-18.0F, 4.0F, -32.0F};

  arv::client::ReplicatedEntityDrawInput input;
  input.model_id = arv::render::EntityModelID{33U};
  input.render_position = {9.0F, 9.0F, 9.0F};
  input.render_rotation = arv::yaw_rotation(1.0F);
  input.first_person = true;
  input.first_person_camera = camera;
  input.first_person_sneak_progress = 0.5F;
  input.apply_held_item_arm_pose = true;
  input.held_item_pose = held_pose;

  const arv::render::EntityDrawItem draw =
      arv::client::make_replicated_entity_draw_item(assets, input);

  EXPECT_EQ(draw.model_id, (arv::render::EntityModelID{33U}));
  expect_vec3_near(
      draw.position,
      arv::client::first_person_player_body_position(camera, 0.5F));
  EXPECT_EQ(draw.rotation, arv::client::first_person_player_body_rotation(camera));
  ASSERT_EQ(draw.bone_rotation_overrides.size(), 1U);
  EXPECT_EQ(draw.bone_rotation_overrides.front().bone_name, "right_arm");
  EXPECT_TRUE(draw.bone_rotation_overrides.front().replace);
}

TEST(ItemRenderingTests, SelectPlayerEntityModelUsesSlimSkinWhenReady) {
  arv::client::EntityRenderAssets assets;
  assets.player_slim_model = arv::render::EntityModelID{2U};
  assets.player_skin_model = arv::render::EntityModelID{3U};
  assets.player_slim_skin_model = arv::render::EntityModelID{4U};

  const arv::client::PlayerEntityModelSelection selection =
      arv::client::select_player_entity_model(
          assets,
          {.base_model_id = arv::render::EntityModelID{1U},
           .slim_model = true,
           .ready_player_skin_region =
               arv::render::TextureRegionHandle{9U, 3U}});

  EXPECT_EQ(selection.model_id, (arv::render::EntityModelID{4U}));
  EXPECT_EQ(
      selection.player_skin_region,
      (arv::render::TextureRegionHandle{9U, 3U}));
}

TEST(ItemRenderingTests, SelectPlayerEntityModelPrefersFirstPersonSkin) {
  arv::client::EntityRenderAssets assets;
  assets.player_skin_model = arv::render::EntityModelID{3U};
  assets.first_person_player_model = arv::render::EntityModelID{5U};
  assets.first_person_player_skin_model = arv::render::EntityModelID{6U};

  const arv::client::PlayerEntityModelSelection selection =
      arv::client::select_player_entity_model(
          assets,
          {.base_model_id = arv::render::EntityModelID{1U},
           .first_person = true,
           .ready_player_skin_region =
               arv::render::TextureRegionHandle{9U, 3U}});

  EXPECT_EQ(selection.model_id, (arv::render::EntityModelID{6U}));
  EXPECT_EQ(
      selection.player_skin_region,
      (arv::render::TextureRegionHandle{9U, 3U}));
}

TEST(ItemRenderingTests, SelectPlayerEntityModelFallsBackToFirstPersonSlim) {
  arv::client::EntityRenderAssets assets;
  assets.player_slim_model = arv::render::EntityModelID{2U};
  assets.first_person_player_model = arv::render::EntityModelID{5U};
  assets.first_person_player_slim_model = arv::render::EntityModelID{7U};

  const arv::client::PlayerEntityModelSelection selection =
      arv::client::select_player_entity_model(
          assets,
          {.base_model_id = arv::render::EntityModelID{1U},
           .slim_model = true,
           .first_person = true});

  EXPECT_EQ(selection.model_id, (arv::render::EntityModelID{7U}));
  EXPECT_FALSE(selection.player_skin_region.valid());
}

TEST(ItemRenderingTests, AppendReplicatedEntityDrawsUsesProfileSkinAndLocalPose) {
  arv::client::EntityRenderAssets assets;
  assets.model_by_type.emplace(
      arv::EntityTypeID{4U},
      arv::render::EntityModelID{10U});
  assets.animation_by_type.emplace(
      arv::EntityTypeID{4U},
      arv::render::EntityAnimationID{11U});
  assets.player_skin_model = arv::render::EntityModelID{20U};
  assets.first_person_player_skin_model = arv::render::EntityModelID{30U};

  std::vector<arv::client::ReplicatedEntityMirror> mirrors(1U);
  mirrors.front().entity_id = arv::EntityNetID{7U};
  mirrors.front().entity_type = arv::EntityTypeID{4U};
  mirrors.front().world_id = arv::WorldID{5U};
  mirrors.front().render_position = {9.0F, 8.0F, 7.0F};
  mirrors.front().render_rotation = arv::yaw_rotation(1.0F);
  mirrors.front().owning_player = arv::PlayerID{3U};
  mirrors.front().animations.push_back({"sneak_idle", 2.0F});
  mirrors.front().head_rotation_degrees = {2.0F, 3.0F, 4.0F};

  bool profile_queried = false;
  const auto player_skin_info =
      [&](arv::PlayerID player_id)
          -> std::optional<arv::client::PlayerEntitySkinInfo> {
    EXPECT_EQ(player_id, (arv::PlayerID{3U}));
    profile_queried = true;
    return arv::client::PlayerEntitySkinInfo{
        .slim_model = false,
        .skin_hash = "skin-hash"};
  };
  bool skin_queried = false;
  const auto ready_player_skin =
      [&](std::string_view skin_hash)
          -> std::optional<arv::render::TextureRegionHandle> {
    EXPECT_EQ(skin_hash, "skin-hash");
    skin_queried = true;
    return arv::render::TextureRegionHandle{6U, 2U};
  };

  arv::render::FramePacket frame;
  arv::client::FirstPersonSneakVisualState sneak;
  arv::client::HeldItemAnimationPose held_pose;
  held_pose.rotation_degrees = {-12.0F, 5.0F, -20.0F};

  const arv::client::ReplicatedEntityFrameResult result =
      arv::client::append_replicated_entity_draws(
          frame,
          assets,
          {.mirrors =
               std::span<const arv::client::ReplicatedEntityMirror>{mirrors},
           .world_id = arv::WorldID{5U},
           .local_entity_id = arv::EntityNetID{7U},
           .local_player_id = arv::PlayerID{3U},
           .first_person_camera = make_forward_camera(),
           .show_local_player_body = true,
           .apply_local_held_item_arm_pose = true,
           .held_item_pose = held_pose,
           .first_person_sneak_visual = &sneak,
           .animation_time_seconds = 4.0F,
           .callbacks =
               {.player_skin_info =
                    arv::FunctionRef<std::optional<
                        arv::client::PlayerEntitySkinInfo>(arv::PlayerID)>{
                        player_skin_info},
                .ready_player_skin =
                    arv::FunctionRef<std::optional<
                        arv::render::TextureRegionHandle>(std::string_view)>{
                        ready_player_skin}}});

  EXPECT_TRUE(profile_queried);
  EXPECT_TRUE(skin_queried);
  EXPECT_EQ(result.appended, 1U);
  EXPECT_FLOAT_EQ(result.first_person_sneak_progress, 1.0F);
  ASSERT_EQ(frame.entities.size(), 1U);
  const arv::render::EntityDrawItem& draw = frame.entities.front();
  EXPECT_EQ(draw.model_id, (arv::render::EntityModelID{30U}));
  EXPECT_EQ(draw.animation_id, (arv::render::EntityAnimationID{11U}));
  EXPECT_EQ(draw.player_skin_region, (arv::render::TextureRegionHandle{6U, 2U}));
  expect_vec3_near(
      draw.position,
      arv::client::first_person_player_body_position(make_forward_camera(), 1.0F));
  EXPECT_EQ(draw.rotation, arv::client::first_person_player_body_rotation(
                               make_forward_camera()));
  ASSERT_EQ(draw.bone_rotation_overrides.size(), 2U);
  EXPECT_EQ(draw.bone_rotation_overrides[0].bone_name, "head");
  EXPECT_EQ(draw.bone_rotation_overrides[1].bone_name, "right_arm");
}

TEST(ItemRenderingTests, AppendReplicatedEntityDrawsUpdatesSneakWhenLocalBodyHidden) {
  arv::client::EntityRenderAssets assets;
  assets.model_by_type.emplace(
      arv::EntityTypeID{4U},
      arv::render::EntityModelID{10U});

  std::vector<arv::client::ReplicatedEntityMirror> mirrors(1U);
  mirrors.front().entity_id = arv::EntityNetID{7U};
  mirrors.front().entity_type = arv::EntityTypeID{4U};
  mirrors.front().world_id = arv::WorldID{5U};
  mirrors.front().animations.push_back({"sneak_idle", 2.0F});

  arv::render::FramePacket frame;
  arv::client::FirstPersonSneakVisualState sneak;
  const arv::client::ReplicatedEntityFrameResult result =
      arv::client::append_replicated_entity_draws(
          frame,
          assets,
          {.mirrors =
               std::span<const arv::client::ReplicatedEntityMirror>{mirrors},
           .world_id = arv::WorldID{5U},
           .local_entity_id = arv::EntityNetID{7U},
           .first_person_camera = make_forward_camera(),
           .show_local_player_body = false,
           .first_person_sneak_visual = &sneak,
           .animation_time_seconds = 4.0F});

  EXPECT_EQ(result.appended, 0U);
  EXPECT_FLOAT_EQ(result.first_person_sneak_progress, 1.0F);
  EXPECT_TRUE(frame.entities.empty());
  EXPECT_TRUE(sneak.initialized);
  EXPECT_FLOAT_EQ(sneak.progress, 1.0F);
}

TEST(ItemRenderingTests, FirstPersonSneakTargetReadsTransitionAndIdleClips) {
  const std::vector<arv::protocol::EntityAnimationPlayback> transition{
      {"sneak", 0.125F}};
  EXPECT_FLOAT_EQ(
      arv::client::first_person_sneak_target_progress(transition),
      0.5F);

  const std::vector<arv::protocol::EntityAnimationPlayback> idle{
      {"sneak_idle", 3.0F}};
  EXPECT_FLOAT_EQ(
      arv::client::first_person_sneak_target_progress(idle),
      1.0F);

  const std::vector<arv::protocol::EntityAnimationPlayback> none{{"walk", 1.0F}};
  EXPECT_FLOAT_EQ(
      arv::client::first_person_sneak_target_progress(none),
      0.0F);
}

TEST(ItemRenderingTests, FirstPersonSneakVisualProgressSmoothsTowardTarget) {
  arv::client::FirstPersonSneakVisualState state;
  const std::vector<arv::protocol::EntityAnimationPlayback> idle{
      {"sneak_idle", 1.0F}};
  const std::vector<arv::protocol::EntityAnimationPlayback> none{};

  EXPECT_FLOAT_EQ(
      arv::client::update_first_person_sneak_visual_progress(state, idle, 10.0F),
      1.0F);
  EXPECT_TRUE(state.initialized);
  EXPECT_FLOAT_EQ(state.progress, 1.0F);

  EXPECT_NEAR(
      arv::client::update_first_person_sneak_visual_progress(state, none, 10.1F),
      0.5F,
      0.0001F);
  EXPECT_NEAR(
      arv::client::update_first_person_sneak_visual_progress(state, none, 10.2F),
      0.0F,
      0.0001F);
}

TEST(ItemRenderingTests, AppendBlockAttachedModelDrawUsesVariantModelTransformAndAnimation) {
  arv::client::EntityRenderAssets assets;
  assets.block_attached_model_by_variant.resize(3U);
  assets.block_attached_model_by_variant[2U] =
      arv::render::EntityModelID{123U};
  assets.block_attached_animation_by_variant.resize(3U);
  assets.block_attached_animation_by_variant[2U] =
      arv::render::EntityAnimationID{456U};
  assets.block_attached_rotation_by_variant.resize(3U);
  assets.block_attached_rotation_by_variant[2U] = arv::yaw_rotation(0.75F);

  arv::client::BlockContainerAnimationState animation;
  const arv::protocol::ContainerBlockSource source{
      arv::WorldID{9},
      arv::BlockPos{{4, 5, 6}}};
  arv::client::update_block_container_animation(animation, source, 2.0F);

  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{9};
  arv::client::append_block_attached_model_draw(
      frame,
      assets,
      arv::voxel::BlockVariantID{2U},
      arv::BlockPos{{4, 5, 6}},
      &animation,
      2.25F);

  ASSERT_EQ(frame.entities.size(), 1U);
  const arv::render::EntityDrawItem& draw = frame.entities.front();
  EXPECT_EQ(draw.model_id, (arv::render::EntityModelID{123U}));
  EXPECT_EQ(draw.animation_id, (arv::render::EntityAnimationID{456U}));
  expect_vec3_near(draw.position, {4.5F, 5.0F, 6.5F});
  EXPECT_EQ(draw.rotation, arv::yaw_rotation(0.75F));
  EXPECT_FLOAT_EQ(draw.scale, 1.0F);
  ASSERT_EQ(draw.animations.size(), 1U);
  EXPECT_EQ(draw.animations.front().name, arv::client::BlockContainerOpenAnimation);
  EXPECT_FLOAT_EQ(draw.animations.front().time_seconds, 0.25F);
}

TEST(ItemRenderingTests, AppendBlockAttachedModelDrawSkipsPlaybackForMismatchedSource) {
  arv::client::EntityRenderAssets assets;
  assets.block_attached_model_by_variant.resize(3U);
  assets.block_attached_model_by_variant[2U] =
      arv::render::EntityModelID{123U};
  assets.block_attached_animation_by_variant.resize(3U);
  assets.block_attached_animation_by_variant[2U] =
      arv::render::EntityAnimationID{456U};

  arv::client::BlockContainerAnimationState animation;
  const arv::protocol::ContainerBlockSource source{
      arv::WorldID{9},
      arv::BlockPos{{4, 5, 6}}};
  arv::client::update_block_container_animation(animation, source, 2.0F);

  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{10};
  arv::client::append_block_attached_model_draw(
      frame,
      assets,
      arv::voxel::BlockVariantID{2U},
      arv::BlockPos{{4, 5, 6}},
      &animation,
      2.25F);

  ASSERT_EQ(frame.entities.size(), 1U);
  EXPECT_EQ(frame.entities.front().model_id, (arv::render::EntityModelID{123U}));
  EXPECT_TRUE(frame.entities.front().animations.empty());
}

TEST(ItemRenderingTests, AppendBlockAttachedModelDrawIgnoresMissingModels) {
  arv::client::EntityRenderAssets assets;
  assets.block_attached_model_by_variant.resize(2U);
  assets.block_attached_model_by_variant[1U] = arv::render::EntityModelID{};

  arv::render::FramePacket frame;
  arv::client::append_block_attached_model_draw(
      frame,
      assets,
      arv::voxel::BlockVariantID{1U},
      arv::BlockPos{{0, 0, 0}});
  arv::client::append_block_attached_model_draw(
      frame,
      assets,
      arv::voxel::BlockVariantID{9U},
      arv::BlockPos{{0, 0, 0}});

  EXPECT_TRUE(frame.entities.empty());
}

TEST(ItemRenderingTests, CollectBlockAttachedModelSectionEntriesUsesSharedVariantPolicy) {
  arv::client::EntityRenderAssets assets;
  assets.block_attached_model_by_variant.resize(6U);
  assets.block_attached_model_by_variant[2U] =
      arv::render::EntityModelID{22U};
  assets.block_attached_model_by_variant[4U] =
      arv::render::EntityModelID{};
  assets.block_attached_model_by_variant[5U] =
      arv::render::EntityModelID{55U};

  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks{};
  const arv::SectionLocalPos first_local{{1, 2, 3}};
  const arv::SectionLocalPos skipped_local{{2, 2, 3}};
  const arv::SectionLocalPos second_local{{3, 2, 3}};
  blocks[arv::voxel::local_index(first_local)] =
      arv::voxel::BlockVariantID{2U};
  blocks[arv::voxel::local_index(skipped_local)] =
      arv::voxel::BlockVariantID{4U};
  blocks[arv::voxel::local_index(second_local)] =
      arv::voxel::BlockVariantID{5U};

  std::vector<arv::client::BlockAttachedModelSectionEntry> entries;
  arv::client::collect_block_attached_model_section_entries(
      assets,
      blocks,
      entries);

  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].local.value.x, first_local.value.x);
  EXPECT_EQ(entries[0].local.value.y, first_local.value.y);
  EXPECT_EQ(entries[0].local.value.z, first_local.value.z);
  EXPECT_EQ(entries[0].variant_id, (arv::voxel::BlockVariantID{2U}));
  EXPECT_EQ(entries[1].local.value.x, second_local.value.x);
  EXPECT_EQ(entries[1].local.value.y, second_local.value.y);
  EXPECT_EQ(entries[1].local.value.z, second_local.value.z);
  EXPECT_EQ(entries[1].variant_id, (arv::voxel::BlockVariantID{5U}));
}

TEST(ItemRenderingTests, AppendBlockAttachedModelSectionEntriesUsesChunkLocalBlocks) {
  arv::client::EntityRenderAssets assets;
  assets.block_attached_model_by_variant.resize(3U);
  assets.block_attached_model_by_variant[2U] =
      arv::render::EntityModelID{22U};

  const std::vector<arv::client::BlockAttachedModelSectionEntry> entries{
      {arv::SectionLocalPos{{1, 2, 3}}, arv::voxel::BlockVariantID{2U}},
      {arv::SectionLocalPos{{4, 5, 6}}, arv::voxel::BlockVariantID{99U}}};
  arv::render::FramePacket frame;
  const arv::usize appended =
      arv::client::append_block_attached_model_draws_for_section_entries(
          frame,
          assets,
          arv::ChunkSectionCoord{{2, 3, 4}},
          entries);

  ASSERT_EQ(appended, 1U);
  ASSERT_EQ(frame.entities.size(), 1U);
  EXPECT_EQ(frame.entities.front().model_id, (arv::render::EntityModelID{22U}));
  expect_vec3_near(frame.entities.front().position, {33.5F, 50.0F, 67.5F});
}

TEST(ItemRenderingTests, AppendBlockOutlineDrawUsesVariantModelAndBlockCenter) {
  arv::client::EntityRenderAssets assets;
  assets.outline_model_by_variant.resize(4U);
  assets.outline_model_by_variant[2U] = arv::render::EntityModelID{55U};

  arv::render::FramePacket frame;
  arv::client::append_block_outline_draw(
      frame,
      assets,
      {.block = arv::BlockPos{{-2, 3, 9}},
       .variant = arv::voxel::BlockVariantID{2U}});

  ASSERT_EQ(frame.entities.size(), 1U);
  EXPECT_EQ(frame.entities.front().model_id, (arv::render::EntityModelID{55U}));
  expect_vec3_near(frame.entities.front().position, {-1.5F, 3.5F, 9.5F});
  EXPECT_FLOAT_EQ(frame.entities.front().scale, 1.0F);

  arv::client::append_block_outline_draw(
      frame,
      assets,
      {.block = arv::BlockPos{{0, 0, 0}},
       .variant = arv::voxel::BlockVariantID{3U}});
  arv::client::append_block_outline_draw(
      frame,
      assets,
      {.block = arv::BlockPos{{0, 0, 0}},
       .variant = arv::voxel::BlockVariantID{9U}});
  EXPECT_EQ(frame.entities.size(), 1U);
}

TEST(ItemRenderingTests, AppendBlockBreakOverlayDrawUsesStageModelWhenActive) {
  arv::client::EntityRenderAssets assets;
  assets.break_overlay_model_by_variant.resize(3U);
  assets.break_overlay_model_by_variant[1U][4U] = arv::render::EntityModelID{88U};

  arv::render::FramePacket frame;
  arv::protocol::BlockBreakProgress progress;
  progress.active = true;
  progress.block = arv::BlockPos{{6, -1, 12}};
  progress.variant = arv::voxel::BlockVariantID{1U};
  progress.stage = 4U;

  arv::client::append_block_break_overlay_draw(frame, assets, progress);

  ASSERT_EQ(frame.entities.size(), 1U);
  EXPECT_EQ(frame.entities.front().model_id, (arv::render::EntityModelID{88U}));
  expect_vec3_near(frame.entities.front().position, {6.5F, -0.5F, 12.5F});
  EXPECT_FLOAT_EQ(frame.entities.front().scale, 1.0F);

  progress.active = false;
  arv::client::append_block_break_overlay_draw(frame, assets, progress);
  progress.active = true;
  progress.stage = arv::client::BreakStageTextureCount;
  arv::client::append_block_break_overlay_draw(frame, assets, progress);
  progress.stage = 0U;
  progress.variant = arv::voxel::BlockVariantID{99U};
  arv::client::append_block_break_overlay_draw(frame, assets, progress);
  EXPECT_EQ(frame.entities.size(), 1U);
}
