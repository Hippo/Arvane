#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <vector>

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
  target.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry);
  neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry);
  const arv::u64 neighbor_version = neighbor.version();

  EXPECT_TRUE(world.set_block({{15, 2, 3}}, arv::voxel::BlockVariantID{3}));

  EXPECT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_EQ(neighbor.version(), neighbor_version + 1);
}

TEST(WorldRuntimeTests, BlockEditQueuesGeometryAndLightingSeparately) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.clear_dirty(
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::Lighting |
      arv::voxel::ChunkDirtyFlags::LightMesh);

  ASSERT_TRUE(world.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{7}));

  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_FALSE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_TRUE(world.has_lighting_work());
  EXPECT_TRUE(world.make_meshing_snapshot(
                       {{0, 0, 0}},
                       arv::world::MissingNeighborPolicy::TreatMissingAsAir)
                  .has_value());
}

TEST(WorldRuntimeTests, InteriorBlockEditDoesNotDirtyEveryLoadedSection) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& neighbor = world.ensure_chunk({{1, 0, 0}});
  target.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry);
  neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry);

  ASSERT_TRUE(world.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{7}));

  EXPECT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_FALSE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
}

TEST(WorldRuntimeTests, BoundaryBlockEditDirtiesOnlyRequiredFaceNeighbor) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& x_neighbor = world.ensure_chunk({{1, 0, 0}});
  arv::voxel::ChunkSection& z_neighbor = world.ensure_chunk({{0, 0, 1}});
  target.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry);
  x_neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry);
  z_neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry);

  ASSERT_TRUE(world.set_block({{15, 2, 3}}, arv::voxel::BlockVariantID{7}));

  EXPECT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(x_neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_FALSE(z_neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
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

TEST(WorldRuntimeTests, LightOnlyChangesKeepMeshingContentVersionValid) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  const auto snapshot = world.make_meshing_snapshot(
      {{0, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);

  ASSERT_TRUE(chunk.set_light({{1, 2, 3}}, arv::voxel::pack_light(3, 4, 5, 6)));

  EXPECT_TRUE(world.validate_mesh_result({{0, 0, 0}}, snapshot->source_version));
  EXPECT_TRUE(world.validate_mesh_content_result({{0, 0, 0}}, snapshot->source_content_version));
  EXPECT_NE(chunk.light_version(), snapshot->source_light_version);
}

TEST(WorldRuntimeTests, BoundaryBlockChangesInvalidateNeighborMeshingContentVersion) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  world.ensure_chunk({{1, 0, 0}});
  const auto snapshot = world.make_meshing_snapshot(
      {{1, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);

  EXPECT_TRUE(world.set_block({{15, 2, 3}}, arv::voxel::BlockVariantID{3}));

  EXPECT_FALSE(world.validate_mesh_content_result({{1, 0, 0}}, snapshot->source_content_version));
}

TEST(WorldRuntimeTests, LightingSnapshotSharesChunkStorage) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);

  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();

  ASSERT_EQ(snapshot.chunks.size(), 1U);
  EXPECT_EQ(snapshot.chunks[0].coord, arv::ChunkCoord({{0, 0, 0}}));
  EXPECT_EQ(snapshot.chunks[0].storage.get(), chunk.storage_identity());
}

TEST(WorldRuntimeTests, PublishingGeneratedChunkInvalidatesDirectNeighborsForRelightAndRemesh) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& neighbor = world.ensure_chunk({{0, 0, 0}});
  neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::Lighting);
  const arv::u64 neighbor_version = neighbor.version();

  arv::world::GeneratedChunk generated;
  generated.world_id = arv::WorldID{1};
  generated.coord = {{1, 0, 0}};
  generated.blocks.fill(arv::voxel::AirBlockVariantID);

  EXPECT_TRUE(world.publish_generated_chunk(std::move(generated)));

  EXPECT_TRUE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_EQ(neighbor.version(), neighbor_version + 1);
}

