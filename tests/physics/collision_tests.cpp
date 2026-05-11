#include "arv/physics/collision.hpp"

#include "arv/physics/player_movement.hpp"
#include "arv/world/world_queries.hpp"
#include "arv/world/world_runtime.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

void ensure_loaded_chunk(arv::world::WorldRuntime& world, arv::ChunkSectionCoord coord) {
  (void)world.ensure_chunk(coord);
}

void place_flat_floor(
    arv::world::WorldRuntime& world,
    arv::voxel::BlockVariantID block,
    arv::i32 min_x,
    arv::i32 max_x,
    arv::i32 min_z,
    arv::i32 max_z) {
  ensure_loaded_chunk(world, {{0, 0, 0}});
  for (arv::i32 z = min_z; z <= max_z; ++z) {
    for (arv::i32 x = min_x; x <= max_x; ++x) {
      ASSERT_TRUE(world.set_block({{x, 0, z}}, block));
    }
  }
}

arv::physics::KinematicBody simulate_kinematic_ticks(
    const arv::physics::ICollisionQuery& query,
    arv::physics::KinematicBody body,
    arv::f32 yaw_radians,
    arv::Seconds tick_seconds,
    const arv::physics::KinematicInput& input,
    const arv::physics::KinematicConfig& config,
    int ticks) {
  for (int tick = 0; tick < ticks; ++tick) {
    const arv::physics::KinematicStepResult step = arv::physics::step_kinematic_body(
        query,
        body,
        yaw_radians,
        tick_seconds,
        input,
        config);
    body = step.body;
  }
  return body;
}

arv::voxel::BlockRegistry make_step_test_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  arv::voxel::BlockDefinition half_block;
  half_block.name = "arvane:half_block";
  half_block.properties.solid = true;
  half_block.properties.collidable = true;
  half_block.render_category = arv::voxel::RenderCategory::None;
  definitions.push_back(std::move(half_block));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry_result);
  arv::voxel::BlockRegistry registry = std::move(registry_result.value());
  registry.set_collision_boxes(
      registry.find_variant("arvane:half_block").value(),
      {{{0.0F, 0.0F, 0.0F}, {1.0F, 0.5F, 1.0F}}});
  return registry;
}

arv::voxel::BlockRegistry make_surface_test_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  arv::voxel::BlockDefinition slippery =
      arv::voxel::make_full_cube_definition("arvane:test_ice");
  slippery.physics.slipperiness = 0.98F;
  definitions.push_back(std::move(slippery));

  arv::voxel::BlockDefinition grippy =
      arv::voxel::make_full_cube_definition("arvane:test_grip");
  grippy.physics.slipperiness = 0.35F;
  definitions.push_back(std::move(grippy));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry_result);
  return std::move(registry_result.value());
}

arv::f32 simulate_release_distance_on_surface(
    arv::voxel::BlockVariantID surface,
    const arv::voxel::BlockRegistry& registry) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  place_flat_floor(world, surface, 0, 1, 0, 12);

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;

  arv::physics::KinematicInput forward;
  forward.move_z = 1;
  arv::physics::KinematicBody body{
      {0.5F, 1.0F, 0.5F},
      {},
      true,
      false,
      false,
      config.breath_seconds};

  body = simulate_kinematic_ticks(query, body, 0.0F, 1.0F / 60.0F, forward, config, 30);
  const arv::f32 release_start_z = body.position.z;
  body = simulate_kinematic_ticks(query, body, 0.0F, 1.0F / 60.0F, {}, config, 30);
  return body.position.z - release_start_z;
}

} // namespace

TEST(PhysicsMovementTests, DefaultGroundWalkForwardStable) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  place_flat_floor(world, stone, 0, 1, 0, 8);

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.walk_speed = 4.5F;

  arv::physics::KinematicInput input;
  input.move_z = 1;

  const arv::physics::KinematicBody body = simulate_kinematic_ticks(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, true, false, false, config.breath_seconds},
      0.0F,
      1.0F / 60.0F,
      input,
      config,
      60);

  EXPECT_TRUE(body.grounded);
  EXPECT_NEAR(body.position.z, 5.0F, 0.05F);
  EXPECT_FLOAT_EQ(body.velocity.z, config.walk_speed);
  EXPECT_FLOAT_EQ(body.velocity.x, 0.0F);
}

