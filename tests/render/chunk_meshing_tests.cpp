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
  definitions.push_back(arv::voxel::make_full_cube_definition(
      "arvane:leaves",
      arv::voxel::RenderCategory::Cutout));
  arv::voxel::BlockDefinition water;
  water.name = "arvane:water";
  water.properties.replaceable = true;
  water.properties.fluid = true;
  water.render_category = arv::voxel::RenderCategory::Transparent;
  water.is_item = false;
  water.texture_assets.fill("arvane:texture/block/water");
  definitions.push_back(std::move(water));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry_result);
  return std::move(registry_result.value());
}

[[nodiscard]] arv::usize face_count(const arv::render::CpuMeshBuffer& mesh) {
  return mesh.indices.size() / 6U;
}

[[nodiscard]] arv::u8 vertex_ao(arv::render::ChunkVertex vertex) {
  return static_cast<arv::u8>((vertex.pos_ao >> 30U) & 0x03U);
}

[[nodiscard]] bool vertex_matches(
    arv::render::ChunkVertex vertex,
    arv::SectionLocalPos block,
    arv::Vec3i model_position,
    arv::voxel::BlockFace face) {
  return (vertex.pos_ao & 0x1FU) == static_cast<arv::u32>(block.value.x) &&
         ((vertex.pos_ao >> 5U) & 0x1FU) == static_cast<arv::u32>(block.value.y) &&
         ((vertex.pos_ao >> 10U) & 0x1FU) == static_cast<arv::u32>(block.value.z) &&
         ((vertex.pos_ao >> 15U) & 0x1FU) == static_cast<arv::u32>(model_position.x) &&
         ((vertex.pos_ao >> 20U) & 0x1FU) == static_cast<arv::u32>(model_position.y) &&
         ((vertex.pos_ao >> 25U) & 0x1FU) == static_cast<arv::u32>(model_position.z) &&
         arv::render::unpack_chunk_face(vertex.light_face_material_hints) == face;
}

[[nodiscard]] std::optional<arv::render::ChunkVertex> find_vertex(
    const arv::render::CpuMeshBuffer& mesh,
    arv::SectionLocalPos block,
    arv::Vec3i model_position,
    arv::voxel::BlockFace face) {
  for (arv::usize i = 0; i < arv::render::chunk_vertex_count(mesh); ++i) {
    const arv::render::ChunkVertex vertex = arv::render::chunk_vertex_at(mesh, i);
    if (vertex_matches(vertex, block, model_position, face)) {
      return vertex;
    }
  }
  return std::nullopt;
}

} // namespace

TEST(ChunkMeshingTests, AmbientOcclusionRuleCoversAllOccupancyCombinations) {
  for (int side0 = 0; side0 <= 1; ++side0) {
    for (int side1 = 0; side1 <= 1; ++side1) {
      for (int corner = 0; corner <= 1; ++corner) {
        const arv::u8 expected = side0 != 0 && side1 != 0
                                     ? 0
                                     : static_cast<arv::u8>(3 - side0 - side1 - corner);
        EXPECT_EQ(
            arv::render::voxel_ambient_occlusion_value(side0 != 0, side1 != 0, corner != 0),
            expected);
      }
    }
  }
}

TEST(ChunkMeshingTests, AmbientOcclusionBothSidesFilledIsFullyOccluded) {
  EXPECT_EQ(arv::render::voxel_ambient_occlusion_value(true, true, false), 0U);
  EXPECT_EQ(arv::render::voxel_ambient_occlusion_value(true, true, true), 0U);
}

TEST(ChunkMeshingTests, AmbientOcclusionQuadFlipDecisionUsesDiagonalSums) {
  EXPECT_FALSE(arv::render::should_flip_voxel_ao_quad({{2, 1, 0, 1}}));
  EXPECT_FALSE(arv::render::should_flip_voxel_ao_quad({{3, 0, 1, 2}}));
  EXPECT_TRUE(arv::render::should_flip_voxel_ao_quad({{1, 2, 1, 0}}));
}

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
  ASSERT_GT(arv::render::chunk_vertex_count(mesh.transparent), 0U);
  const arv::render::ChunkVertex vertex = arv::render::chunk_vertex_at(mesh.transparent, 0);
  EXPECT_EQ(
      arv::render::unpack_chunk_material_class(vertex.light_face_material_hints),
      arv::render::VertexMaterialClass::Transparent);
  EXPECT_EQ(
      arv::render::unpack_chunk_animation_id(vertex.light_face_material_hints),
      arv::render::VertexAnimationId::None);
  EXPECT_EQ(
      arv::render::unpack_chunk_material_flags(vertex.light_face_material_hints),
      arv::render::VertexMaterialFlag_None);
}

TEST(ChunkMeshingTests, CutoutCubeUsesOpaqueMeshWithCutoutMaterialClass) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID leaves = registry.find_variant("arvane:leaves").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{4, 4, 4}}, leaves));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);

  EXPECT_EQ(face_count(mesh.opaque), 6U);
  EXPECT_EQ(face_count(mesh.transparent), 0U);
  ASSERT_GT(arv::render::chunk_vertex_count(mesh.opaque), 0U);
  const arv::render::ChunkVertex vertex = arv::render::chunk_vertex_at(mesh.opaque, 0);
  EXPECT_EQ(
      arv::render::unpack_chunk_material_class(vertex.light_face_material_hints),
      arv::render::VertexMaterialClass::Cutout);
}