TEST(WorldRuntimeTests, PublishingGeneratedChunkInvalidatesDiagonalNeighborsForRemesh) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& diagonal = world.ensure_chunk({{0, 0, 0}});
  diagonal.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::Lighting);
  const arv::u64 diagonal_version = diagonal.version();

  arv::world::GeneratedChunk generated;
  generated.world_id = arv::WorldID{1};
  generated.coord = {{1, 1, 1}};
  generated.blocks.fill(arv::voxel::AirBlockVariantID);

  EXPECT_TRUE(world.publish_generated_chunk(std::move(generated)));

  EXPECT_TRUE(diagonal.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_FALSE(diagonal.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_EQ(diagonal.version(), diagonal_version + 1);
}

TEST(WorldRuntimeTests, LightingPublicationInvalidatesDirectNeighborMeshesWhenBoundaryLightChanges) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& lit_chunk = world.ensure_chunk({{1, 0, 0}});
  target.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::Lighting);
  lit_chunk.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::Lighting);
  const arv::u64 target_light_version = target.light_version();

  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = lit_chunk.lights();
  lights[arv::voxel::local_index({{0, 4, 5}})] = arv::voxel::pack_light(7, 6, 5, 4);
  const arv::u8 negative_x_boundary =
      static_cast<arv::u8>(1U << arv::voxel::face_index(arv::voxel::BlockFace::NegativeX));

  arv::world::LightingResult result;
  result.world_id = arv::WorldID{1};
  result.chunks.push_back({{{1, 0, 0}}, lit_chunk.version(), lights, 0U, negative_x_boundary});

  const arv::world::LightingPublishStats stats = world.publish_lighting_result(std::move(result));

  EXPECT_EQ(stats.accepted_chunks, 1U);
  EXPECT_EQ(stats.changed_chunks, 1U);
  EXPECT_EQ(stats.boundary_continuations, 1U);
  EXPECT_TRUE(lit_chunk.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_EQ(target.light_version(), target_light_version + 1);
}

