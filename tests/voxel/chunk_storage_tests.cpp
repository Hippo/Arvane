#include "arv/voxel/storage.hpp"

#include <gtest/gtest.h>

#include <memory>

TEST(ChunkStorageTests, LocalIndexUsesCanonicalChunkSectionLayout) {
  EXPECT_EQ(arv::voxel::local_index({{0, 0, 0}}), 0U);
  EXPECT_EQ(arv::voxel::local_index({{1, 0, 0}}), 1U);
  EXPECT_EQ(arv::voxel::local_index({{0, 1, 0}}), 16U);
  EXPECT_EQ(arv::voxel::local_index({{0, 0, 1}}), 256U);
  EXPECT_EQ(arv::voxel::local_index({{15, 15, 15}}), arv::voxel::ChunkVolume - 1U);
}

TEST(ChunkStorageTests, LocalColumnIndexUsesCanonicalChunkColumnLayout) {
  EXPECT_EQ(arv::voxel::local_column_index({0, 0}), 0U);
  EXPECT_EQ(arv::voxel::local_column_index({1, 0}), 1U);
  EXPECT_EQ(arv::voxel::local_column_index({0, 1}), 16U);
  EXPECT_EQ(arv::voxel::local_column_index({15, 15}), arv::voxel::ChunkArea - 1U);
}

TEST(ChunkStorageTests, LocalPosUsesCanonicalChunkSectionLayout) {
  EXPECT_EQ(arv::voxel::local_pos(0U).value, (arv::Vec3i{0, 0, 0}));
  EXPECT_EQ(arv::voxel::local_pos(1U).value, (arv::Vec3i{1, 0, 0}));
  EXPECT_EQ(arv::voxel::local_pos(16U).value, (arv::Vec3i{0, 1, 0}));
  EXPECT_EQ(arv::voxel::local_pos(256U).value, (arv::Vec3i{0, 0, 1}));
  EXPECT_EQ(
      arv::voxel::local_pos(arv::voxel::ChunkVolume - 1U).value,
      (arv::Vec3i{15, 15, 15}));
}

TEST(ChunkStorageTests, LocalIndexAndLocalPosRoundTrip) {
  for (arv::usize index = 0; index < arv::voxel::ChunkVolume; ++index) {
    const arv::SectionLocalPos local = arv::voxel::local_pos(index);
    ASSERT_TRUE(arv::voxel::is_valid_local(local));
    EXPECT_EQ(arv::voxel::local_index(local), index);
  }
}

TEST(ChunkStorageTests, LocalColumnIndexAndLocalColumnRoundTrip) {
  for (arv::usize index = 0; index < arv::voxel::ChunkArea; ++index) {
    const arv::ChunkLocalColumn local = arv::voxel::local_column(index);
    EXPECT_GE(local.x, 0);
    EXPECT_LT(local.x, arv::ChunkSize);
    EXPECT_GE(local.z, 0);
    EXPECT_LT(local.z, arv::ChunkSize);
    EXPECT_EQ(arv::voxel::local_column_index(local), index);
  }
}

TEST(ChunkStorageTests, SnapshotStorageIndexAndOffsetRoundTrip) {
  for (arv::usize index = 0; index < arv::voxel::ChunkSnapshotNeighborhoodVolume; ++index) {
    const arv::Vec3i offset = arv::voxel::snapshot_storage_offset(index);
    EXPECT_GE(offset.x, -1);
    EXPECT_LE(offset.x, 1);
    EXPECT_GE(offset.y, -1);
    EXPECT_LE(offset.y, 1);
    EXPECT_GE(offset.z, -1);
    EXPECT_LE(offset.z, 1);
    EXPECT_EQ(arv::voxel::snapshot_storage_index(offset.x, offset.y, offset.z), index);
  }
}

TEST(ChunkStorageTests, ChunksDefaultToAirStorage) {
  const arv::voxel::ChunkSection chunk{{{0, 0, 0}}};

  EXPECT_EQ(chunk.block({{0, 0, 0}}), arv::voxel::AirBlockVariantID);
  EXPECT_EQ(chunk.block({{15, 15, 15}}), arv::voxel::AirBlockVariantID);
}

TEST(ChunkStorageTests, SetBlockIncrementsVersionAndMarksDirty) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  const arv::u64 initial_version = chunk.version();
  const arv::u64 initial_light_version = chunk.light_version();
  chunk.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry);

  EXPECT_TRUE(chunk.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{4}));
  EXPECT_EQ(chunk.block({{1, 2, 3}}), arv::voxel::BlockVariantID{4});
  EXPECT_EQ(chunk.version(), initial_version + 1);
  EXPECT_EQ(chunk.light_version(), initial_light_version);
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::NeedsSave));

  EXPECT_FALSE(chunk.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{4}));
  EXPECT_EQ(chunk.version(), initial_version + 1);
}

