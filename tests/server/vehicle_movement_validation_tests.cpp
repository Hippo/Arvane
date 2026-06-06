#include "arv/server/vehicle_movement_validation.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {

arv::entity::VehicleAuthorityDefinition authority() {
  arv::entity::VehicleAuthorityDefinition result;
  result.max_speed = 10.0F;
  result.max_acceleration = 20.0F;
  result.max_deceleration = 30.0F;
  result.max_angular_speed = 2.0F;
  result.max_vertical_speed = 4.0F;
  return result;
}

arv::protocol::ServerboundVehicleMove vehicle_move(
    arv::u32 sequence,
    arv::Vec3f position) {
  arv::protocol::ServerboundVehicleMove move;
  move.vehicle_id = arv::EntityNetID{10};
  move.sequence = sequence;
  move.client_tick = sequence;
  move.dt_seconds = 0.05F;
  move.position = position;
  move.velocity = {2.0F, 0.0F, 0.0F};
  move.rotation = {};
  move.flags = arv::protocol::VehicleMoveFlags::OnGround;
  return move;
}

arv::server::AcceptedVehicleMovementState accepted_at(
    arv::u32 sequence,
    arv::Vec3f position,
    double receive_time_seconds = 1.0) {
  return {
      position,
      {2.0F, 0.0F, 0.0F},
      {},
      {},
      arv::protocol::VehicleMoveFlags::OnGround,
      sequence,
      sequence,
      receive_time_seconds,
      true};
}

arv::server::VehicleMovementValidationContext controller_context() {
  arv::server::VehicleMovementValidationContext context;
  context.receive_time_seconds = 1.05;
  context.server_tick = 20;
  context.passenger_is_mounted = true;
  context.passenger_controls_vehicle = true;
  return context;
}

} // namespace

TEST(VehicleMovementValidationTests, AcceptsFirstFiniteControllerMove) {
  const auto result = arv::server::validate_serverbound_vehicle_move(
      vehicle_move(1, {0.0F, 4.0F, 0.0F}),
      {},
      controller_context(),
      authority());

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.accepted_state.position, (arv::Vec3f{0.0F, 4.0F, 0.0F}));
}

TEST(VehicleMovementValidationTests, RejectsNaNPosition) {
  auto move = vehicle_move(1, {0.0F, 4.0F, 0.0F});
  move.position.x = std::numeric_limits<arv::f32>::quiet_NaN();

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      {},
      controller_context(),
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::InvalidNumber);
}

TEST(VehicleMovementValidationTests, RejectsStaleSequence) {
  const auto result = arv::server::validate_serverbound_vehicle_move(
      vehicle_move(2, {0.1F, 4.0F, 0.0F}),
      accepted_at(2, {0.0F, 4.0F, 0.0F}),
      controller_context(),
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::StalePacket);
}

TEST(VehicleMovementValidationTests, RejectsNonController) {
  auto context = controller_context();
  context.passenger_controls_vehicle = false;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      vehicle_move(1, {0.0F, 4.0F, 0.0F}),
      {},
      context,
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::NotController);
}

TEST(VehicleMovementValidationTests, RejectsExcessiveSpeed) {
  auto move = vehicle_move(2, {0.0F, 4.0F, 0.0F});
  move.velocity = {40.0F, 0.0F, 0.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      accepted_at(1, {0.0F, 4.0F, 0.0F}),
      controller_context(),
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::TooFast);
}

TEST(VehicleMovementValidationTests, RejectsExcessiveAcceleration) {
  auto move = vehicle_move(2, {0.25F, 4.0F, 0.0F});
  move.velocity = {9.0F, 0.0F, 0.0F};

  auto strict = authority();
  strict.max_acceleration = 5.0F;
  strict.max_deceleration = 5.0F;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      accepted_at(1, {0.0F, 4.0F, 0.0F}),
      controller_context(),
      strict);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::TooMuchAcceleration);
}

TEST(VehicleMovementValidationTests, RejectsExcessiveAngularVelocity) {
  auto move = vehicle_move(1, {0.0F, 4.0F, 0.0F});
  move.angular_velocity = {0.0F, 12.0F, 0.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      {},
      controller_context(),
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::TooMuchAngularVelocity);
}

TEST(VehicleMovementValidationTests, RejectsRotationJumpWithZeroAngularVelocity) {
  auto move = vehicle_move(2, {0.1F, 4.0F, 0.0F});
  move.angular_velocity = {};
  move.rotation = arv::yaw_rotation(arv::PiRadians);

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      accepted_at(1, {0.0F, 4.0F, 0.0F}),
      controller_context(),
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::TooMuchAngularVelocity);
}

TEST(VehicleMovementValidationTests, RejectsCollisionPath) {
  auto context = controller_context();
  context.swept_aabb_overlaps_solid = true;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      vehicle_move(1, {0.0F, 4.0F, 0.0F}),
      {},
      context,
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::Collision);
}

TEST(VehicleMovementValidationTests, RejectsMissingRequiredWaterSupport) {
  auto boat = authority();
  boat.requires_water = true;
  boat.min_water_support_samples = 2;

  auto context = controller_context();
  context.water_support_samples = 1;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      vehicle_move(1, {0.0F, 4.0F, 0.0F}),
      {},
      context,
      boat);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::MissingWaterSupport);
}