TEST(WorldRuntimeTests, LightingPublicationInvalidatesDiagonalNeighborMeshesWhenCornerLightChanges) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& lit_chunk = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& x_neighbor = world.ensure_chunk({{1, 0, 0}});
  arv::voxel::ChunkSection& z_neighbor = world.ensure_chunk({{0, 0, 1}});
  arv::voxel::ChunkSection& diagonal = world.ensure_chunk({{1, 0, 1}});
  lit_chunk.clear_dirty(
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::Lighting |
      arv::voxel::ChunkDirtyFlags::LightMesh);
  x_neighbor.clear_dirty(
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::Lighting |
      arv::voxel::ChunkDirtyFlags::LightMesh);
  z_neighbor.clear_dirty(
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::Lighting |
      arv::voxel::ChunkDirtyFlags::LightMesh);
  diagonal.clear_dirty(
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::Lighting |
      arv::voxel::ChunkDirtyFlags::LightMesh);
  const arv::u64 diagonal_light_version = diagonal.light_version();

  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = lit_chunk.lights();
  lights[arv::voxel::local_index({{15, 4, 15}})] = arv::voxel::pack_light(7, 6, 5, 4);
  const arv::u8 positive_x_boundary =
      static_cast<arv::u8>(1U << arv::voxel::face_index(arv::voxel::BlockFace::PositiveX));
  const arv::u8 positive_z_boundary =
      static_cast<arv::u8>(1U << arv::voxel::face_index(arv::voxel::BlockFace::PositiveZ));

  arv::world::LightingResult result;
  result.world_id = arv::WorldID{1};
  result.chunks.push_back(
      {{{0, 0, 0}},
       lit_chunk.version(),
       lights,
       0U,
       static_cast<arv::u8>(positive_x_boundary | positive_z_boundary)});
  result.chunks.back().changed_light_dependency_mask
      [arv::voxel::snapshot_storage_index(1, 0, 1)] = true;

  const arv::world::LightingPublishStats stats =
      world.publish_lighting_result(std::move(result));

  EXPECT_EQ(stats.accepted_chunks, 1U);
  EXPECT_TRUE(x_neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_TRUE(z_neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_TRUE(diagonal.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_FALSE(diagonal.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_EQ(diagonal.light_version(), diagonal_light_version + 1);
}

TEST(WorldRuntimeTests, LightingPublicationAcceptsChunksDirtiedBySameLightingSolve) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& lit_chunk = world.ensure_chunk({{1, 0, 0}});
  target.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);
  lit_chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);

  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lit_chunk_lights =
      lit_chunk.lights();
  lit_chunk_lights[arv::voxel::local_index({{0, 4, 5}})] =
      arv::voxel::pack_light(7, 6, 5, 4);
  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> target_lights =
      target.lights();
  target_lights[arv::voxel::local_index({{15, 4, 5}})] =
      arv::voxel::pack_light(6, 5, 4, 3);

  arv::world::LightingResult result;
  result.world_id = arv::WorldID{1};
  result.chunks.push_back(
      {{{1, 0, 0}}, lit_chunk.version(), lit_chunk_lights, lit_chunk.content_version()});
  result.chunks.push_back(
      {{{0, 0, 0}}, target.version(), target_lights, target.content_version()});

  const arv::world::LightingPublishStats stats =
      world.publish_lighting_result(std::move(result));

  EXPECT_EQ(stats.accepted_chunks, 2U);
  EXPECT_EQ(stats.stale_chunks, 0U);
  EXPECT_EQ(stats.changed_chunks, 2U);
  EXPECT_FALSE(target.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_EQ(
      arv::voxel::light_sky(target.light({{15, 4, 5}})),
      3);
}

TEST(WorldRuntimeTests, LightingPublicationRejectsStaleWorldRevision) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);
  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();

  ASSERT_TRUE(world.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{7}));

  arv::world::LightingResult result;
  result.world_id = arv::WorldID{1};
  result.source_revision = snapshot.source_revision;
  result.chunks.push_back(
      {{{0, 0, 0}}, snapshot.chunks[0].source_version, chunk.lights(), snapshot.chunks[0].source_content_version});

  const arv::world::LightingPublishStats stats =
      world.publish_lighting_result(std::move(result));

  EXPECT_EQ(stats.accepted_chunks, 0U);
  EXPECT_EQ(stats.stale_chunks, 1U);
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
}

TEST(WorldRuntimeTests, RejectsStaleMeshPublicationAndAcceptsCurrentVersion) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  const arv::u64 stale_version = chunk.version();

  EXPECT_TRUE(world.set_block({{1, 1, 1}}, arv::voxel::BlockVariantID{2}));
  EXPECT_FALSE(world.publish_mesh_result({{0, 0, 0}}, stale_version, chunk.light_version()));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));

  EXPECT_TRUE(world.publish_mesh_result({{0, 0, 0}}, chunk.version(), chunk.light_version()));
  EXPECT_FALSE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_EQ(chunk.mesh_state(), arv::voxel::ChunkMeshState::Ready);
}

TEST(WorldRuntimeTests, AcceptsMeshWithStaleLightVersionButKeepsLightMeshDirty) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::LightMesh);

  const auto snapshot = world.make_meshing_snapshot(
      {{0, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);

  ASSERT_TRUE(chunk.set_light({{1, 2, 3}}, arv::voxel::pack_light(1, 2, 3, 4)));

  EXPECT_TRUE(world.publish_mesh_result(
      {{0, 0, 0}},
      snapshot->source_version,
      snapshot->source_light_version));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_FALSE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
}

TEST(WorldRuntimeTests, MeshPublicationRejectsStaleContentVersion) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  const auto snapshot = world.make_meshing_snapshot(
      {{0, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);

  ASSERT_TRUE(world.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{7}));

  EXPECT_FALSE(world.publish_mesh_result(
      {{0, 0, 0}},
      chunk.block_version(),
      snapshot->source_light_version,
      snapshot->source_content_version));
  EXPECT_TRUE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
}

TEST(WorldRuntimeTests, DirtyMeshQueueReportsEditedSectionsWithoutFullScan) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  (void)world.consume_dirty_mesh_chunks(arv::voxel::ChunkDirtyFlags::Geometry);

  ASSERT_TRUE(world.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{7}));
  const std::vector<arv::ChunkCoord> dirty =
      world.consume_dirty_mesh_chunks(arv::voxel::ChunkDirtyFlags::Geometry);

  ASSERT_EQ(dirty.size(), 1U);
  EXPECT_EQ(dirty.front(), (arv::ChunkCoord{{0, 0, 0}}));
  EXPECT_TRUE(world.consume_dirty_mesh_chunks(
                       arv::voxel::ChunkDirtyFlags::Geometry)
                  .empty());
}

