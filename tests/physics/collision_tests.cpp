#include "arv/physics/collision.hpp"

#include "arv/world/world_queries.hpp"
#include "arv/world/world_runtime.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

namespace {

void ensure_loaded_chunk(arv::world::WorldRuntime& world, arv::ChunkCoord coord) {
  (void)world.ensure_chunk(coord);
}

} // namespace

TEST(PhysicsTests, ResolveMovementClampsAgainstSolidTerrainAabb) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  const arv::physics::KinematicConfig config;
  const arv::Aabb3f bounds = arv::physics::make_body_bounds({0.5F, 1.0F, 0.5F}, config);

  const arv::physics::MovementResolution result =
      arv::physics::resolve_movement(query, bounds, {1.0F, 0.0F, 0.0F});

  EXPECT_TRUE(result.hit_x);
  EXPECT_NEAR(result.applied_delta.x, 0.199F, 0.001F);
  EXPECT_NEAR(result.applied_delta.y, 0.0F, 0.0001F);
  EXPECT_NEAR(result.applied_delta.z, 0.0F, 0.0001F);
}

TEST(PhysicsTests, PerAxisResolutionAllowsSlidingAlongNonBlockingAxis) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  const arv::physics::KinematicConfig config;
  const arv::Aabb3f bounds = arv::physics::make_body_bounds({0.5F, 1.0F, 0.5F}, config);

  const arv::physics::MovementResolution result =
      arv::physics::resolve_movement(query, bounds, {1.0F, 0.0F, 1.0F});

  EXPECT_TRUE(result.hit_x);
  EXPECT_FALSE(result.hit_z);
  EXPECT_NEAR(result.applied_delta.x, 0.199F, 0.001F);
  EXPECT_NEAR(result.applied_delta.z, 1.0F, 0.0001F);
}

TEST(PhysicsTests, GroundDetectionFindsSupportingBlocksBelowBody) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  const arv::physics::KinematicConfig config;
  const arv::Aabb3f bounds = arv::physics::make_body_bounds({0.5F, 1.0F, 0.5F}, config);

  EXPECT_TRUE(arv::physics::has_support_below(query, bounds));
}

TEST(PhysicsTests, RaycastHitsTargetBlockWithinReach) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{3, 1, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  const std::optional<arv::physics::BlockRaycastHit> hit = arv::physics::raycast_blocks(
      query,
      {0.5F, 1.62F, 0.5F},
      {1.0F, 0.0F, 0.0F},
      5.0F);

  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->block, (arv::BlockPos{{3, 1, 0}}));
  EXPECT_EQ(hit->variant, stone);
  EXPECT_EQ(hit->face, arv::voxel::BlockFace::NegativeX);
  EXPECT_FLOAT_EQ(hit->distance, 2.5F);
}

TEST(PhysicsTests, RaycastReturnsMissBeyondReachLimit) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{3, 1, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  const std::optional<arv::physics::BlockRaycastHit> hit = arv::physics::raycast_blocks(
      query,
      {0.5F, 1.62F, 0.5F},
      {1.0F, 0.0F, 0.0F},
      2.0F);

  EXPECT_FALSE(hit.has_value());
}

TEST(PhysicsTests, SneakingPreventsWalkingOffSupportedLedge) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.walk_speed = 1.0F;
  config.sneak_speed = 1.0F;

  arv::physics::KinematicInput input;
  input.move_z = 1;
  input.sneak_held = true;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, true, false},
      0.0F,
      1.0F,
      input,
      config);

  EXPECT_TRUE(result.body.sneaking);
  EXPECT_NEAR(result.body.position.z, 0.5F, 0.0001F);
  EXPECT_TRUE(result.body.grounded);
}

TEST(PhysicsTests, StandingUpUnderLowCeilingKeepsBodySneaking) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{0, 2, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.walk_speed = 0.0F;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, true, true},
      0.0F,
      1.0F / 60.0F,
      {},
      config);

  EXPECT_TRUE(result.body.sneaking);
}