TEST(PhysicsMovementTests, DefaultGroundSprintFasterThanWalk) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  place_flat_floor(world, stone, 0, 1, 0, 10);

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.walk_speed = 4.5F;
  config.sprint_speed = 6.0F;

  arv::physics::KinematicInput walk_input;
  walk_input.move_z = 1;
  arv::physics::KinematicInput sprint_input = walk_input;
  sprint_input.sprint_held = true;

  const arv::physics::KinematicBody start{
      {0.5F, 1.0F, 0.5F},
      {},
      true,
      false,
      false,
      config.breath_seconds};
  const arv::physics::KinematicBody walked =
      simulate_kinematic_ticks(query, start, 0.0F, 1.0F / 60.0F, walk_input, config, 60);
  const arv::physics::KinematicBody sprinted =
      simulate_kinematic_ticks(query, start, 0.0F, 1.0F / 60.0F, sprint_input, config, 60);

  EXPECT_GT(sprinted.position.z, walked.position.z + 1.0F);
  EXPECT_FLOAT_EQ(sprinted.velocity.z, config.sprint_speed);
  EXPECT_FLOAT_EQ(walked.velocity.z, config.walk_speed);
}

TEST(PhysicsMovementTests, DefaultGroundSneakSlowerThanWalk) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  place_flat_floor(world, stone, 0, 1, 0, 8);

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.walk_speed = 4.5F;
  config.sneak_speed = 2.25F;

  arv::physics::KinematicInput walk_input;
  walk_input.move_z = 1;
  arv::physics::KinematicInput sneak_input = walk_input;
  sneak_input.sneak_held = true;

  const arv::physics::KinematicBody start{
      {0.5F, 1.0F, 0.5F},
      {},
      true,
      false,
      false,
      config.breath_seconds};
  const arv::physics::KinematicBody walked =
      simulate_kinematic_ticks(query, start, 0.0F, 1.0F / 60.0F, walk_input, config, 60);
  const arv::physics::KinematicBody sneaked =
      simulate_kinematic_ticks(query, start, 0.0F, 1.0F / 60.0F, sneak_input, config, 60);

  EXPECT_TRUE(sneaked.sneaking);
  EXPECT_LT(sneaked.position.z, walked.position.z - 1.0F);
  EXPECT_FLOAT_EQ(sneaked.velocity.z, config.sneak_speed);
}

TEST(PhysicsMovementTests, NoCollisionFlyModeMovesByFlySpeed) {
  arv::player::PlayerTuning tuning = arv::player::default_player_tuning();
  tuning.fly_speed = 8.0F;
  tuning.fly_vertical_speed = 6.0F;

  arv::physics::PlayerKinematicState state;
  state.position = {1.0F, 2.0F, 3.0F};
  state.movement_mode = arv::player::MovementMode::Flying;

  arv::player::PlayerCommand command;
  command.move_z = 1;
  command.jump_held = true;

  const arv::physics::PlayerMovementStep step = arv::physics::simulate_player_movement_tick(
      nullptr,
      state,
      command,
      tuning,
      {.can_fly = true, .has_collision = false},
      0.5F);

  EXPECT_FALSE(step.used_collision);
  EXPECT_FALSE(step.state.grounded);
  EXPECT_FALSE(step.state.swimming);
  EXPECT_FLOAT_EQ(step.state.velocity.z, tuning.fly_speed);
  EXPECT_FLOAT_EQ(step.state.velocity.y, tuning.fly_vertical_speed);
  EXPECT_FLOAT_EQ(step.state.position.z, 7.0F);
  EXPECT_FLOAT_EQ(step.state.position.y, 5.0F);
}