TEST(WorldRuntimeTests, ApplyingChunkSnapshotWithoutLightingKeepsChunkMeshable) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  blocks[arv::voxel::local_index({{1, 2, 3}})] = arv::voxel::BlockVariantID{7};

  world.apply_chunk_snapshot({{0, 0, 0}}, std::move(blocks), std::nullopt);

  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_TRUE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_EQ(
      arv::voxel::light_sky(chunk->light({{1, 2, 3}})),
      0);
  const auto snapshot = world.make_meshing_snapshot(
      {{0, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(
      snapshot->blocks()[arv::voxel::local_index({{1, 2, 3}})],
      (arv::voxel::BlockVariantID{7}));
}

TEST(WorldRuntimeTests, ApplyingChunkSnapshotWithoutLightingPreservesExistingLight) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights;
  lights.fill(arv::voxel::NoLight);
  lights[arv::voxel::local_index({{4, 5, 6}})] = arv::voxel::pack_light(1, 2, 3, 4);
  world.apply_chunk_snapshot({{0, 0, 0}}, blocks, lights);

  blocks[arv::voxel::local_index({{1, 2, 3}})] = arv::voxel::BlockVariantID{7};
  world.apply_chunk_snapshot({{0, 0, 0}}, std::move(blocks), std::nullopt);

  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->light({{4, 5, 6}}), arv::voxel::pack_light(1, 2, 3, 4));
  EXPECT_EQ(chunk->block({{1, 2, 3}}), (arv::voxel::BlockVariantID{7}));
  EXPECT_TRUE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
}

TEST(WorldRuntimeTests, ApplyingStaleLightingPayloadUsesValuesButKeepsLightingDirty) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights;
  lights.fill(arv::voxel::NoLight);
  lights[arv::voxel::local_index({{4, 5, 6}})] = arv::voxel::pack_light(1, 2, 3, 4);

  world.apply_chunk_snapshot(
      {{0, 0, 0}},
      blocks,
      lights,
      std::nullopt,
      false);

  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->light({{4, 5, 6}}), arv::voxel::pack_light(1, 2, 3, 4));
  EXPECT_TRUE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
}

TEST(WorldRuntimeTests, ApplyingAuthoritativeChunkSnapshotClearsLightingDirtyWhenLightValuesMatch) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights;
  lights.fill(arv::voxel::NoLight);
  lights[arv::voxel::local_index({{4, 5, 6}})] = arv::voxel::pack_light(1, 2, 3, 4);
  world.apply_chunk_snapshot({{0, 0, 0}}, blocks, lights);

  arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  chunk->mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);

  world.apply_chunk_snapshot({{0, 0, 0}}, std::move(blocks), std::move(lights));

  EXPECT_FALSE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));
  EXPECT_EQ(chunk->light({{4, 5, 6}}), arv::voxel::pack_light(1, 2, 3, 4));
}

TEST(WorldRuntimeTests, ApplyingChunkSnapshotCarriesFluidCells) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  std::array<arv::voxel::FluidCell, arv::voxel::ChunkVolume> fluids;
  fluids.fill(arv::voxel::empty_fluid_cell());
  fluids[arv::voxel::local_index({{1, 2, 3}})] =
      arv::voxel::make_fluid_source(arv::voxel::make_water_fluid_definition());

  world.apply_chunk_snapshot(
      {{0, 0, 0}},
      std::move(blocks),
      std::nullopt,
      std::move(fluids));

  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_TRUE(chunk->fluid({{1, 2, 3}}).source());
  EXPECT_TRUE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
}

