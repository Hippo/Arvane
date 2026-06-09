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

arv::entity::VehicleCrashDefinition crash_tuning() {
  arv::entity::VehicleCrashDefinition crash;
  crash.enabled = true;
  crash.bump_threshold = 2.0F;
  crash.hard_impact_threshold = 8.0F;
  crash.ejection_threshold = 16.0F;
  crash.destruction_threshold = 24.0F;
  crash.min_direct_impact_dot = 0.35F;
  return crash;
}

} // namespace

TEST(VehicleCrashAssessmentTests, UsesImpactSpeedIntoNormalInsteadOfRawSpeed) {
  auto contact = arv::server::VehicleCrashContact{
      {-1.0F, 0.0F, 0.0F},
      {0.0F, 0.0F, -40.0F},
      true};

  const auto assessment =
      arv::server::assess_vehicle_crash(crash_tuning(), contact);

  EXPECT_EQ(assessment.severity, arv::entity::CrashSeverity::None);
  EXPECT_FLOAT_EQ(assessment.normal_impact_speed, 0.0F);
}

TEST(VehicleCrashAssessmentTests, ClassifiesDirectThresholds) {
  auto contact = arv::server::VehicleCrashContact{
      {-1.0F, 0.0F, 0.0F},
      {18.0F, 0.0F, 0.0F},
      true};

  auto assessment = arv::server::assess_vehicle_crash(crash_tuning(), contact);

  EXPECT_EQ(assessment.severity, arv::entity::CrashSeverity::Ejection);
  EXPECT_FLOAT_EQ(assessment.normal_impact_speed, 18.0F);

  contact.pre_impact_velocity = {28.0F, 0.0F, 0.0F};
  assessment = arv::server::assess_vehicle_crash(crash_tuning(), contact);

  EXPECT_EQ(assessment.severity, arv::entity::CrashSeverity::Destruction);
}

TEST(VehicleCrashAssessmentTests, ShallowScrapesAreCappedAtBump) {
  auto contact = arv::server::VehicleCrashContact{
      {-1.0F, 0.0F, 0.0F},
      {2.1F, 0.0F, -30.0F},
      true};

  const auto assessment =
      arv::server::assess_vehicle_crash(crash_tuning(), contact);

  EXPECT_EQ(assessment.severity, arv::entity::CrashSeverity::Bump);
  EXPECT_LT(assessment.direct_impact_dot, 0.35F);
}

TEST(VehicleCrashAssessmentTests, AircraftLandingUsesVerticalImpactOnly) {
  arv::entity::VehicleCrashDefinition aircraft = crash_tuning();
  aircraft.bump_threshold = 6.0F;
  aircraft.hard_impact_threshold = 16.0F;
  aircraft.ejection_threshold = 34.0F;
  aircraft.destruction_threshold = 46.0F;
  const auto rough_landing = arv::server::VehicleCrashContact{
      {0.0F, 1.0F, 0.0F},
      {32.0F, -5.0F, 0.0F},
      true};

  auto assessment = arv::server::assess_vehicle_crash(aircraft, rough_landing);

  EXPECT_EQ(assessment.severity, arv::entity::CrashSeverity::None);
  EXPECT_FLOAT_EQ(assessment.normal_impact_speed, 5.0F);

  const auto hard_landing = arv::server::VehicleCrashContact{
      {0.0F, 1.0F, 0.0F},
      {12.0F, -20.0F, 0.0F},
      true};
  assessment = arv::server::assess_vehicle_crash(aircraft, hard_landing);

  EXPECT_EQ(assessment.severity, arv::entity::CrashSeverity::HardImpact);
}

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

TEST(VehicleMovementValidationTests, AcceptsDirectionChangeWithinSpeedAndRotationGuardrails) {
  auto move = vehicle_move(2, {0.0F, 4.0F, 0.25F});
  move.velocity = {0.0F, 0.0F, 5.0F};

  auto previous = accepted_at(1, {0.0F, 4.0F, 0.0F});
  previous.velocity = {5.0F, 0.0F, 0.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      previous,
      controller_context(),
      authority());

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.accepted_state.velocity, (arv::Vec3f{0.0F, 0.0F, 5.0F}));
}

TEST(VehicleMovementValidationTests, AcceptsImpactStopWithoutCorrectionStorm) {
  auto move = vehicle_move(2, {0.4F, 4.0F, 0.0F});
  move.velocity = {};

  auto previous = accepted_at(1, {0.0F, 4.0F, 0.0F});
  previous.velocity = {10.0F, 0.0F, 0.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      previous,
      controller_context(),
      authority());

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.accepted_state.velocity, (arv::Vec3f{}));
}

TEST(VehicleMovementValidationTests, AcceptsImpactSlideWithoutCorrectionStorm) {
  auto move = vehicle_move(2, {0.35F, 4.0F, -0.1F});
  move.velocity = {8.0F, 0.0F, 0.0F};

  auto previous = accepted_at(1, {0.0F, 4.0F, 0.0F});
  previous.velocity = {8.0F, 0.0F, -8.0F};
  auto context = controller_context();
  context.previous_velocity_sweep_hit_solid = true;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      previous,
      context,
      authority());

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.accepted_state.velocity, move.velocity);
}

