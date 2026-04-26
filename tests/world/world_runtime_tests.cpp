#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

TEST(WorldRuntimeTests, SetAndGetBlockUseFloorBasedNegativeCoordinates) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk(arv::block_to_chunk({{-1, 0, 0}}));

  EXPECT_TRUE(world.set_block({{-1, 0, 0}}, arv::voxel::BlockVariantID{2}));
  EXPECT_EQ(world.get_block({{-1, 0, 0}}), arv::voxel::BlockVariantID{2});

  const arv::voxel::ChunkSection* chunk = world.find_chunk({{-1, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->block({{15, 0, 0}}), arv::voxel::BlockVariantID{2});
}

TEST(WorldRuntimeTests, BoundaryBlockChangeInvalidatesLoadedNeighborMeshVersion) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& neighbor = world.ensure_chunk({{1, 0, 0}});
  target.clear_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh);
  neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh);
  const arv::u64 neighbor_version = neighbor.version();

  EXPECT_TRUE(world.set_block({{15, 2, 3}}, arv::voxel::BlockVariantID{3}));

  EXPECT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));
  EXPECT_TRUE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));
  EXPECT_EQ(neighbor.version(), neighbor_version + 1);
}

TEST(WorldRuntimeTests, MeshingSnapshotCopiesAvailableNeighborBoundary) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  world.ensure_chunk({{1, 0, 0}});

  EXPECT_TRUE(world.set_block({{16, 4, 5}}, arv::voxel::BlockVariantID{7}));
  const auto snapshot = world.make_meshing_snapshot(
      {{0, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);

  ASSERT_TRUE(snapshot);
  EXPECT_EQ(arv::voxel::snapshot_block(snapshot.value(), {{16, 4, 5}}), arv::voxel::BlockVariantID{7});
}

TEST(WorldRuntimeTests, MeshingSnapshotCopiesAvailableNeighborBoundaryLight) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& neighbor = world.ensure_chunk({{1, 0, 0}});

  ASSERT_TRUE(neighbor.set_light({{0, 4, 5}}, arv::voxel::pack_light(3, 4, 5, 6)));
  const auto snapshot = world.make_meshing_snapshot(
      {{0, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);

  ASSERT_TRUE(snapshot);
  EXPECT_EQ(arv::voxel::snapshot_light(snapshot.value(), {{16, 4, 5}}), arv::voxel::pack_light(3, 4, 5, 6));
}

TEST(WorldRuntimeTests, LightingSnapshotSharesChunkStorage) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});

  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();

  ASSERT_EQ(snapshot.chunks.size(), 1U);
  EXPECT_EQ(snapshot.chunks[0].coord, arv::ChunkCoord({{0, 0, 0}}));
  EXPECT_EQ(snapshot.chunks[0].storage.get(), chunk.storage_identity());
}

TEST(WorldRuntimeTests, PublishingGeneratedChunkInvalidatesDirectNeighborsForRelightAndRemesh) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& neighbor = world.ensure_chunk({{0, 0, 0}});
  neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh | arv::voxel::ChunkDirtyFlags::NeedsRelight);
  const arv::u64 neighbor_version = neighbor.version();

  arv::world::GeneratedChunk generated;
  generated.world_id = arv::WorldID{1};
  generated.coord = {{1, 0, 0}};
  generated.blocks.fill(arv::voxel::AirBlockVariantID);

  EXPECT_TRUE(world.publish_generated_chunk(std::move(generated)));

  EXPECT_TRUE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));
  EXPECT_TRUE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight));
  EXPECT_EQ(neighbor.version(), neighbor_version + 1);
}

TEST(WorldRuntimeTests, PublishingGeneratedChunkInvalidatesDiagonalNeighborsForRemesh) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& diagonal = world.ensure_chunk({{0, 0, 0}});
  diagonal.clear_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh | arv::voxel::ChunkDirtyFlags::NeedsRelight);
  const arv::u64 diagonal_version = diagonal.version();

  arv::world::GeneratedChunk generated;
  generated.world_id = arv::WorldID{1};
  generated.coord = {{1, 1, 1}};
  generated.blocks.fill(arv::voxel::AirBlockVariantID);

  EXPECT_TRUE(world.publish_generated_chunk(std::move(generated)));

  EXPECT_TRUE(diagonal.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));
  EXPECT_FALSE(diagonal.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight));
  EXPECT_EQ(diagonal.version(), diagonal_version + 1);
}

TEST(WorldRuntimeTests, LightingPublicationInvalidatesDirectNeighborMeshesWhenBoundaryLightChanges) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& lit_chunk = world.ensure_chunk({{1, 0, 0}});
  target.clear_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh | arv::voxel::ChunkDirtyFlags::NeedsRelight);
  lit_chunk.clear_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh | arv::voxel::ChunkDirtyFlags::NeedsRelight);
  const arv::u64 target_version = target.version();

  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = lit_chunk.lights();
  lights[arv::voxel::local_index({{0, 4, 5}})] = arv::voxel::pack_light(7, 6, 5, 4);

  arv::world::LightingResult result;
  result.world_id = arv::WorldID{1};
  result.chunks.push_back({{{1, 0, 0}}, lit_chunk.version(), lights});

  const arv::world::LightingPublishStats stats = world.publish_lighting_result(std::move(result));

  EXPECT_EQ(stats.accepted_chunks, 1U);
  EXPECT_EQ(stats.changed_chunks, 1U);
  EXPECT_TRUE(lit_chunk.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));
  EXPECT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));
  EXPECT_FALSE(target.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight));
  EXPECT_EQ(target.version(), target_version + 1);
}

TEST(WorldRuntimeTests, RejectsStaleMeshPublicationAndAcceptsCurrentVersion) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  const arv::u64 stale_version = chunk.version();

  EXPECT_TRUE(world.set_block({{1, 1, 1}}, arv::voxel::BlockVariantID{2}));
  EXPECT_FALSE(world.publish_mesh_result({{0, 0, 0}}, stale_version));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));

  EXPECT_TRUE(world.publish_mesh_result({{0, 0, 0}}, chunk.version()));
  EXPECT_FALSE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));
  EXPECT_EQ(chunk.mesh_state(), arv::voxel::ChunkMeshState::Ready);
}
