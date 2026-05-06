#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

namespace {

arv::voxel::BlockRegistry make_fluid_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

arv::voxel::FluidDefinition make_immediate_water(
    arv::voxel::FluidTypeID type = arv::voxel::DefaultWaterFluidTypeID) {
  arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition(type);
  water.flow_interval_ticks = 0U;
  return water;
}

void make_active_chunk(arv::world::WorldRuntime& world, arv::ChunkSectionCoord coord) {
  world.ensure_chunk(coord);
  ASSERT_TRUE(world.set_chunk_lifecycle_state(coord, arv::voxel::ChunkLifecycleState::Active));
}

void place_floor(
    arv::world::WorldRuntime& world,
    arv::voxel::BlockVariantID stone,
    arv::i32 min_x,
    arv::i32 max_x,
    arv::i32 z) {
  for (arv::i32 x = min_x; x <= max_x; ++x) {
    ASSERT_TRUE(world.set_block({{x, 0, z}}, stone));
  }
}

void place_boundary_channel_without_updates(
    arv::world::WorldRuntime& world,
    arv::voxel::BlockVariantID stone) {
  arv::voxel::ChunkSection* origin = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(origin, nullptr);
  ASSERT_TRUE(origin->set_block({{15, 0, 1}}, stone));
  ASSERT_TRUE(origin->set_block({{14, 1, 1}}, stone));
  ASSERT_TRUE(origin->set_block({{15, 1, 0}}, stone));
  ASSERT_TRUE(origin->set_block({{15, 1, 2}}, stone));

  arv::voxel::ChunkSection* target = world.find_chunk({{1, 0, 0}});
  ASSERT_NE(target, nullptr);
  ASSERT_TRUE(target->set_block({{0, 0, 1}}, stone));
}

void drain_fluid_updates(
    arv::world::WorldRuntime& world,
    const arv::voxel::BlockRegistry& registry,
    const arv::voxel::FluidDefinition& water) {
  for (arv::u32 step = 0; step < 64U && world.pending_fluid_update_count() != 0U; ++step) {
    (void)world.process_fluid_updates(registry, water, 512);
  }
}

} // namespace