TEST(VehicleMovementValidationTests, CollisionCorrectionStopsAtLastSafeState) {
  auto context = controller_context();
  context.final_aabb_overlaps_solid = true;

  auto previous = accepted_at(4, {1.0F, 4.0F, 0.0F});
  previous.velocity = {8.0F, 0.0F, 0.0F};
  previous.angular_velocity = {0.0F, 1.0F, 0.0F};

  auto move = vehicle_move(5, {1.4F, 4.0F, 0.0F});
  move.velocity = {8.0F, 0.0F, 0.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      previous,
      context,
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::Collision);
  EXPECT_EQ(result.accepted_state.position, previous.position);
  EXPECT_EQ(result.accepted_state.velocity, (arv::Vec3f{}));
  EXPECT_EQ(result.accepted_state.angular_velocity, (arv::Vec3f{}));
}

TEST(VehicleMovementValidationTests, CollisionCorrectionCanUseResolvedSafeState) {
  auto context = controller_context();
  context.final_aabb_overlaps_solid = true;
  context.has_collision_resolved_state = true;
  context.collision_resolved_state =
      accepted_at(5, {1.0F, 4.0F, -0.4F}, context.receive_time_seconds);
  context.collision_resolved_state.velocity = {2.0F, 0.0F, 0.0F};
  context.collision_resolved_state.angular_velocity = {0.0F, 0.25F, 0.0F};

  auto previous = accepted_at(4, {1.0F, 4.0F, 0.0F});
  previous.velocity = {2.0F, 0.0F, -8.0F};

  auto move = vehicle_move(5, {1.0F, 4.0F, -0.6F});
  move.velocity = {2.0F, 0.0F, -8.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      previous,
      context,
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::Collision);
  EXPECT_EQ(result.accepted_state.position, context.collision_resolved_state.position);
  EXPECT_EQ(result.accepted_state.velocity, context.collision_resolved_state.velocity);
  EXPECT_EQ(
      result.accepted_state.angular_velocity,
      context.collision_resolved_state.angular_velocity);
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

TEST(VehicleMovementValidationTests, RejectsFinalAabbInsideSolid) {
  auto context = controller_context();
  context.final_aabb_overlaps_solid = true;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      vehicle_move(1, {0.0F, 4.0F, 0.0F}),
      {},
      context,
      authority());

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::Collision);
}

TEST(VehicleMovementValidationTests, RejectsGroundRequiredVehicleClaimingUnsupportedGround) {
  auto ground_required = authority();
  ground_required.requires_ground = true;

  auto context = controller_context();
  context.has_ground_support = false;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      vehicle_move(1, {0.0F, 4.0F, 0.0F}),
      {},
      context,
      ground_required);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::MissingGroundSupport);
}

TEST(VehicleMovementValidationTests, AcceptsAirborneGroundVehicleWithinGuardrails) {
  auto ground_required = authority();
  ground_required.requires_ground = true;

  auto context = controller_context();
  context.has_ground_support = false;

  auto move = vehicle_move(2, {0.1F, 3.98F, 0.0F});
  move.velocity = {2.0F, -0.4F, 0.0F};
  move.flags = arv::protocol::VehicleMoveFlags::None;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      accepted_at(1, {0.0F, 4.0F, 0.0F}),
      context,
      ground_required);

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.accepted_state.flags, arv::protocol::VehicleMoveFlags::None);
  EXPECT_EQ(result.accepted_state.velocity, (arv::Vec3f{2.0F, -0.4F, 0.0F}));
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

TEST(VehicleMovementValidationTests, RejectsAircraftSweptCollisionPath) {
  auto aircraft = authority();
  aircraft.requires_swept_collision = true;

  auto context = controller_context();
  context.proposed_sweep_hit_solid = true;

  auto previous = accepted_at(1, {0.0F, 20.0F, 0.0F});
  previous.velocity = {20.0F, 0.0F, 0.0F};

  auto move = vehicle_move(2, {1.0F, 20.0F, 0.0F});
  move.velocity = {20.0F, 0.0F, 0.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      previous,
      context,
      aircraft);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::VehicleCorrectionReason::Collision);
}

TEST(VehicleMovementValidationTests, RejectsAircraftEnteringUnloadedChunk) {
  auto aircraft = authority();
  aircraft.max_unloaded_chunk_entry_distance = 0.0F;

  auto context = controller_context();
  context.target_chunk_loaded = false;

  const auto result = arv::server::validate_serverbound_vehicle_move(
      vehicle_move(1, {32.0F, 20.0F, 0.0F}),
      {},
      context,
      aircraft);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(
      result.reason,
      arv::protocol::VehicleCorrectionReason::AircraftEnteredUnloadedChunk);
}

TEST(VehicleMovementValidationTests, RejectsSubmersibleUsingWaterEnvelopeInAir) {
  auto submarine = authority();
  submarine.min_water_occupancy = 0.35F;

  auto context = controller_context();
  context.water_occupancy = 0.0F;

  auto move = vehicle_move(1, {0.0F, 20.0F, 0.0F});
  move.flags = arv::protocol::VehicleMoveFlags::InWater;
  move.velocity = {8.0F, 1.0F, 0.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      {},
      context,
      submarine);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(
      result.reason,
      arv::protocol::VehicleCorrectionReason::InvalidSubmersibleMedium);
}

TEST(VehicleMovementValidationTests, AcceptsSubmersibleOutOfWaterFallbackFall) {
  auto submarine = authority();
  submarine.min_water_occupancy = 0.35F;

  auto context = controller_context();
  context.water_occupancy = 0.0F;

  auto move = vehicle_move(2, {0.0F, 19.98F, 0.0F});
  move.flags = arv::protocol::VehicleMoveFlags::None;
  move.velocity = {0.2F, -0.4F, 0.0F};

  const auto result = arv::server::validate_serverbound_vehicle_move(
      move,
      accepted_at(1, {0.0F, 20.0F, 0.0F}),
      context,
      submarine);

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.accepted_state.velocity, move.velocity);
}