TEST(ChunkStorageTests, SetLightIncrementsLightVersionAndMarksLightMeshOnly) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  const arv::u64 block_version = chunk.block_version();
  const arv::u64 light_version = chunk.light_version();
  const arv::u64 light_data_version = chunk.light_data_version();
  chunk.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::LightMesh);

  ASSERT_TRUE(chunk.set_light({{1, 2, 3}}, arv::voxel::pack_light(1, 2, 3, 4)));

  EXPECT_EQ(chunk.block_version(), block_version);
  EXPECT_EQ(chunk.light_version(), light_version + 1);
  EXPECT_EQ(chunk.light_data_version(), light_data_version + 1);
  EXPECT_FALSE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
}

TEST(ChunkStorageTests, LightDependencyDirtyDoesNotChangeSerializedLightDataVersion) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  const arv::u64 light_version = chunk.light_version();
  const arv::u64 light_data_version = chunk.light_data_version();
  chunk.clear_dirty(arv::voxel::ChunkDirtyFlags::LightMesh);

  chunk.mark_light_dependency_dirty();

  EXPECT_EQ(chunk.light_version(), light_version + 1);
  EXPECT_EQ(chunk.light_data_version(), light_data_version);
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
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

TEST(ChunkStorageTests, MutatingLightWithActiveSnapshotDetachesAndPreservesSnapshotView) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_light({{1, 2, 3}}, arv::voxel::pack_light(1, 2, 3, 4)));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::voxel::ChunkStorage* snapshot_storage = snapshot.center_storage_identity();
  const arv::u64 light_version = chunk.light_version();

  ASSERT_TRUE(chunk.set_light({{1, 2, 3}}, arv::voxel::pack_light(4, 3, 2, 1)));

  EXPECT_EQ(snapshot.center_storage_identity(), snapshot_storage);
  EXPECT_NE(chunk.storage_identity(), snapshot_storage);
  EXPECT_EQ(
      arv::voxel::snapshot_light(snapshot, {{1, 2, 3}}),
      arv::voxel::pack_light(1, 2, 3, 4));
  EXPECT_EQ(chunk.light({{1, 2, 3}}), arv::voxel::pack_light(4, 3, 2, 1));
  EXPECT_EQ(chunk.light_version(), light_version + 1);
}

TEST(ChunkStorageTests, MutatingFluidWithActiveSnapshotDetachesAndPreservesSnapshotView) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  ASSERT_TRUE(chunk.set_fluid({{1, 2, 3}}, arv::voxel::make_fluid_source(water)));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::voxel::ChunkStorage* snapshot_storage = snapshot.center_storage_identity();
  const arv::u64 version = chunk.version();

  ASSERT_TRUE(chunk.set_fluid(
      {{1, 2, 3}},
      arv::voxel::make_flowing_fluid(water, 4)));

  EXPECT_EQ(snapshot.center_storage_identity(), snapshot_storage);
  EXPECT_NE(chunk.storage_identity(), snapshot_storage);
  EXPECT_EQ(
      arv::voxel::snapshot_fluid(snapshot, {{1, 2, 3}}),
      arv::voxel::make_fluid_source(water));
  EXPECT_EQ(
      chunk.fluid({{1, 2, 3}}),
      arv::voxel::make_flowing_fluid(water, 4));
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

TEST(ChunkStorageTests, SnapshotMissingStorageSamplesAsEmpty) {
  arv::voxel::ChunkSnapshot snapshot;

  EXPECT_EQ(
      arv::voxel::snapshot_block(snapshot, {{0, 0, 0}}),
      arv::voxel::AirBlockVariantID);
  EXPECT_EQ(
      arv::voxel::snapshot_light(snapshot, {{0, 0, 0}}),
      arv::voxel::NoLight);
  EXPECT_EQ(
      arv::voxel::snapshot_fluid(snapshot, {{0, 0, 0}}),
      arv::voxel::empty_fluid_cell());
}

TEST(ChunkStorageTests, SnapshotSkyLightOverlayAppliesOnlyToOpenAirCells) {
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 3, 1}}, arv::voxel::BlockVariantID{4}));
  ASSERT_TRUE(chunk.set_light({{1, 2, 1}}, arv::voxel::pack_light(1, 2, 3, 4)));

  arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  auto overlay = std::make_shared<arv::voxel::ChunkSnapshotSkyLightOverlay>();
  overlay->min_open_sky_y[arv::voxel::local_column_index({1, 1})] = 2U;
  snapshot.sky_light_overlays[arv::voxel::ChunkSnapshotCenterStorageIndex] = overlay;

  EXPECT_EQ(
      arv::voxel::snapshot_light(snapshot, {{1, 1, 1}}),
      arv::voxel::NoLight);
  EXPECT_EQ(
      arv::voxel::snapshot_light(snapshot, {{1, 2, 1}}),
      arv::voxel::pack_light(1, 2, 3, arv::voxel::MaxLightLevel));
  EXPECT_EQ(
      arv::voxel::snapshot_light(snapshot, {{1, 3, 1}}),
      arv::voxel::NoLight);
}
