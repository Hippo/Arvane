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

TEST(LocalPlayerControllerTests, VehicleActionInputsFlowIntoCommands) {
  arv::client::LocalPlayerController controller;

  controller.submit_input(arv::client::InputFrame{
      .mount_pressed = true,
      .vehicle_roll_left_held = true});
  const arv::player::PlayerCommand first = controller.consume_command(20);
  EXPECT_TRUE(first.mount_pressed);
  EXPECT_TRUE(first.vehicle_roll_left_held);
  EXPECT_FALSE(first.vehicle_roll_right_held);

  controller.submit_input(arv::client::InputFrame{
      .vehicle_roll_right_held = true});
  const arv::player::PlayerCommand second = controller.consume_command(21);
  EXPECT_FALSE(second.mount_pressed);
  EXPECT_FALSE(second.vehicle_roll_left_held);
  EXPECT_TRUE(second.vehicle_roll_right_held);
}

TEST(LocalPlayerControllerTests, RideStartClearsPendingMountPress) {
  arv::client::LocalPlayerController controller;
  controller.enter_world(
      arv::WorldID{1},
      arv::PlayerID{2},
      arv::EntityNetID{3},
      {0.0F, 0.0F, 0.0F});

  controller.submit_input(arv::client::InputFrame{.mount_pressed = true});
  arv::protocol::ClientboundRideStarted event;
  event.passenger_id = arv::EntityNetID{3};
  event.rideable_id = arv::EntityNetID{10};
  event.seat_id = "driver";
  event.controls_vehicle = true;
  controller.apply_ride_started(event);

  const arv::player::PlayerCommand command = controller.consume_command(22);
  EXPECT_FALSE(command.mount_pressed);
  EXPECT_EQ(controller.mounted_vehicle_id(), std::optional<arv::EntityNetID>{arv::EntityNetID{10}});
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

TEST(LocalPlayerControllerTests, MountedVehicleTransformMovesLocalAnchorToSeat) {
  arv::client::LocalPlayerController controller;
  controller.enter_world(
      arv::WorldID{1},
      arv::PlayerID{2},
      arv::EntityNetID{3},
      {0.0F, 0.0F, 0.0F});

  arv::protocol::ClientboundRideStarted event;
  event.passenger_id = arv::EntityNetID{3};
  event.rideable_id = arv::EntityNetID{10};
  event.seat_id = "driver";
  event.controls_vehicle = true;
  controller.apply_ride_started(event);

  controller.apply_mounted_vehicle_transform(
      arv::EntityNetID{10},
      {10.0F, 2.0F, -3.0F},
      arv::yaw_rotation(arv::PiRadians),
      {1.0F, 0.75F, 0.0F},
      {},
      {4.0F, 0.0F, 0.0F});

  EXPECT_NEAR(controller.player_position().x, 9.0F, 0.0001F);
  EXPECT_NEAR(controller.player_position().y, 2.75F, 0.0001F);
  EXPECT_NEAR(controller.player_position().z, -3.0F, 0.0001F);
  EXPECT_EQ(controller.player_velocity(), (arv::Vec3f{4.0F, 0.0F, 0.0F}));
}

TEST(LocalPlayerControllerTests, RideableDespawnClearsMountedState) {
  arv::client::LocalPlayerController controller;
  controller.enter_world(
      arv::WorldID{1},
      arv::PlayerID{2},
      arv::EntityNetID{3},
      {0.0F, 0.0F, 0.0F});

  arv::protocol::ClientboundRideStarted event;
  event.passenger_id = arv::EntityNetID{3};
  event.rideable_id = arv::EntityNetID{10};
  event.seat_id = "driver";
  event.controls_vehicle = true;
  controller.apply_ride_started(event);
  ASSERT_EQ(controller.mounted_vehicle_id(), std::optional<arv::EntityNetID>{arv::EntityNetID{10}});

  controller.clear_mount_if_rideable_despawned(arv::EntityNetID{10});

  EXPECT_FALSE(controller.mounted_vehicle_id().has_value());
  EXPECT_TRUE(controller.mounted_seat_id().empty());
  EXPECT_EQ(controller.player_velocity(), (arv::Vec3f{}));
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

TEST(LocalPlayerControllerTests, DoubleTapJumpRequestsFlyToggleWhenAllowed) {
  arv::client::LocalPlayerController controller;
  arv::protocol::ClientboundPlayerPermissions permissions;
  permissions.game_mode_id = "creative";
  permissions.core = arv::player::survival_core_permissions();
  permissions.core.can_fly = true;
  controller.apply_player_permissions(permissions);

  controller.submit_input(arv::client::InputFrame{.jump_pressed = true, .jump_held = true});
  const arv::player::PlayerCommand first_jump = controller.consume_command(20);
  EXPECT_TRUE(first_jump.jump_pressed);
  EXPECT_FALSE(first_jump.toggle_fly_pressed);

  controller.submit_input(arv::client::InputFrame{.jump_pressed = true, .jump_held = true});
  const arv::player::PlayerCommand second_jump = controller.consume_command(26);
  EXPECT_TRUE(second_jump.jump_pressed);
  EXPECT_TRUE(second_jump.toggle_fly_pressed);
}

TEST(LocalPlayerControllerTests, DoubleTapJumpDoesNotRequestFlyWithoutPermission) {
  arv::client::LocalPlayerController controller;

  controller.submit_input(arv::client::InputFrame{.jump_pressed = true});
  EXPECT_FALSE(controller.consume_command(20).toggle_fly_pressed);
  controller.submit_input(arv::client::InputFrame{.jump_pressed = true});
  EXPECT_FALSE(controller.consume_command(21).toggle_fly_pressed);
}

TEST(LocalPlayerControllerTests, DoubleTapJumpEnablesLocalFlyingPrediction) {
  arv::client::LocalPlayerController controller;
  controller.enter_world(
      arv::WorldID{1},
      arv::PlayerID{2},
      arv::EntityNetID{3},
      {0.0F, 10.0F, 0.0F});
  arv::protocol::ClientboundPlayerPermissions permissions;
  permissions.game_mode_id = "creative";
  permissions.core = arv::player::survival_core_permissions();
  permissions.core.can_fly = true;
  controller.apply_player_permissions(permissions);

  controller.submit_input(arv::client::InputFrame{.jump_pressed = true});
  arv::player::PlayerCommand command = controller.consume_command(20);
  controller.update_local_movement(command, nullptr, arv::tick_duration_seconds(arv::DefaultBaseTps));
  controller.submit_input(arv::client::InputFrame{.jump_pressed = true});
  command = controller.consume_command(26);
  ASSERT_TRUE(command.toggle_fly_pressed);
  controller.update_local_movement(command, nullptr, arv::tick_duration_seconds(arv::DefaultBaseTps));

  const arv::protocol::ServerboundPlayerMove move =
      controller.build_movement_packet(
          1,
          26,
          arv::protocol::PlayerActionFlags::ToggleFlyPressed);
  EXPECT_TRUE(arv::protocol::has_move_flag(
      move.flags,
      arv::protocol::PlayerMoveFlags::Flying));
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