TEST(ChunkMeshingTests, FluidCellsEmitIntoTransparentMesh) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_fluid({{4, 4, 4}}, arv::voxel::make_fluid_source(water)));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);

  EXPECT_EQ(face_count(mesh.opaque), 0U);
  EXPECT_EQ(face_count(mesh.transparent), 6U);

  bool saw_surface_animation = false;
  bool saw_still_animation = false;
  for (arv::usize i = 0; i < arv::render::chunk_vertex_count(mesh.transparent); ++i) {
    const arv::render::ChunkVertex vertex = arv::render::chunk_vertex_at(mesh.transparent, i);
    const arv::u32 packed = vertex.light_face_material_hints;
    EXPECT_EQ(
        arv::render::unpack_chunk_material_class(packed),
        arv::render::VertexMaterialClass::Water);
    const arv::render::VertexMaterialFlags material_flags =
        arv::render::unpack_chunk_material_flags(packed);
    EXPECT_TRUE(arv::render::has_vertex_material_flag(
        material_flags,
        arv::render::VertexMaterialFlag_TintFromFluid));
    EXPECT_TRUE(arv::render::has_vertex_material_flag(
        material_flags,
        arv::render::VertexMaterialFlag_DisableAo));

    const arv::render::VertexAnimationId animation_id =
        arv::render::unpack_chunk_animation_id(packed);
    if (arv::render::unpack_chunk_face(packed) == arv::voxel::BlockFace::PositiveY) {
      EXPECT_EQ(animation_id, arv::render::VertexAnimationId::WaterSurface);
      saw_surface_animation = true;
    } else if (animation_id == arv::render::VertexAnimationId::WaterStill) {
      saw_still_animation = true;
    }
  }
  EXPECT_TRUE(saw_surface_animation);
  EXPECT_TRUE(saw_still_animation);
}

TEST(ChunkMeshingTests, SameLayerNeighborsDoNotDarkenFlatTopFaces) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{0, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{1, 1, 2}}, stone));
  ASSERT_TRUE(chunk.set_block({{0, 1, 2}}, stone));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {0, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(vertex_ao(vertex.value()), 3U);
}

TEST(ChunkMeshingTests, FaceAdjacentOpaqueOccludersPackPerVertexAmbientOcclusion) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{0, 2, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{1, 2, 2}}, stone));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {0, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(vertex_ao(vertex.value()), 0U);
}

TEST(ChunkMeshingTests, SideFacesUseFaceAdjacentAmbientOcclusionSamples) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{2, 2, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{2, 1, 2}}, stone));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {16, 16, 16},
      arv::voxel::BlockFace::PositiveX);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(vertex_ao(vertex.value()), 0U);
}

TEST(ChunkMeshingTests, SideFacesIgnoreOccludersOutsideTheirFacePlane) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{2, 2, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{1, 2, 2}}, stone));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {16, 16, 16},
      arv::voxel::BlockFace::PositiveX);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(vertex_ao(vertex.value()), 2U);
}

TEST(ChunkMeshingTests, FrontFacesUseFaceAdjacentAmbientOcclusionSamples) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{2, 1, 2}}, stone));
  ASSERT_TRUE(chunk.set_block({{1, 2, 2}}, stone));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {16, 16, 16},
      arv::voxel::BlockFace::PositiveZ);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(vertex_ao(vertex.value()), 0U);
}

TEST(ChunkMeshingTests, TransparentBlocksDoNotContributeToAmbientOcclusion) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID glass = registry.find_variant("arvane:glass").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_block({{0, 2, 1}}, glass));
  ASSERT_TRUE(chunk.set_block({{1, 2, 2}}, glass));
  ASSERT_TRUE(chunk.set_block({{0, 2, 2}}, glass));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {0, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(vertex_ao(vertex.value()), 3U);
}

TEST(ChunkMeshingTests, AmbientOcclusionSamplesChunkHaloCorners) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  world.ensure_chunk({{1, 1, 1}});
  ASSERT_TRUE(world.set_block({{15, 15, 15}}, stone));
  ASSERT_TRUE(world.set_block({{16, 16, 16}}, stone));

  const auto snapshot = world.make_meshing_snapshot(
      {{0, 0, 0}},
      arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot.value(), registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{15, 15, 15}},
      {16, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(vertex_ao(vertex.value()), 2U);
}

TEST(ChunkMeshingTests, NeighborBoundaryCullsChunkEdgeFace) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  arv::voxel::ChunkSection neighbor{{{1, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{15, 1, 1}}, stone));
  ASSERT_TRUE(neighbor.set_block({{0, 1, 1}}, stone));
  arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  snapshot.storages[arv::voxel::snapshot_storage_index(1, 0, 0)] = neighbor.storage_view();

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
    const auto face = arv::render::unpack_chunk_face(vertex.light_face_material_hints);
    if (face == arv::voxel::BlockFace::PositiveY) {
      top_face_vertex = vertex;
      break;
    }
  }

  ASSERT_TRUE(top_face_vertex.has_value());
  EXPECT_EQ(arv::render::unpack_chunk_block_light_r(top_face_vertex->light_face_material_hints), 3U);
  EXPECT_EQ(arv::render::unpack_chunk_block_light_g(top_face_vertex->light_face_material_hints), 4U);
  EXPECT_EQ(arv::render::unpack_chunk_block_light_b(top_face_vertex->light_face_material_hints), 5U);
  EXPECT_EQ(arv::render::unpack_chunk_sky_light(top_face_vertex->light_face_material_hints), 6U);
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
  EXPECT_EQ(mesh.source_content_version, snapshot->source_content_version);
  EXPECT_EQ(mesh.source_light_version, snapshot->source_light_version);
  EXPECT_FALSE(world.publish_mesh_result(
      mesh.coord,
      mesh.source_version,
      mesh.source_light_version));
}
