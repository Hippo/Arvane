#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

namespace {

arv::voxel::BlockRegistry make_lighting_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  arv::voxel::BlockDefinition glowstone =
      arv::voxel::make_full_cube_definition("arvane:glowstone");
  glowstone.properties.emits_light = true;
  glowstone.properties.emission_r = 15;
  glowstone.properties.emission_g = 10;
  glowstone.properties.emission_b = 5;
  definitions.push_back(glowstone);

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

arv::world::WorldGenerationConfig flat_generation_config(arv::i32 surface_y) {
  arv::world::WorldGenerationConfig config;
  config.seed_salt = "lighting-test";
  config.min_y = -64;
  config.max_y = 128;
  config.base_height = surface_y;
  config.height_scale = 0;
  return config;
}

void replace_flat_terrain_chunk(
    arv::world::WorldRuntime& world,
    arv::ChunkCoord coord,
    arv::voxel::BlockVariantID stone,
    arv::i32 surface_y,
    bool open_test_shaft) {
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks{};
  blocks.fill(arv::voxel::AirBlockVariantID);
  for (arv::i32 z = 0; z < arv::ChunkSize; ++z) {
    for (arv::i32 y = 0; y < arv::ChunkSize; ++y) {
      for (arv::i32 x = 0; x < arv::ChunkSize; ++x) {
        const arv::i32 world_y = coord.value.y * arv::ChunkSize + y;
        if (world_y > surface_y) {
          continue;
        }
        if (open_test_shaft && x == 1 && z == 1) {
          continue;
        }
        blocks[arv::voxel::local_index({{x, y, z}})] = stone;
      }
    }
  }
  world.ensure_chunk(coord).replace_generated_blocks(std::move(blocks));
}

bool run_lighting_worker_round(
    arv::world::WorldRuntime& world,
    const arv::voxel::BlockRegistry& registry) {
  bool processed = false;
  for (int pass = 0; pass < 64; ++pass) {
    const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();
    if (snapshot.chunks.empty()) {
      break;
    }

    arv::world::LightingResult result =
        arv::world::WorldRuntime::build_lighting_result(
            snapshot,
            registry);
    const arv::world::LightingPublishStats stats =
        world.publish_lighting_result(std::move(result));
    processed = processed || stats.accepted_chunks > 0U || stats.stale_chunks > 0U;
  }
  return processed;
}

arv::world::LightingResult build_single_lighting_result(
    arv::world::WorldRuntime& world,
    const arv::voxel::BlockRegistry& registry) {
  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();
  return arv::world::WorldRuntime::build_lighting_result(snapshot, registry);
}

} // namespace

TEST(LightingTests, SunlightPropagatesDownOpenLoadedColumns) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);

  EXPECT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, 15, 1}})), 15);
  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, 0, 1}})), 15);
}

TEST(LightingTests, MissingAboveUnderGeneratedTerrainDoesNotSeedSkyLight) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_generation_context(123U, flat_generation_config(32));
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);

  EXPECT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, 15, 1}})), 0);
  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, 0, 1}})), 0);
}

TEST(LightingTests, MissingAboveAtGeneratedSurfaceSeedsSkyLight) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_generation_context(123U, flat_generation_config(8));
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);

  EXPECT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, 15, 1}})), 15);
  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, 0, 1}})), 15);
}

TEST(LightingTests, SurfaceColumnContextSeedsLowerSectionWhenAboveLightIsStale) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_generation_context(123U, flat_generation_config(20));

  replace_flat_terrain_chunk(world, {{0, 1, 0}}, stone, 20, true);
  replace_flat_terrain_chunk(world, {{0, 0, 0}}, stone, 20, true);
  replace_flat_terrain_chunk(world, {{0, -1, 0}}, stone, 20, true);

  constexpr arv::voxel::ChunkDirtyFlags all_dirty =
      arv::voxel::ChunkDirtyFlags::NeedsSave |
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::Lighting |
      arv::voxel::ChunkDirtyFlags::LightMesh |
      arv::voxel::ChunkDirtyFlags::NeedsFluidStep;
  world.find_chunk({{0, 1, 0}})->clear_dirty(all_dirty);
  world.find_chunk({{0, 0, 0}})->clear_dirty(all_dirty);
  arv::voxel::ChunkSection* target = world.find_chunk({{0, -1, 0}});
  ASSERT_NE(target, nullptr);
  target->clear_dirty(all_dirty);
  target->mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);

  EXPECT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, -1, 1}})), 15);
  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{2, -1, 1}})), 0);
}

