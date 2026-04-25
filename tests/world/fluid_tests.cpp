#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

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

void make_active_chunk(arv::world::WorldRuntime& world, arv::ChunkCoord coord) {
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
  for (arv::u32 step = 0; step < 8U && world.pending_fluid_update_count() != 0U; ++step) {
    (void)world.process_fluid_updates(registry, water, 512);
  }
}

} // namespace

TEST(FluidTests, LiquidFlowsDownBeforeSidewaysSpread) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
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
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
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
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});
  place_floor(world, stone, 1, 2, 1);

  ASSERT_TRUE(world.place_fluid_source({{1, 1, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 64);
  ASSERT_FALSE(world.get_fluid({{2, 1, 1}}).empty());

  ASSERT_TRUE(world.set_fluid({{1, 1, 1}}, arv::voxel::empty_fluid_cell(), water));
  (void)world.process_fluid_updates(registry, water, 128);

  EXPECT_TRUE(world.get_fluid({{2, 1, 1}}).empty());
}

TEST(FluidTests, SourceCellsRemainStable) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  make_active_chunk(world, {{0, 0, 0}});

  ASSERT_TRUE(world.place_fluid_source({{1, 1, 1}}, water));
  (void)world.process_fluid_updates(registry, water, 128);

  EXPECT_TRUE(world.get_fluid({{1, 1, 1}}).source());
}

TEST(FluidTests, LiquidCrossesChunkBordersWhenBothChunksAreActive) {
  const arv::voxel::BlockRegistry registry = make_fluid_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
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
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
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
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
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
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
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
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
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
