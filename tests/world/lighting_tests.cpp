#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

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

bool run_lighting_worker_round(
    arv::world::WorldRuntime& world,
    const arv::voxel::BlockRegistry& registry) {
  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();
  if (snapshot.chunks.empty()) {
    return false;
  }

  arv::world::LightingResult result =
      arv::world::WorldRuntime::build_lighting_result(
          snapshot,
          registry);
  const arv::world::LightingPublishStats stats =
      world.publish_lighting_result(std::move(result));
  return stats.accepted_chunks > 0U || stats.stale_chunks > 0U;
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