TEST(LightingTests, DirtyNeighborLightDoesNotSeedSectionRelight) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_generation_context(123U, flat_generation_config(32));
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& neighbor = world.ensure_chunk({{1, 0, 0}});
  ASSERT_TRUE(neighbor.set_light(
      {{0, 1, 1}},
      arv::voxel::with_sky_light(arv::voxel::NoLight, 15)));
  neighbor.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);
  target.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);

  const arv::world::LightingResult result = build_single_lighting_result(world, registry);

  ASSERT_EQ(result.chunks.size(), 1U);
  EXPECT_EQ(result.chunks.front().coord, arv::ChunkCoord({{0, 0, 0}}));
  EXPECT_EQ(
      arv::voxel::light_sky(result.chunks.front().lights[
          arv::voxel::local_index({{15, 1, 1}})]),
      0);
}

TEST(LightingTests, DirtyNeighborPreservesPreviousTargetBoundaryLightDuringRelight) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_generation_context(123U, flat_generation_config(32));
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& neighbor = world.ensure_chunk({{1, 0, 0}});
  ASSERT_TRUE(target.set_light(
      {{15, 1, 1}},
      arv::voxel::with_sky_light(arv::voxel::NoLight, 8)));
  neighbor.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);
  target.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);

  const arv::world::LightingResult result = build_single_lighting_result(world, registry);

  ASSERT_EQ(result.chunks.size(), 1U);
  EXPECT_EQ(
      arv::voxel::light_sky(result.chunks.front().lights[
          arv::voxel::local_index({{15, 1, 1}})]),
      8);
  EXPECT_GT(
      arv::voxel::light_sky(result.chunks.front().lights[
          arv::voxel::local_index({{14, 1, 1}})]),
      0);
}

TEST(LightingTests, BlockLightFallsOffFromEmitter) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID glowstone = registry.find_variant("arvane:glowstone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{1, 1, 1}}, glowstone));

  EXPECT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_r(world.get_light({{1, 1, 1}})), 15);
  EXPECT_EQ(arv::voxel::light_r(world.get_light({{2, 1, 1}})), 14);
  EXPECT_EQ(arv::voxel::light_g(world.get_light({{2, 1, 1}})), 9);
  EXPECT_EQ(arv::voxel::light_b(world.get_light({{2, 1, 1}})), 4);
}

TEST(LightingTests, LightRemovalRebuildsToDarkness) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID glowstone = registry.find_variant("arvane:glowstone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{1, 1, 1}}, glowstone));
  ASSERT_TRUE(run_lighting_worker_round(world, registry));
  ASSERT_EQ(arv::voxel::light_r(world.get_light({{2, 1, 1}})), 14);

  ASSERT_TRUE(world.set_block({{1, 1, 1}}, arv::voxel::AirBlockVariantID));
  EXPECT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_r(world.get_light({{2, 1, 1}})), 0);
}

TEST(LightingTests, OpaqueBlockPlacementBlocksLight) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID glowstone = registry.find_variant("arvane:glowstone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{1, 1, 1}}, glowstone));
  ASSERT_TRUE(run_lighting_worker_round(world, registry));
  ASSERT_EQ(arv::voxel::light_r(world.get_light({{3, 1, 1}})), 13);

  ASSERT_TRUE(world.set_block({{2, 1, 1}}, stone));
  ASSERT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_r(world.get_light({{2, 1, 1}})), 0);
  EXPECT_LT(arv::voxel::light_r(world.get_light({{3, 1, 1}})), 13);
}

TEST(LightingTests, OpaqueBlockRemovalRestoresLight) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID glowstone = registry.find_variant("arvane:glowstone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{1, 1, 1}}, glowstone));
  ASSERT_TRUE(world.set_block({{2, 1, 1}}, stone));
  ASSERT_TRUE(run_lighting_worker_round(world, registry));
  ASSERT_EQ(arv::voxel::light_r(world.get_light({{2, 1, 1}})), 0);

  ASSERT_TRUE(world.set_block({{2, 1, 1}}, arv::voxel::AirBlockVariantID));
  ASSERT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_r(world.get_light({{2, 1, 1}})), 14);
  EXPECT_EQ(arv::voxel::light_r(world.get_light({{3, 1, 1}})), 13);
}