TEST(WorldRuntimeTests, DuplicateChunkSnapshotDoesNotAdvanceMeshRevisionAgain) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  blocks[arv::voxel::local_index({{1, 2, 3}})] = arv::voxel::BlockVariantID{7};

  world.apply_chunk_snapshot({{0, 0, 0}}, blocks, std::nullopt);
  const arv::u64 revision_after_first_apply = world.mesh_revision();
  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = chunk->lights();

  world.apply_chunk_snapshot({{0, 0, 0}}, std::move(blocks), std::move(lights));

  EXPECT_EQ(world.mesh_revision(), revision_after_first_apply);
  EXPECT_TRUE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
}

TEST(WorldRuntimeTests, LightOnlyChunkSnapshotOnlyDirtiesBoundaryLightMeshNeighbors) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& center = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& x_neighbor = world.ensure_chunk({{1, 0, 0}});
  arv::voxel::ChunkSection& z_neighbor = world.ensure_chunk({{0, 0, 1}});

  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = center.lights();
  world.apply_chunk_snapshot({{0, 0, 0}}, blocks, lights);
  ASSERT_TRUE(world.publish_mesh_result(
      {{0, 0, 0}},
      center.block_version(),
      center.light_version()));
  center.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::LightMesh);
  x_neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::LightMesh);
  z_neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::LightMesh);

  lights[arv::voxel::local_index({{15, 2, 3}})] = arv::voxel::pack_light(1, 2, 3, 4);
  world.apply_chunk_snapshot({{0, 0, 0}}, std::move(blocks), std::move(lights));

  EXPECT_TRUE(center.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_FALSE(center.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_TRUE(x_neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_FALSE(x_neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_FALSE(z_neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_FALSE(z_neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
}

TEST(WorldRuntimeTests, LightOnlyChunkSnapshotDirtiesDiagonalMeshNeighborAtCorner) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& center = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& diagonal = world.ensure_chunk({{1, 0, 1}});

  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = center.lights();
  world.apply_chunk_snapshot({{0, 0, 0}}, blocks, lights);
  ASSERT_TRUE(world.publish_mesh_result(
      {{0, 0, 0}},
      center.block_version(),
      center.light_version()));
  center.clear_dirty(
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::LightMesh);
  diagonal.clear_dirty(
      arv::voxel::ChunkDirtyFlags::Geometry |
      arv::voxel::ChunkDirtyFlags::LightMesh);

  lights[arv::voxel::local_index({{15, 2, 15}})] = arv::voxel::pack_light(1, 2, 3, 4);
  world.apply_chunk_snapshot({{0, 0, 0}}, std::move(blocks), std::move(lights));

  EXPECT_TRUE(center.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_TRUE(diagonal.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_FALSE(diagonal.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
}

TEST(WorldRuntimeTests, InteriorLightOnlyChunkSnapshotDoesNotDirtyNeighbors) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& center = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& neighbor = world.ensure_chunk({{1, 0, 0}});

  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> blocks;
  blocks.fill(arv::voxel::AirBlockVariantID);
  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = center.lights();
  world.apply_chunk_snapshot({{0, 0, 0}}, blocks, lights);
  ASSERT_TRUE(world.publish_mesh_result(
      {{0, 0, 0}},
      center.block_version(),
      center.light_version()));
  center.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::LightMesh);
  neighbor.clear_dirty(arv::voxel::ChunkDirtyFlags::Geometry | arv::voxel::ChunkDirtyFlags::LightMesh);

  lights[arv::voxel::local_index({{2, 3, 4}})] = arv::voxel::pack_light(1, 2, 3, 4);
  world.apply_chunk_snapshot({{0, 0, 0}}, std::move(blocks), std::move(lights));

  EXPECT_TRUE(center.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_FALSE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
  EXPECT_FALSE(neighbor.is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
}
