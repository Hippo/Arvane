#include "arv/voxel/storage.hpp"

#include <gtest/gtest.h>

TEST(ChunkStorageTests, LocalIndexUsesCanonicalChunkSectionLayout) {
  EXPECT_EQ(arv::voxel::local_index({{0, 0, 0}}), 0U);
  EXPECT_EQ(arv::voxel::local_index({{1, 0, 0}}), 1U);
  EXPECT_EQ(arv::voxel::local_index({{0, 1, 0}}), 16U);
  EXPECT_EQ(arv::voxel::local_index({{0, 0, 1}}), 256U);
  EXPECT_EQ(arv::voxel::local_index({{15, 15, 15}}), arv::voxel::ChunkVolume - 1U);
}

TEST(ChunkStorageTests, ChunksDefaultToAirStorage) {
  const arv::voxel::ChunkSection chunk{{{0, 0, 0}}};

  EXPECT_EQ(chunk.block({{0, 0, 0}}), arv::voxel::AirBlockVariantID);
  EXPECT_EQ(chunk.block({{15, 15, 15}}), arv::voxel::AirBlockVariantID);
}

TEST(ChunkStorageTests, SetBlockIncrementsVersionAndMarksDirty) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  const arv::u64 initial_version = chunk.version();
  chunk.clear_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh);

  EXPECT_TRUE(chunk.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{4}));
  EXPECT_EQ(chunk.block({{1, 2, 3}}), arv::voxel::BlockVariantID{4});
  EXPECT_EQ(chunk.version(), initial_version + 1);
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsSave));

  EXPECT_FALSE(chunk.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{4}));
  EXPECT_EQ(chunk.version(), initial_version + 1);
}

TEST(ChunkStorageTests, SnapshotCreationSharesChunkStorageWithoutCopying) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{4}));

  const arv::voxel::ChunkStorage* storage = chunk.storage_identity();
  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);

  EXPECT_EQ(snapshot.center_storage_identity(), storage);
  EXPECT_EQ(arv::voxel::snapshot_block(snapshot, {{1, 2, 3}}), arv::voxel::BlockVariantID{4});
}

TEST(ChunkStorageTests, MutatingChunkWithoutActiveSnapshotReusesStorage) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};

  const arv::voxel::ChunkStorage* storage = chunk.storage_identity();
  const arv::u64 version = chunk.version();
  ASSERT_TRUE(chunk.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{4}));

  EXPECT_EQ(chunk.storage_identity(), storage);
  EXPECT_EQ(chunk.version(), version + 1);
}

TEST(ChunkStorageTests, MutatingChunkWithActiveSnapshotDetachesAndPreservesSnapshotView) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{4}));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::voxel::ChunkStorage* snapshot_storage = snapshot.center_storage_identity();
  const arv::u64 version = chunk.version();

  ASSERT_TRUE(chunk.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{9}));

  EXPECT_EQ(snapshot.center_storage_identity(), snapshot_storage);
  EXPECT_NE(chunk.storage_identity(), snapshot_storage);
  EXPECT_EQ(arv::voxel::snapshot_block(snapshot, {{1, 2, 3}}), arv::voxel::BlockVariantID{4});
  EXPECT_EQ(chunk.block({{1, 2, 3}}), arv::voxel::BlockVariantID{9});
  EXPECT_EQ(chunk.version(), version + 1);
}

TEST(ChunkStorageTests, SnapshotSamplesPreparedNeighborBoundaries) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  arv::voxel::ChunkSection neighbor{{{1, 0, 0}}};
  ASSERT_TRUE(neighbor.set_block({{0, 7, 9}}, arv::voxel::BlockVariantID{8}));
  arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  snapshot.storages[arv::voxel::snapshot_storage_index(1, 0, 0)] = neighbor.storage_view();

  EXPECT_EQ(arv::voxel::snapshot_block(snapshot, {{16, 7, 9}}), arv::voxel::BlockVariantID{8});
  EXPECT_EQ(arv::voxel::snapshot_block(snapshot, {{15, 7, 9}}), arv::voxel::AirBlockVariantID);
}