TEST(LightingTests, SkyLightUpdatesAfterColumnBlockChanges) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);
  ASSERT_TRUE(run_lighting_worker_round(world, registry));
  ASSERT_EQ(arv::voxel::light_sky(world.get_light({{1, 14, 1}})), 15);

  ASSERT_TRUE(world.set_block({{1, 15, 1}}, stone));
  ASSERT_TRUE(run_lighting_worker_round(world, registry));
  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, 15, 1}})), 0);
  EXPECT_LT(arv::voxel::light_sky(world.get_light({{1, 14, 1}})), 15);

  ASSERT_TRUE(world.set_block({{1, 15, 1}}, arv::voxel::AirBlockVariantID));
  ASSERT_TRUE(run_lighting_worker_round(world, registry));
  EXPECT_EQ(arv::voxel::light_sky(world.get_light({{1, 14, 1}})), 15);
}

TEST(LightingTests, BlockLightCrossesLoadedChunkBorders) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID glowstone = registry.find_variant("arvane:glowstone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  world.ensure_chunk({{1, 0, 0}});
  ASSERT_TRUE(world.set_block({{15, 1, 1}}, glowstone));

  EXPECT_TRUE(run_lighting_worker_round(world, registry));

  EXPECT_EQ(arv::voxel::light_r(world.get_light({{16, 1, 1}})), 14);
}

TEST(LightingTests, SingleLightingJobPublishesOnlyOneSection) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}}).mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);
  world.ensure_chunk({{1, 0, 0}}).mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);

  arv::world::LightingResult result = build_single_lighting_result(world, registry);

  EXPECT_LE(result.chunks.size(), 1U);
  EXPECT_GE(result.stats.continuation_count, 1U);
  EXPECT_TRUE(result.continuation_requested);
}

TEST(LightingTests, MissingNeighborRecordsBoundaryDeferral) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID glowstone = registry.find_variant("arvane:glowstone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{15, 1, 1}}, glowstone));

  arv::world::LightingResult result = build_single_lighting_result(world, registry);
  EXPECT_GT(result.stats.boundary_deferrals, 0U);
  EXPECT_TRUE(result.continuation_requested);

  const arv::world::LightingPublishStats stats =
      world.publish_lighting_result(std::move(result));
  EXPECT_GT(stats.boundary_deferrals, 0U);
}

TEST(LightingTests, LightingJobStaysWithinSectionWorkBudget) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  const arv::voxel::BlockVariantID glowstone = registry.find_variant("arvane:glowstone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{8, 8, 8}}, glowstone));

  const arv::world::LightingResult result = build_single_lighting_result(world, registry);

  EXPECT_LE(result.stats.nodes_processed, arv::world::DefaultLightingWorkBudgetNodes);
  EXPECT_FALSE(result.stats.exceeded_soft_budget);
}

TEST(LightingTests, BuildResultOmitsUnchangedCleanChunks) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);
  ASSERT_TRUE(run_lighting_worker_round(world, registry));
  ASSERT_FALSE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight));

  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();
  const arv::world::LightingResult result =
      arv::world::WorldRuntime::build_lighting_result(snapshot, registry);

  EXPECT_TRUE(result.chunks.empty());
}

TEST(LightingTests, BuildResultKeepsDirtyChunksEvenWhenLightValuesMatch) {
  const arv::voxel::BlockRegistry registry = make_lighting_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);
  ASSERT_TRUE(run_lighting_worker_round(world, registry));

  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);
  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();
  arv::world::LightingResult result =
      arv::world::WorldRuntime::build_lighting_result(snapshot, registry);

  ASSERT_EQ(result.chunks.size(), 1U);
  const arv::world::LightingPublishStats stats =
      world.publish_lighting_result(std::move(result));

  EXPECT_EQ(stats.accepted_chunks, 1U);
  EXPECT_EQ(stats.changed_chunks, 0U);
  EXPECT_FALSE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight));
}
