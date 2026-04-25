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

TEST(ChunkStorageTests, SnapshotSamplesPreparedNeighborBoundaries) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  snapshot.neighbors[arv::voxel::face_index(arv::voxel::BlockFace::PositiveX)]
      .blocks[arv::voxel::boundary_index_for_face(arv::voxel::BlockFace::PositiveX, {{0, 7, 9}})] =
      arv::voxel::BlockVariantID{8};

  EXPECT_EQ(arv::voxel::snapshot_block(snapshot, {{16, 7, 9}}), arv::voxel::BlockVariantID{8});
  EXPECT_EQ(arv::voxel::snapshot_block(snapshot, {{15, 7, 9}}), arv::voxel::AirBlockVariantID);
}