TEST(FluidTests, LiquidFlowsDownBeforeSidewaysSpread) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  ASSERT_TRUE(world.place_fluid_source({{1, 2, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 32);

  const arv::voxel::FluidCell below = world.get_fluid({{1, 1, 1}});
  EXPECT_EQ(below.type, water.type);
  EXPECT_EQ(below.level, water.vertical_flow_level);
  EXPECT_TRUE(below.falling());
}

TEST(FluidTests, LiquidSpreadsHorizontallyWhenDownwardFlowIsBlocked) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  place_floor(world, stone, 1, 2, 1);

  ASSERT_TRUE(world.place_fluid_source({{1, 1, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 64);

  const arv::voxel::FluidCell spread = world.get_fluid({{2, 1, 1}});
  EXPECT_EQ(spread.type, water.type);
  EXPECT_EQ(spread.level, water.max_level - water.horizontal_decay);
}

TEST(FluidTests, UnsupportedFlowDecaysAfterSourceRemoval) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  place_floor(world, stone, 1, 2, 1);

  ASSERT_TRUE(world.place_fluid_source({{1, 1, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 64);
  ASSERT_FALSE(world.get_fluid({{2, 1, 1}}).empty());

  ASSERT_TRUE(world.set_fluid({{1, 1, 1}}, arv::voxel::empty_fluid_cell(), water));
  drain_fluid_updates(world, registry, water);

  const arv::voxel::FluidCell remaining = world.get_fluid({{2, 1, 1}});
  EXPECT_TRUE(remaining.empty())
      << "level=" << static_cast<int>(remaining.level)
      << " source=" << remaining.source();
}

TEST(FluidTests, SourceCellsRemainStable) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  ASSERT_TRUE(world.place_fluid_source({{1, 1, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 128);

  EXPECT_TRUE(world.get_fluid({{1, 1, 1}}).source());
}

TEST(FluidTests, LiquidCrossesChunkBordersWhenBothChunksAreActive) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  make_active_chunk(world, {{1, 0, 0}});
  place_floor(world, stone, 15, 16, 1);

  ASSERT_TRUE(world.place_fluid_source({{15, 1, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 128);

  const arv::voxel::FluidCell crossed = world.get_fluid({{16, 1, 1}});
  EXPECT_EQ(crossed.type, water.type);
  EXPECT_EQ(crossed.level, water.max_level - water.horizontal_decay);
}

TEST(FluidTests, DeferredBoundaryFluidFlowsIntoEmptyInactiveNeighborAfterActivation) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  world.ensure_chunk({{1, 0, 0}});
  place_boundary_channel_without_updates(world, stone);

  ASSERT_TRUE(world.place_fluid_source({{15, 1, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 128);
  ASSERT_TRUE(world.get_fluid({{16, 1, 1}}).empty());
  ASSERT_EQ(world.deferred_boundary_fluid_event_count({{1, 0, 0}}), 1U);

  ASSERT_TRUE(world.set_chunk_lifecycle_state({{1, 0, 0}}, arv::voxel::ChunkLifecycleState::Active));
  (void)world.process_fluid_updates(registry, water, 256);

  const arv::voxel::FluidCell crossed = world.get_fluid({{16, 1, 1}});
  EXPECT_EQ(crossed.type, water.type);
  EXPECT_EQ(crossed.level, water.max_level - water.horizontal_decay);
}

TEST(FluidTests, DeferredBoundaryFluidPreventsStarvationWhenTargetHasNoFluid) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  world.ensure_chunk({{1, 0, 0}});
  place_boundary_channel_without_updates(world, stone);

  ASSERT_TRUE(world.place_fluid_source({{15, 1, 1}}, water));
  drain_fluid_updates(world, registry, water);
  ASSERT_EQ(world.pending_fluid_update_count(), 0U);
  ASSERT_EQ(world.deferred_boundary_fluid_event_count({{1, 0, 0}}), 1U);
  ASSERT_TRUE(world.get_fluid({{16, 1, 1}}).empty());

  ASSERT_TRUE(world.set_chunk_lifecycle_state({{1, 0, 0}}, arv::voxel::ChunkLifecycleState::Active));
  ASSERT_EQ(world.pending_fluid_update_count(), 1U);
  (void)world.process_fluid_updates(registry, water, 1);

  EXPECT_FALSE(world.get_fluid({{16, 1, 1}}).empty());
}

TEST(FluidTests, DeferredBoundaryFluidEventsAreConsumedOnce) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  world.ensure_chunk({{1, 0, 0}});
  place_boundary_channel_without_updates(world, stone);

  ASSERT_TRUE(world.place_fluid_source({{15, 1, 1}}, water));
  drain_fluid_updates(world, registry, water);
  ASSERT_EQ(world.deferred_boundary_fluid_event_count({{1, 0, 0}}), 1U);
  ASSERT_TRUE(world.set_chunk_lifecycle_state({{1, 0, 0}}, arv::voxel::ChunkLifecycleState::Active));

  EXPECT_EQ(world.deferred_boundary_fluid_event_count({{1, 0, 0}}), 0U);
  EXPECT_EQ(world.pending_fluid_update_count(), 1U);

  ASSERT_TRUE(world.set_chunk_lifecycle_state({{1, 0, 0}}, arv::voxel::ChunkLifecycleState::Loaded));
  ASSERT_TRUE(world.set_chunk_lifecycle_state({{1, 0, 0}}, arv::voxel::ChunkLifecycleState::Active));

  EXPECT_EQ(world.deferred_boundary_fluid_event_count({{1, 0, 0}}), 0U);
  EXPECT_EQ(world.pending_fluid_update_count(), 1U);
}

TEST(FluidTests, RepeatedDeferredBoundaryFluidAttemptsAreDeduplicated) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  world.ensure_chunk({{1, 0, 0}});

  for (arv::u32 attempt = 0; attempt < 16U; ++attempt) {
    world.enqueue_fluid_update({{16, 1, 1}}, arv::world::FluidUpdateReason::NeighborChanged);
  }

  drain_fluid_updates(world, registry, water);

  EXPECT_EQ(world.deferred_boundary_fluid_event_count({{1, 0, 0}}), 1U);
  EXPECT_EQ(world.total_deferred_boundary_fluid_event_count(), 1U);
}

TEST(FluidTests, PendingFluidUpdatesAreDeduplicatedBeforeProcessing) {
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  for (arv::u32 attempt = 0; attempt < 16U; ++attempt) {
    world.enqueue_fluid_update({{1, 1, 1}}, arv::world::FluidUpdateReason::NeighborChanged);
  }

  EXPECT_EQ(world.pending_fluid_update_count(), 1U);
  (void)water;
}

TEST(FluidTests, SnapshotFluidSimulationBuildsOffThreadApplyResult) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  ASSERT_TRUE(world.place_fluid_source({{1, 2, 1}}, water));
  std::optional<arv::world::FluidSimulationRequest> request =
      world.make_fluid_simulation_request(64);
  ASSERT_TRUE(request.has_value());

  arv::world::FluidSimulationResult result =
      arv::world::FluidSolver::build_simulation_result(
          std::move(request.value()),
          registry,
          water);
  EXPECT_TRUE(world.get_fluid({{1, 1, 1}}).empty());
  EXPECT_GT(result.processed, 0U);

  (void)world.apply_fluid_simulation_result(std::move(result), water);

  const arv::voxel::FluidCell below = world.get_fluid({{1, 1, 1}});
  EXPECT_EQ(below.type, water.type);
  EXPECT_TRUE(below.falling());
}

TEST(FluidTests, SnapshotFluidSimulationDoesNotCascadeWithinSingleRequest) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  ASSERT_TRUE(world.place_fluid_source({{1, 4, 1}}, water));
  std::optional<arv::world::FluidSimulationRequest> request =
      world.make_fluid_simulation_request(512);
  ASSERT_TRUE(request.has_value());

  arv::world::FluidSimulationResult result =
      arv::world::FluidSolver::build_simulation_result(
          std::move(request.value()),
          registry,
          water);
  (void)world.apply_fluid_simulation_result(std::move(result), water);

  EXPECT_FALSE(world.get_fluid({{1, 3, 1}}).empty());
  EXPECT_TRUE(world.get_fluid({{1, 2, 1}}).empty());

  request = world.make_fluid_simulation_request(512);
  ASSERT_TRUE(request.has_value());
  result = arv::world::FluidSolver::build_simulation_result(
      std::move(request.value()),
      registry,
      water);
  (void)world.apply_fluid_simulation_result(std::move(result), water);

  EXPECT_FALSE(world.get_fluid({{1, 2, 1}}).empty());
}

TEST(FluidTests, FlowIntervalDelaysPropagationUntilScheduledTick) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  water.flow_interval_ticks = 4U;
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  ASSERT_TRUE(world.place_fluid_source({{1, 2, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 128);
  EXPECT_TRUE(world.get_fluid({{1, 1, 1}}).empty());

  world.set_tick_index(3U);
  (void)world.process_fluid_updates(registry, water, 128);
  EXPECT_TRUE(world.get_fluid({{1, 1, 1}}).empty());

  world.set_tick_index(4U);
  (void)world.process_fluid_updates(registry, water, 128);
  EXPECT_FALSE(world.get_fluid({{1, 1, 1}}).empty());
}

TEST(FluidTests, FluidDefinitionsCanUseDifferentFlowIntervals) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  water.flow_interval_ticks = 2U;
  arv::voxel::FluidDefinition lava =
      arv::voxel::make_water_fluid_definition(arv::voxel::FluidTypeID{2});
  lava.flow_interval_ticks = 6U;
  arv::world::WorldRuntime water_world{arv::WorldID{1}};
  arv::world::WorldRuntime lava_world{arv::WorldID{2}};
  make_active_chunk(water_world, {{0, 0, 0}});
  make_active_chunk(lava_world, {{0, 0, 0}});

  ASSERT_TRUE(water_world.place_fluid_source({{1, 2, 1}}, water));
  ASSERT_TRUE(lava_world.place_fluid_source({{1, 2, 1}}, lava));

  water_world.set_tick_index(2U);
  lava_world.set_tick_index(2U);
  (void)water_world.process_fluid_updates(registry, water, 128);
  (void)lava_world.process_fluid_updates(registry, lava, 128);

  EXPECT_FALSE(water_world.get_fluid({{1, 1, 1}}).empty());
  EXPECT_TRUE(lava_world.get_fluid({{1, 1, 1}}).empty());

  lava_world.set_tick_index(6U);
  (void)lava_world.process_fluid_updates(registry, lava, 128);
  EXPECT_FALSE(lava_world.get_fluid({{1, 1, 1}}).empty());
}

TEST(FluidTests, FasterFluidStillPropagatesOneCellPerUpdate) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  water.flow_interval_ticks = 1U;
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  ASSERT_TRUE(world.place_fluid_source({{1, 4, 1}}, water));
  world.set_tick_index(1U);
  (void)world.process_fluid_updates(registry, water, 512);

  EXPECT_FALSE(world.get_fluid({{1, 3, 1}}).empty());
  EXPECT_TRUE(world.get_fluid({{1, 2, 1}}).empty());
}

TEST(FluidTests, DuplicateScheduledUpdatesCoalesceAndStaleEventsAreIgnored) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  for (arv::u32 attempt = 0; attempt < 16U; ++attempt) {
    world.enqueue_fluid_update(
        {{1, 1, 1}},
        arv::world::FluidUpdateReason::NeighborChanged,
        10U);
  }
  EXPECT_EQ(world.pending_fluid_update_count(), 1U);

  world.enqueue_fluid_update(
      {{1, 1, 1}},
      arv::world::FluidUpdateReason::FluidRemoved,
      2U);
  EXPECT_EQ(world.pending_fluid_update_count(), 1U);

  world.set_tick_index(2U);
  EXPECT_EQ(world.process_fluid_updates(registry, water, 128), 1U);
  EXPECT_EQ(world.pending_fluid_update_count(), 0U);

  world.set_tick_index(10U);
  EXPECT_EQ(world.process_fluid_updates(registry, water, 128), 0U);
  EXPECT_EQ(world.pending_fluid_update_count(), 0U);
}

TEST(FluidTests, FluidChangesDirtyChunkButDoNotSynchronouslyMeshOrLight) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  water.flow_interval_ticks = 1U;
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_chunk_lifecycle_state({{0, 0, 0}}, arv::voxel::ChunkLifecycleState::Active));

  ASSERT_TRUE(world.place_fluid_source({{1, 2, 1}}, water));
  chunk.clear_dirty(
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::Lighting |
      arv::voxel::ChunkDirtyFlags::LightMesh);
  world.set_tick_index(1U);
  (void)world.process_fluid_updates(registry, water, 128);

  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_EQ(chunk.mesh_state(), arv::voxel::ChunkMeshState::Dirty);
  EXPECT_TRUE(world.has_lighting_work());
}

TEST(FluidTests, SnapshotFluidRemovalDoesNotCascadeWithinSingleRequest) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  place_floor(world, stone, 1, 5, 1);

  ASSERT_TRUE(world.place_fluid_source({{1, 1, 1}}, water));
  drain_fluid_updates(world, registry, water);
  ASSERT_FALSE(world.get_fluid({{4, 1, 1}}).empty());

  ASSERT_TRUE(world.set_fluid({{1, 1, 1}}, arv::voxel::empty_fluid_cell(), water));
  std::optional<arv::world::FluidSimulationRequest> request =
      world.make_fluid_simulation_request(512);
  ASSERT_TRUE(request.has_value());
  arv::world::FluidSimulationResult result =
      arv::world::FluidSolver::build_simulation_result(
          std::move(request.value()),
          registry,
          water);
  (void)world.apply_fluid_simulation_result(std::move(result), water);

  EXPECT_FALSE(world.get_fluid({{4, 1, 1}}).empty());
}

TEST(FluidTests, FluidRemovalCleansLoadedInactiveNeighborAcrossChunkBorder) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = make_immediate_water();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  make_active_chunk(world, {{1, 0, 0}});
  place_floor(world, stone, 15, 18, 1);

  ASSERT_TRUE(world.place_fluid_source({{15, 1, 1}}, water));
  drain_fluid_updates(world, registry, water);
  ASSERT_FALSE(world.get_fluid({{16, 1, 1}}).empty());
  ASSERT_TRUE(world.set_chunk_lifecycle_state({{1, 0, 0}}, arv::voxel::ChunkLifecycleState::Loaded));

  ASSERT_TRUE(world.set_fluid({{15, 1, 1}}, arv::voxel::empty_fluid_cell(), water));
  for (arv::u32 step = 0; step < 64U && !world.get_fluid({{16, 1, 1}}).empty(); ++step) {
    (void)world.process_fluid_updates(registry, water, 512);
  }

  EXPECT_TRUE(world.get_fluid({{16, 1, 1}}).empty());
  ASSERT_TRUE(world.set_chunk_lifecycle_state({{1, 0, 0}}, arv::voxel::ChunkLifecycleState::Active));
  for (arv::u32 step = 0; step < 16U && world.pending_fluid_update_count() != 0U; ++step) {
    (void)world.process_fluid_updates(registry, water, 512);
  }
  EXPECT_TRUE(world.get_fluid({{16, 1, 1}}).empty());
}

TEST(FluidTests, SimulationRadiusSeparatesLoadedAndActiveChunks) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  world.ensure_chunk({{2, 0, 0}});

  world.update_simulation_radius({{0, 0, 0}}, 1);

  EXPECT_TRUE(world.is_chunk_active({{0, 0, 0}}));
  EXPECT_FALSE(world.is_chunk_active({{2, 0, 0}}));
  EXPECT_EQ(world.loaded_chunk_count(), 2U);
  EXPECT_EQ(world.active_chunk_count(), 1U);
}

TEST(FluidTests, SimulationRadiusDoesNotMaterializePendingChunks) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.request_chunk_load({{0, 0, 0}});

  world.update_simulation_radius({{0, 0, 0}}, 1);

  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->lifecycle_state(), arv::voxel::ChunkLifecycleState::Requested);
}
