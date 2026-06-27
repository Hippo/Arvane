#include "arv/server/player_movement_validation.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {

arv::protocol::ServerboundPlayerMove move_at(
    arv::u32 sequence,
    arv::Vec3f position) {
  arv::protocol::ServerboundPlayerMove move;
  move.sequence = sequence;
  move.client_tick = sequence;
  move.position = position;
  move.flags = arv::protocol::PlayerMoveFlags::OnGround;
  return move;
}

arv::server::AcceptedPlayerMovementState accepted_at(
    arv::u32 sequence,
    arv::Vec3f position,
    double receive_time_seconds = 1.0) {
  return {
      position,
      {},
      0.0F,
      0.0F,
      arv::protocol::PlayerMoveFlags::OnGround,
      sequence,
      sequence,
      receive_time_seconds,
      true};
}

} // namespace

TEST(MovementValidationTests, AcceptsFirstFiniteMoveAtSpawn) {
  const arv::io::MovementValidationConfig config;
  const arv::server::AcceptedPlayerMovementState previous;
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.0;

  const auto result = arv::server::validate_serverbound_player_move(
      move_at(1, {8.0F, 20.0F, -20.0F}),
      previous,
      context,
      config);

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.accepted_state.position, (arv::Vec3f{8.0F, 20.0F, -20.0F}));
}

TEST(MovementValidationTests, AcceptsNormalWalkingDelta) {
  const arv::io::MovementValidationConfig config;
  const auto previous = accepted_at(1, {0.0F, 20.0F, 0.0F}, 1.0);
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.05;

  const auto result = arv::server::validate_serverbound_player_move(
      move_at(2, {0.3F, 20.0F, 0.0F}),
      previous,
      context,
      config);

  EXPECT_TRUE(result.accepted);
}

TEST(MovementValidationTests, RejectsNaNPosition) {
  const arv::io::MovementValidationConfig config;
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.0;
  auto move = move_at(1, {0.0F, 20.0F, 0.0F});
  move.position.x = std::numeric_limits<arv::f32>::quiet_NaN();

  const auto result = arv::server::validate_serverbound_player_move(
      move,
      {},
      context,
      config);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::PlayerMoveCorrectionReason::InvalidNumber);
}

TEST(MovementValidationTests, RejectsInfinityRotation) {
  const arv::io::MovementValidationConfig config;
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.0;
  auto move = move_at(1, {0.0F, 20.0F, 0.0F});
  move.yaw_radians = std::numeric_limits<arv::f32>::infinity();

  const auto result = arv::server::validate_serverbound_player_move(
      move,
      {},
      context,
      config);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::PlayerMoveCorrectionReason::InvalidNumber);
}

TEST(MovementValidationTests, RejectsStaleSequence) {
  const arv::io::MovementValidationConfig config;
  const auto previous = accepted_at(3, {0.0F, 20.0F, 0.0F});
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.05;

  const auto result = arv::server::validate_serverbound_player_move(
      move_at(3, {0.1F, 20.0F, 0.0F}),
      previous,
      context,
      config);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::PlayerMoveCorrectionReason::StalePacket);
}

TEST(MovementValidationTests, RejectsHugeTeleportWithoutServerTeleport) {
  const arv::io::MovementValidationConfig config;
  const auto previous = accepted_at(1, {0.0F, 20.0F, 0.0F}, 1.0);
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.05;

  const auto result = arv::server::validate_serverbound_player_move(
      move_at(2, {100.0F, 20.0F, 0.0F}),
      previous,
      context,
      config);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::PlayerMoveCorrectionReason::TooFast);
}

TEST(MovementValidationTests, RejectsMovementIntoUnloadedChunkWhenConfigured) {
  arv::io::MovementValidationConfig config;
  config.reject_unloaded_chunk_movement = true;
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.0;
  context.target_chunk_loaded = false;

  const auto result = arv::server::validate_serverbound_player_move(
      move_at(1, {0.0F, 20.0F, 0.0F}),
      {},
      context,
      config);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::PlayerMoveCorrectionReason::UnloadedChunk);
}