TEST(PhysicsMovementTests, UnderwaterFastSwimUsesPitchAndDrainsBreath) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));
  ASSERT_TRUE(world.place_fluid_source({{0, 2, 0}}, water));
  ASSERT_TRUE(world.place_fluid_source({{0, 3, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.fast_underwater_swim_speed = 5.0F;
  config.underwater_buoyancy = 1.0F;
  config.breath_seconds = 15.0F;

  arv::physics::KinematicInput input;
  input.move_z = 1;
  input.sprint_held = true;
  input.pitch_radians = arv::degrees_to_radians(30.0F);

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, false, false, false, 10.0F},
      0.0F,
      1.0F / 60.0F,
      input,
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingUnderwater);
  EXPECT_TRUE(result.body.swimming);
  EXPECT_NEAR(result.body.velocity.z, 4.330127F, 0.0001F);
  EXPECT_NEAR(result.body.velocity.y, 2.516667F, 0.0001F);
  EXPECT_NEAR(result.body.breath_seconds, 10.0F - 1.0F / 60.0F, 0.0001F);
}

TEST(PhysicsMovementTests, UnderwaterSwimmingAppliesLiquidDragBuoyancyAndBreathDrain) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));
  ASSERT_TRUE(world.place_fluid_source({{0, 2, 0}}, water));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.liquid_drag = 0.5F;
  config.underwater_buoyancy = 1.0F;
  config.breath_seconds = 15.0F;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {0.0F, -4.0F, 0.0F}, false, false, false, 10.0F},
      0.0F,
      1.0F,
      {},
      config);

  EXPECT_EQ(result.liquid_state, arv::physics::LiquidContactState::SwimmingUnderwater);
  EXPECT_NEAR(result.body.velocity.y, -1.0F, 0.0001F);
  EXPECT_FLOAT_EQ(result.body.breath_seconds, 9.0F);
}

TEST(PhysicsMovementTests, SupportSurfaceProbeReturnsBlockSlipperiness) {
  const arv::voxel::BlockRegistry registry = make_surface_test_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID ice = registry.find_variant("arvane:test_ice").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 0, 0}}, ice));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  const arv::physics::KinematicConfig config;

  const arv::physics::SupportSurfaceProbe stone_probe =
      arv::physics::probe_support_surface(
          query,
          arv::physics::make_body_bounds({0.5F, 1.0F, 0.5F}, config));
  const arv::physics::SupportSurfaceProbe ice_probe =
      arv::physics::probe_support_surface(
          query,
          arv::physics::make_body_bounds({1.5F, 1.0F, 0.5F}, config));

  ASSERT_TRUE(stone_probe.supported);
  ASSERT_TRUE(ice_probe.supported);
  EXPECT_EQ(stone_probe.block, (arv::BlockPos{{0, 0, 0}}));
  EXPECT_EQ(ice_probe.block, (arv::BlockPos{{1, 0, 0}}));
  EXPECT_FLOAT_EQ(stone_probe.physics.slipperiness, arv::voxel::DefaultBlockSlipperiness);
  EXPECT_FLOAT_EQ(ice_probe.physics.slipperiness, 0.98F);
}

TEST(PhysicsMovementTests, GroundFrictionSlidesFartherOnSlipperyBlocks) {
  const arv::voxel::BlockRegistry registry = make_surface_test_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID ice = registry.find_variant("arvane:test_ice").value();
  const arv::voxel::BlockVariantID grippy = registry.find_variant("arvane:test_grip").value();

  const arv::f32 default_release_distance =
      simulate_release_distance_on_surface(stone, registry);
  const arv::f32 slippery_release_distance =
      simulate_release_distance_on_surface(ice, registry);
  const arv::f32 grippy_release_distance =
      simulate_release_distance_on_surface(grippy, registry);

  EXPECT_GT(slippery_release_distance, default_release_distance + 0.05F);
  EXPECT_GT(default_release_distance, grippy_release_distance + 0.01F);
}

