#include "arv/client/camera_shell.hpp"

#include <gtest/gtest.h>

TEST(ClientCameraTests, SnapshotCopiesOwnedCameraState) {
  arv::client::CameraShell camera;
  camera.set_world_id(arv::WorldID{11});
  camera.set_position({1.0F, 2.0F, 3.0F});
  camera.set_rotation(0.25F, 0.5F);

  const arv::render::CameraSnapshot snapshot = camera.snapshot(800, 600, 0.25F);

  EXPECT_EQ(snapshot.world_id, arv::WorldID{11});
  EXPECT_EQ(snapshot.position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
  EXPECT_FLOAT_EQ(snapshot.fov_y_degrees, arv::client::DefaultCameraFovYDegrees);
  EXPECT_EQ(snapshot.viewport_width, 800U);
  EXPECT_EQ(snapshot.viewport_height, 600U);
  EXPECT_FLOAT_EQ(snapshot.partial_tick_alpha, 0.25F);
}

TEST(ClientCameraTests, SnapshotUsesConfiguredFov) {
  arv::client::CameraShell camera;
  camera.set_fov_y_degrees(82.0F);

  const arv::render::CameraSnapshot snapshot = camera.snapshot(800, 600, 0.0F);

  EXPECT_FLOAT_EQ(camera.fov_y_degrees(), 82.0F);
  EXPECT_FLOAT_EQ(snapshot.fov_y_degrees, 82.0F);
}

TEST(ClientCameraTests, PitchIsClamped) {
  arv::client::CameraShell camera;
  camera.set_rotation(0.0F, 100.0F);

  EXPECT_FLOAT_EQ(camera.pitch_radians(), arv::client::MaxCameraPitchRadians);
}

TEST(ClientCameraTests, FreecamMovesRelativeToFacingDirection) {
  arv::client::CameraShell camera;
  camera.set_position({0.0F, 0.0F, 0.0F});
  camera.set_rotation(0.0F, 0.0F);

  camera.apply_freecam_input({
      true,
      false,
      false,
      false,
      false,
      false,
      false,
      0.0F,
      0.0F,
      0.5F});

  EXPECT_FLOAT_EQ(camera.position().x, 0.0F);
  EXPECT_FLOAT_EQ(camera.position().y, 0.0F);
  EXPECT_FLOAT_EQ(camera.position().z, arv::client::FreecamMoveSpeedBlocksPerSecond * 0.5F);
}

TEST(ClientCameraTests, FreecamMouseLookUpdatesCameraBasis) {
  arv::client::CameraShell camera;
  camera.set_rotation(0.0F, 0.0F);

  camera.apply_freecam_input({
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      100.0F,
      -50.0F,
      0.0F});

  EXPECT_NEAR(camera.yaw_radians(), 100.0F * arv::client::FreecamMouseSensitivityRadians, 0.0001F);
  EXPECT_NEAR(camera.pitch_radians(), 50.0F * arv::client::FreecamMouseSensitivityRadians, 0.0001F);
  EXPECT_GT(camera.forward_vector().x, 0.0F);
  EXPECT_GT(camera.forward_vector().y, 0.0F);
}