TEST(MovementValidationTests, AllowsVoidSpaceMovementWithoutLoadedChunks) {
  arv::io::MovementValidationConfig config;
  config.reject_unloaded_chunk_movement = true;
  config.lag_grace_blocks = 0.0F;
  const auto previous = accepted_at(1, {0.0F, 20.0F, 0.0F}, 1.0);
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.05;
  context.target_chunk_loaded = false;
  context.void_space_movement = true;

  auto move = move_at(2, {2.0F, 21.0F, 0.0F});
  move.flags = arv::protocol::PlayerMoveFlags::None;

  const auto result = arv::server::validate_serverbound_player_move(
      move,
      previous,
      context,
      config);

  EXPECT_TRUE(result.accepted);
}

TEST(MovementValidationTests, AllowsStationaryAcceptedPositionBeforeChunkLoads) {
  arv::io::MovementValidationConfig config;
  config.reject_unloaded_chunk_movement = true;
  const auto previous = accepted_at(1, {8.0F, 20.0F, -20.0F}, 1.0);
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.05;
  context.target_chunk_loaded = false;

  const auto result = arv::server::validate_serverbound_player_move(
      move_at(2, {8.0F, 20.0F, -20.0F}),
      previous,
      context,
      config);

  EXPECT_TRUE(result.accepted);
}

TEST(MovementValidationTests, RejectsSolidCollisionWhenConfigured) {
  arv::io::MovementValidationConfig config;
  config.solid_collision_check = true;
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.0;
  context.final_aabb_overlaps_solid = true;

  const auto result = arv::server::validate_serverbound_player_move(
      move_at(1, {0.0F, 20.0F, 0.0F}),
      {},
      context,
      config);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::PlayerMoveCorrectionReason::Collision);
}

TEST(MovementValidationTests, AllowsAuthorizedFlyingSpeed) {
  const arv::io::MovementValidationConfig config;
  const auto previous = accepted_at(1, {0.0F, 20.0F, 0.0F}, 1.0);
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.25;
  context.can_fly = true;
  auto move = move_at(2, {5.0F, 20.0F, 0.0F});
  move.flags = arv::protocol::PlayerMoveFlags::Flying;

  const auto result = arv::server::validate_serverbound_player_move(
      move,
      previous,
      context,
      config);

  EXPECT_TRUE(result.accepted);
}

TEST(MovementValidationTests, UsesServerReceiveTimeNotClientTick) {
  const arv::io::MovementValidationConfig config;
  const auto previous = accepted_at(1, {0.0F, 20.0F, 0.0F}, 1.0);
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.01;
  auto move = move_at(2, {4.0F, 20.0F, 0.0F});
  move.client_tick = 1000000;

  const auto result = arv::server::validate_serverbound_player_move(
      move,
      previous,
      context,
      config);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, arv::protocol::PlayerMoveCorrectionReason::TooFast);
}

TEST(MovementValidationTests, AllowsRecentServerKnockbackVelocity) {
  arv::io::MovementValidationConfig config;
  config.lag_grace_blocks = 0.0F;
  config.base_position_epsilon_blocks = 0.0F;
  config.max_ground_speed_blocks_per_second = 1.0F;
  const auto previous = accepted_at(1, {0.0F, 20.0F, 0.0F}, 1.0);
  arv::server::MovementValidationContext context;
  context.receive_time_seconds = 1.05;
  context.server_velocity = {8.0F, 1.5F, 0.0F};
  context.knockback_control_suppression_ticks = 2;

  const auto result = arv::server::validate_serverbound_player_move(
      move_at(2, {0.42F, 20.05F, 0.0F}),
      previous,
      context,
      config);

  EXPECT_TRUE(result.accepted);
}
