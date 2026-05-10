#include "arv/client/local_player_controller.hpp"

#include <gtest/gtest.h>

TEST(LocalPlayerControllerTests, HotbarSelectionAndScrollWrapProduceStableCommands) {
  arv::client::LocalPlayerController controller;

  controller.submit_input(arv::client::InputFrame{.hotbar_select = 7});
  const arv::player::PlayerCommand selected = controller.consume_command(10);
  EXPECT_EQ(selected.client_tick, 10U);
  EXPECT_EQ(selected.selected_hotbar_slot, 7U);

  controller.submit_input(arv::client::InputFrame{.hotbar_scroll_steps = 3});
  const arv::player::PlayerCommand wrapped = controller.consume_command(11);
  EXPECT_EQ(wrapped.client_tick, 11U);
  EXPECT_EQ(wrapped.selected_hotbar_slot, 4U);
}

TEST(LocalPlayerControllerTests, MoveCorrectionSnapsLocalTransformDirectly) {
  arv::client::LocalPlayerController controller;

  controller.enter_world(arv::WorldID{1}, arv::PlayerID{2}, arv::EntityNetID{3}, {1.0F, 2.0F, 3.0F});
  arv::protocol::ClientboundPlayerMoveCorrection correction;
  correction.position = {5.0F, 6.0F, 7.0F};
  correction.velocity = {0.0F, 1.0F, 0.0F};
  correction.yaw_radians = 0.25F;
  correction.pitch_radians = -0.15F;
  correction.flags = arv::protocol::PlayerMoveFlags::OnGround;

  controller.apply_move_correction(correction);

  EXPECT_EQ(controller.player_position(), (arv::Vec3f{5.0F, 6.0F, 7.0F}));
  EXPECT_FLOAT_EQ(controller.camera().yaw_radians(), 0.25F);
  EXPECT_FLOAT_EQ(controller.camera().pitch_radians(), -0.15F);
}

TEST(LocalPlayerControllerTests, PlayerVelocityPacketAppliesKnockbackVelocity) {
  arv::client::LocalPlayerController controller;
  controller.enter_world(arv::WorldID{1}, arv::PlayerID{2}, arv::EntityNetID{3}, {0.0F, 0.0F, 0.0F});

  arv::protocol::ClientboundPlayerVelocity velocity;
  velocity.player_id = arv::PlayerID{2};
  velocity.velocity = {8.0F, 1.5F, 0.0F};
  velocity.knockback_control_suppression_ticks = 2;
  controller.apply_player_velocity(velocity);

  const arv::protocol::ServerboundPlayerMove move =
      controller.build_movement_packet(
          1,
          10,
          arv::protocol::PlayerActionFlags::None);

  EXPECT_EQ(move.velocity, (arv::Vec3f{8.0F, 1.5F, 0.0F}));
  EXPECT_FALSE(arv::protocol::has_move_flag(
      move.flags,
      arv::protocol::PlayerMoveFlags::OnGround));
}

TEST(LocalPlayerControllerTests, SendsRawSprintIntentForLocalMovementRules) {
  arv::client::LocalPlayerController controller;

  controller.submit_input(arv::client::InputFrame{
      .move_forward = true,
      .move_right = true,
      .sprint_held = true});
  const arv::player::PlayerCommand sprinting = controller.consume_command(12);
  EXPECT_TRUE(sprinting.sprint_held);

  controller.submit_input(arv::client::InputFrame{
      .move_right = true,
      .sprint_held = true});
  const arv::player::PlayerCommand strafing = controller.consume_command(13);
  EXPECT_TRUE(strafing.sprint_held);

  controller.submit_input(arv::client::InputFrame{
      .move_forward = true,
      .sneak_held = true,
      .sprint_held = true});
  const arv::player::PlayerCommand sneaking = controller.consume_command(14);
  EXPECT_TRUE(sneaking.sneak_held);
  EXPECT_TRUE(sneaking.sprint_held);
}

TEST(LocalPlayerControllerTests, SprintingAppliesCameraFovBoost) {
  arv::client::LocalPlayerController controller;

  controller.submit_input(arv::client::InputFrame{
      .move_forward = true,
      .sprint_held = true});
  EXPECT_FLOAT_EQ(
      controller.camera().fov_y_degrees(),
      arv::client::DefaultCameraFovYDegrees + arv::client::DefaultSprintCameraFovBoostDegrees);

  controller.submit_input(arv::client::InputFrame{
      .move_right = true,
      .sprint_held = true});
  EXPECT_FLOAT_EQ(controller.camera().fov_y_degrees(), arv::client::DefaultCameraFovYDegrees);

  controller.submit_input(arv::client::InputFrame{
      .move_forward = true,
      .sneak_held = true,
      .sprint_held = true});
  EXPECT_FLOAT_EQ(controller.camera().fov_y_degrees(), arv::client::DefaultCameraFovYDegrees);
}
