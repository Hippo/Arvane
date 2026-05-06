#include "arv/physics/collision.hpp"

#include "arv/world/world_queries.hpp"
#include "arv/world/world_runtime.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

namespace {

void ensure_loaded_chunk(arv::world::WorldRuntime& world, arv::ChunkSectionCoord coord) {
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

TEST(PhysicsTests, HeldJumpLaunchesWhenGrounded) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.jump_velocity = 7.0F;

  arv::physics::KinematicInput input;
  input.jump_held = true;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, true, false},
      0.0F,
      1.0F / 60.0F,
      input,
      config);

  EXPECT_FLOAT_EQ(result.body.velocity.y, config.jump_velocity);
  EXPECT_FALSE(result.body.grounded);
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

TEST(PhysicsTests, SurfaceSwimmingStopsNaturalSinking) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.surface_buoyancy = 12.0F;
  config.liquid_drag = 0.5F;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {0.0F, -4.0F, 0.0F}, false, false},
      0.0F,
      1.0F / 60.0F,
      {},
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_GE(result.body.velocity.y, 0.0F);
  EXPECT_FALSE(result.body.grounded);
}

TEST(PhysicsTests, SurfaceSwimmingFloatsOnlyUntilFeetAreOneBlockDeep) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));
  ASSERT_TRUE(world.place_fluid_source({{0, 2, 0}}, water));
  ASSERT_TRUE(world.place_fluid_source({{0, 3, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.surface_float_depth = 1.0F;
  config.surface_buoyancy = 120.0F;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 2.5F, 0.5F}, {}, false, false},
      0.0F,
      1.0F,
      {},
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_NEAR(result.body.position.y, 3.0F, 0.0001F);
  EXPECT_NEAR(result.body.velocity.y, 0.5F, 0.0001F);
}

TEST(PhysicsTests, SurfaceSwimmingSettlesDownFromWaterTop) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.surface_float_depth = 1.0F;
  config.surface_buoyancy = 12.0F;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.99F, 0.5F}, {}, false, false},
      0.0F,
      1.0F,
      {},
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_LT(result.body.position.y, 1.99F);
  EXPECT_NEAR(result.body.position.y, 1.0F, 0.0001F);
  EXPECT_LE(result.body.velocity.y, 0.0F);
  EXPECT_FALSE(result.body.grounded);
}

TEST(PhysicsTests, ShallowWaterSettlesPlayerAtBottomInsteadOfSurface) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.surface_float_depth = 1.0F;
  config.liquid_drag = 0.5F;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.2F, 0.5F}, {0.0F, -4.0F, 0.0F}, false, false},
      0.0F,
      1.0F,
      {},
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_NEAR(result.body.position.y, 1.0F, 0.002F);
  EXPECT_LE(result.body.velocity.y, 0.0F);
}

TEST(PhysicsTests, JumpAtWaterSurfaceLaunchesOutOfWater) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.jump_velocity = 8.0F;

  arv::physics::KinematicInput input;
  input.jump_pressed = true;
  input.jump_held = true;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, false, false},
      0.0F,
      1.0F / 60.0F,
      input,
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_GE(result.body.velocity.y, config.jump_velocity);
  EXPECT_GT(result.body.position.y, 1.0F);
}

TEST(PhysicsTests, SneakDescendsWithoutEnteringSneakPoseInLiquid) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.downward_swim_acceleration = 18.0F;

  arv::physics::KinematicInput input;
  input.sneak_held = true;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, false, false},
      0.0F,
      1.0F / 60.0F,
      input,
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_LT(result.body.velocity.y, 0.0F);
  EXPECT_FALSE(result.body.sneaking);
}

TEST(PhysicsTests, UnderwaterJumpSwimsUpAndConsumesBreath) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));
  ASSERT_TRUE(world.place_fluid_source({{0, 2, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.breath_seconds = 15.0F;
  config.upward_swim_acceleration = 18.0F;

  arv::physics::KinematicInput input;
  input.jump_held = true;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, false, false, false, 10.0F},
      0.0F,
      1.0F,
      input,
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingUnderwater);
  EXPECT_GT(result.body.velocity.y, 0.0F);
  EXPECT_FLOAT_EQ(result.body.breath_seconds, 9.0F);
}

TEST(PhysicsTests, FastSwimmingUsesSwimmingPoseAndConfiguredSpeed) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.fast_surface_swim_speed = 4.25F;
  config.swimming_body_height = 0.55F;

  arv::physics::KinematicInput input;
  input.move_z = 1;
  input.sprint_held = true;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, false, false},
      0.0F,
      1.0F / 60.0F,
      input,
      config);

  EXPECT_TRUE(result.body.swimming);
  EXPECT_EQ(result.pose, arv::physics::PlayerCollisionPose::Swimming);
  EXPECT_FLOAT_EQ(result.body.velocity.z, config.fast_surface_swim_speed);
  EXPECT_NEAR(
      result.movement.resolved_bounds.max.y - result.movement.resolved_bounds.min.y,
      config.swimming_body_height,
      0.0001F);
}

TEST(PhysicsTests, FastSurfaceSwimmingDoesNotForceUnderwaterState) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.breath_seconds = 15.0F;
  config.fast_surface_swim_speed = 3.0F;

  arv::physics::KinematicInput input;
  input.move_z = 1;
  input.sprint_held = true;

  arv::physics::KinematicStepResult first = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, false, false, false, config.breath_seconds},
      0.0F,
      1.0F / 60.0F,
      input,
      config);
  ASSERT_TRUE(first.body.swimming);

  const arv::physics::KinematicStepResult second = arv::physics::step_kinematic_body(
      query,
      first.body,
      0.0F,
      1.0F / 60.0F,
      input,
      config);

  EXPECT_EQ(second.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_FLOAT_EQ(second.body.breath_seconds, config.breath_seconds);
}

TEST(PhysicsTests, SwimmingPosePersistsWhenStandingPoseIsBlocked) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 2, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.swimming_body_height = 0.6F;
  config.body_height = 1.8F;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, false, false, true},
      0.0F,
      1.0F / 60.0F,
      {},
      config);

  EXPECT_TRUE(result.body.swimming);
  EXPECT_EQ(result.pose, arv::physics::PlayerCollisionPose::Swimming);
}
