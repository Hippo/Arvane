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