TEST(PhysicsMovementTests, BlockFrictionDoesNotAffectSwimming) {
  const arv::voxel::BlockRegistry registry = make_surface_test_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID ice = registry.find_variant("arvane:test_ice").value();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();

  auto swim_step = [&](arv::voxel::BlockVariantID floor) {
    arv::world::WorldRuntime world{arv::WorldID{1}};
    ensure_loaded_chunk(world, {{0, 0, 0}});
    EXPECT_TRUE(world.set_block({{0, 0, 0}}, floor));
    EXPECT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

    const arv::world::WorldCollisionQuery query(world, registry, false);
    arv::physics::KinematicConfig config;
    config.surface_swim_speed = 2.5F;

    arv::physics::KinematicInput input;
    input.move_z = 1;
    return arv::physics::step_kinematic_body(
        query,
        {{0.5F, 1.0F, 0.5F}, {}, false, false, false, config.breath_seconds},
        0.0F,
        1.0F / 60.0F,
        input,
        config);
  };

  const arv::physics::KinematicStepResult default_floor = swim_step(stone);
  const arv::physics::KinematicStepResult slippery_floor = swim_step(ice);

  EXPECT_EQ(default_floor.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_EQ(slippery_floor.liquid_state, arv::physics::LiquidContactState::SwimmingAtSurface);
  EXPECT_NEAR(default_floor.body.velocity.x, slippery_floor.body.velocity.x, 0.0001F);
  EXPECT_NEAR(default_floor.body.velocity.y, slippery_floor.body.velocity.y, 0.0001F);
  EXPECT_NEAR(default_floor.body.velocity.z, slippery_floor.body.velocity.z, 0.0001F);
}

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

TEST(PhysicsTests, GroundedBodyStepsOntoObstacleWithinStepHeight) {
  const arv::voxel::BlockRegistry registry = make_step_test_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID half_block =
      registry.find_variant("arvane:half_block").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, half_block));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.walk_speed = 1.0F;
  config.step_height = 0.6F;

  arv::physics::KinematicInput input;
  input.move_x = 1;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, true, false},
      0.0F,
      1.0F,
      input,
      config);

  EXPECT_FALSE(result.movement.hit_x);
  EXPECT_TRUE(result.body.grounded);
  EXPECT_NEAR(result.body.position.x, 1.5F, 0.001F);
  EXPECT_NEAR(result.body.position.y, 1.501F, 0.002F);
}

TEST(PhysicsTests, StepHeightDoesNotClimbFullBlockObstacle) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.walk_speed = 1.0F;
  config.step_height = 0.6F;

  arv::physics::KinematicInput input;
  input.move_x = 1;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {}, true, false},
      0.0F,
      1.0F,
      input,
      config);

  EXPECT_TRUE(result.movement.hit_x);
  EXPECT_NEAR(result.body.position.x, 0.699F, 0.002F);
  EXPECT_NEAR(result.body.position.y, 1.0F, 0.0001F);
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

TEST(PhysicsTests, SprintJumpAddsConfiguredHorizontalBoost) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  place_flat_floor(world, stone, 0, 2, 0, 2);

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.sprint_speed = 6.0F;
  config.jump_velocity = 7.0F;
  config.sprint_jump_horizontal_boost = 1.25F;

  arv::physics::KinematicInput input;
  input.move_z = 1;
  input.sprint_held = true;
  input.jump_pressed = true;
  input.jump_held = true;

  const arv::physics::KinematicStepResult boosted = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {0.0F, 0.0F, config.sprint_speed}, true, false},
      0.0F,
      1.0F / 60.0F,
      input,
      config);

  config.sprint_jump_horizontal_boost = 0.0F;
  const arv::physics::KinematicStepResult unboosted = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 1.0F, 0.5F}, {0.0F, 0.0F, config.sprint_speed}, true, false},
      0.0F,
      1.0F / 60.0F,
      input,
      config);

  EXPECT_FLOAT_EQ(boosted.body.velocity.y, config.jump_velocity);
  EXPECT_FALSE(boosted.body.grounded);
  EXPECT_FLOAT_EQ(unboosted.body.velocity.z, config.sprint_speed);
  EXPECT_FLOAT_EQ(boosted.body.velocity.z, config.sprint_speed + 1.25F);
  EXPECT_GT(boosted.body.position.z, unboosted.body.position.z);
}

