#include "arv/render/chunk_mesher.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

namespace {

arv::voxel::BlockRegistry make_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));
  definitions.push_back(arv::voxel::make_full_cube_definition(
      "arvane:glass",
      arv::voxel::RenderCategory::Transparent));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry_result);
  return std::move(registry_result.value());
}

[[nodiscard]] arv::usize face_count(const arv::render::CpuMeshBuffer& mesh) {
  return mesh.indices.size() / 6U;
}

} // namespace

TEST(ChunkMeshingTests, SingleOpaqueCubeEmitsSixFaces) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);

  EXPECT_EQ(face_count(mesh.opaque), 6U);
  EXPECT_EQ(arv::render::chunk_vertex_count(mesh.opaque), 24U);
  EXPECT_EQ(mesh.opaque.indices.size(), 36U);
  EXPECT_EQ(face_count(mesh.transparent), 0U);
}

TEST(ChunkMeshingTests, AdjacentOpaqueCubesCullInternalFaces) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{2, 1, 1}}, stone));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);

  EXPECT_EQ(face_count(mesh.opaque), 10U);
  EXPECT_EQ(mesh.opaque.indices.size(), 60U);
}

TEST(ChunkMeshingTests, TransparentCubeUsesTransparentMeshOutput) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID glass = registry.find_variant("arvane:glass").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{4, 4, 4}}, glass));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);

  EXPECT_EQ(face_count(mesh.opaque), 0U);
  EXPECT_EQ(face_count(mesh.transparent), 6U);
}

TEST(ChunkMeshingTests, NeighborBoundaryCullsChunkEdgeFace) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{15, 1, 1}}, stone));
  arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  snapshot.neighbors[arv::voxel::face_index(arv::voxel::BlockFace::PositiveX)]
      .blocks[arv::voxel::boundary_index_for_face(arv::voxel::BlockFace::PositiveX, {{0, 1, 1}})] = stone;

  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);

  EXPECT_EQ(face_count(mesh.opaque), 5U);
}

TEST(ChunkMeshingTests, MeshVerticesUseExposedNeighborLighting) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_light({{1, 2, 1}}, arv::voxel::pack_light(3, 4, 5, 6)));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);

  ASSERT_GT(arv::render::chunk_vertex_count(mesh.opaque), 0U);
  std::optional<arv::render::ChunkVertex> top_face_vertex;
  for (arv::usize i = 0; i < arv::render::chunk_vertex_count(mesh.opaque); ++i) {
    const arv::render::ChunkVertex vertex = arv::render::chunk_vertex_at(mesh.opaque, i);
    const auto face = static_cast<arv::voxel::BlockFace>((vertex.light_face_flags >> 16U) & 0x07U);
    if (face == arv::voxel::BlockFace::PositiveY) {
      top_face_vertex = vertex;
      break;
    }
  }

  ASSERT_TRUE(top_face_vertex.has_value());
  EXPECT_EQ(top_face_vertex->light_face_flags & 0x0FU, 3U);
  EXPECT_EQ((top_face_vertex->light_face_flags >> 4U) & 0x0FU, 4U);
  EXPECT_EQ((top_face_vertex->light_face_flags >> 8U) & 0x0FU, 5U);
  EXPECT_EQ((top_face_vertex->light_face_flags >> 12U) & 0x0FU, 6U);
}

TEST(ChunkMeshingTests, WorkerMeshResultUsesSnapshotVersionAndStaleResultsAreRejected) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{1, 1, 1}}, stone));
  const auto snapshot = world.make_meshing_snapshot(
      {{0, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);

  arv::sched::Scheduler scheduler(1);
  auto job = scheduler.submit("chunk_mesh_snapshot_test", [snapshot, &registry] {
    return arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot.value(), registry);
  });

  ASSERT_TRUE(world.set_block({{2, 1, 1}}, stone));
  const arv::render::ChunkMeshData mesh = job.get();

  EXPECT_EQ(mesh.source_version, snapshot->source_version);
  EXPECT_FALSE(world.publish_mesh_result(mesh.coord, mesh.source_version));
}