TEST(PhysicsTests, AirborneMovementPreservesHorizontalVelocityWithLimitedControl) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::physics::KinematicConfig config;
  config.air_speed = 3.0F;
  config.air_control_acceleration_per_second = 9.0F;

  const arv::physics::KinematicStepResult result = arv::physics::step_kinematic_body(
      query,
      {{0.5F, 3.0F, 0.5F}, {8.0F, 4.0F, 0.0F}, false, false},
      0.0F,
      1.0F / 30.0F,
      {},
      config);

  EXPECT_GT(result.body.velocity.x, 7.0F);
  EXPECT_GT(result.body.position.x, 0.73F);
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

TEST(PhysicsTests, RayIntersectsAabbReturnsDistanceFaceAndPoint) {
  const std::optional<arv::physics::RayAabbHit> hit =
      arv::physics::ray_intersects_aabb(
          {0.0F, 1.5F, 1.5F},
          {1.0F, 0.0F, 0.0F},
          {{1.0F, 1.0F, 1.0F}, {2.0F, 2.0F, 2.0F}},
          5.0F);

  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->face, arv::voxel::BlockFace::NegativeX);
  EXPECT_NEAR(hit->distance, 1.0F, 0.0001F);
  EXPECT_NEAR(hit->hit_point.x, 1.0F, 0.0001F);
  EXPECT_NEAR(hit->hit_point.y, 1.5F, 0.0001F);
  EXPECT_NEAR(hit->hit_point.z, 1.5F, 0.0001F);
}

TEST(PhysicsTests, RayIntersectsAabbMissesBeyondMaxDistance) {
  const std::optional<arv::physics::RayAabbHit> hit =
      arv::physics::ray_intersects_aabb(
          {0.0F, 1.5F, 1.5F},
          {1.0F, 0.0F, 0.0F},
          {{1.0F, 1.0F, 1.0F}, {2.0F, 2.0F, 2.0F}},
          0.5F);

  EXPECT_FALSE(hit.has_value());
}

TEST(PhysicsTests, RayIntersectsAabbMissesWhenParallelOutsideBounds) {
  const std::optional<arv::physics::RayAabbHit> hit =
      arv::physics::ray_intersects_aabb(
          {0.0F, 3.0F, 1.5F},
          {1.0F, 0.0F, 0.0F},
          {{1.0F, 1.0F, 1.0F}, {2.0F, 2.0F, 2.0F}},
          5.0F);

  EXPECT_FALSE(hit.has_value());
}

TEST(PhysicsTests, RayIntersectsAabbReturnsZeroDistanceFromInsideBounds) {
  const std::optional<arv::physics::RayAabbHit> hit =
      arv::physics::ray_intersects_aabb(
          {1.5F, 1.5F, 1.5F},
          {1.0F, 0.0F, 0.0F},
          {{1.0F, 1.0F, 1.0F}, {2.0F, 2.0F, 2.0F}},
          5.0F);

  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->distance, 0.0F, 0.0001F);
  EXPECT_NEAR(hit->hit_point.x, 1.5F, 0.0001F);
  EXPECT_NEAR(hit->hit_point.y, 1.5F, 0.0001F);
  EXPECT_NEAR(hit->hit_point.z, 1.5F, 0.0001F);
}

TEST(PhysicsTests, RayIntersectsAabbRejectsZeroDirection) {
  const std::optional<arv::physics::RayAabbHit> hit =
      arv::physics::ray_intersects_aabb(
          {1.5F, 1.5F, 1.5F},
          {},
          {{1.0F, 1.0F, 1.0F}, {2.0F, 2.0F, 2.0F}},
          5.0F);

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
