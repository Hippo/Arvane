#include "arv/render/chunk_mesher.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
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
  arv::voxel::BlockDefinition oak_leaves = arv::voxel::make_full_cube_definition(
      "arvane:oak_leaves",
      arv::voxel::RenderCategory::Cutout);
  oak_leaves.render_tint = arv::voxel::BlockRenderTint::Foliage;
  definitions.push_back(std::move(oak_leaves));
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

arv::render::FluidRenderData make_fluid_render_data(
    const arv::voxel::BlockRegistry& registry) {
  arv::render::FluidRenderData render_data;
  render_data.set_fluid_variant(
      arv::voxel::DefaultWaterFluidTypeID,
      registry.find_variant("arvane:water").value());
  return render_data;
}

arv::voxel::BlockRegistry make_model_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  arv::voxel::BlockDefinition slab;
  slab.name = "arvane:stone_slab";
  slab.render_category = arv::voxel::RenderCategory::Opaque;
  slab.properties.solid = true;
  slab.properties.collidable = true;
  slab.properties.opaque = true;
  slab.properties.blocks_light = true;
  slab.properties.ao_occluder = true;
  slab.model.asset = "arvane:model/block/slab";
  definitions.push_back(std::move(slab));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry_result);
  return std::move(registry_result.value());
}

arv::voxel::BlockRegistry make_directional_model_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());

  arv::voxel::BlockDefinition stairs;
  stairs.name = "arvane:stone_stairs";
  stairs.render_category = arv::voxel::RenderCategory::Opaque;
  stairs.properties.solid = true;
  stairs.properties.collidable = true;
  stairs.properties.opaque = true;
  stairs.properties.blocks_light = true;
  stairs.properties.ao_occluder = true;
  stairs.model.asset = "arvane:model/block/stairs";
  stairs.states.push_back({"facing", {"north", "east"}});
  stairs.states.push_back({"half", {"bottom", "top"}});
  definitions.push_back(std::move(stairs));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry_result);
  return std::move(registry_result.value());
}

arv::voxel::BlockRenderData make_slab_render_data(
    const arv::voxel::BlockRegistry& registry) {
  arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());
  const arv::voxel::BlockVariantID slab =
      registry.find_variant("arvane:stone_slab").value();

  arv::voxel::BlockModelBoxRenderData box;
  box.min = {0, 0, 0};
  box.max = {16, 8, 16};
  for (const arv::voxel::BlockFace face : arv::voxel::AllBlockFaces) {
    const arv::usize index = arv::voxel::face_index(face);
    arv::voxel::BlockModelFaceRenderData face_data;
    face_data.uvs = arv::voxel::default_cube_face_uvs(face);
    box.faces[index] = face_data;
  }
  std::vector<arv::voxel::BlockModelBoxRenderData> boxes;
  boxes.push_back(std::move(box));
  render_data.set_model_boxes(slab, std::move(boxes));
  return render_data;
}

arv::voxel::BlockRenderData make_stair_render_data(
    const arv::voxel::BlockRegistry& registry) {
  arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());

  for (const std::string& name : {
           "arvane:stone_stairs[facing=north,half=bottom]",
           "arvane:stone_stairs[facing=north,half=top]",
           "arvane:stone_stairs[facing=east,half=bottom]",
           "arvane:stone_stairs[facing=east,half=top]",
       }) {
    const arv::voxel::BlockVariantID stairs = registry.find_variant(name).value();
    const arv::voxel::BlockVariantRecord* variant = registry.variant(stairs);
    const arv::voxel::BlockModelTransform transform =
        variant == nullptr ? arv::voxel::BlockModelTransform{} : variant->model_transform;
    render_data.set_model_center_origin(stairs, {8, 8, 8});

    arv::voxel::BlockModelBoxRenderData lower;
    lower.min = {0, 0, 0};
    lower.max = {16, 8, 16};
    arv::voxel::BlockModelBoxRenderData upper;
    upper.min = {0, 8, 0};
    upper.max = {16, 16, 8};
    for (const arv::voxel::BlockFace face : arv::voxel::AllBlockFaces) {
      const arv::usize index = arv::voxel::face_index(face);
      arv::voxel::BlockModelFaceRenderData face_data;
      face_data.uvs = arv::voxel::default_cube_face_uvs(face);
      lower.faces[index] = face_data;
      upper.faces[index] = face_data;
    }
    std::vector<arv::voxel::BlockModelBoxRenderData> boxes;
    lower = arv::voxel::transform_model_box(lower, {8, 8, 8}, transform);
    upper = arv::voxel::transform_model_box(upper, {8, 8, 8}, transform);
    boxes.push_back(std::move(lower));
    boxes.push_back(std::move(upper));
    render_data.set_model_boxes(stairs, std::move(boxes));
  }
  return render_data;
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

[[nodiscard]] arv::world::WorldGenerationConfig flat_generation_config(arv::i32 surface_y) {
  arv::world::WorldGenerationConfig config;
  config.seed_salt = "meshing-test";
  config.min_y = -32;
  config.max_y = 64;
  config.base_height = surface_y;
  config.height_scale = 0;
  return config;
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

TEST(ChunkMeshingTests, CubeVerticesCarryAtlasPageFromRenderData) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());
  for (const arv::voxel::BlockFace face : arv::voxel::AllBlockFaces) {
    arv::voxel::BlockFaceRenderData face_data;
    face_data.page = 3;
    face_data.uvs = arv::voxel::default_cube_face_uvs(face);
    render_data.set_face(stone, face, face_data);
  }
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));

  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry, &render_data);

  ASSERT_GT(arv::render::chunk_vertex_count(mesh.opaque), 0U);
  EXPECT_EQ(arv::render::chunk_vertex_at(mesh.opaque, 0).atlas_page, 3U);
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

TEST(ChunkMeshingTests, FoliageTintUsesOpaqueMeshWithBiomeTintFlag) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID leaves = registry.find_variant("arvane:oak_leaves").value();
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
      arv::render::VertexMaterialClass::Foliage);
  EXPECT_TRUE(arv::render::has_vertex_material_flag(
      arv::render::unpack_chunk_material_flags(vertex.light_face_material_hints),
      arv::render::VertexMaterialFlag_TintFromBiome));
}

TEST(ChunkMeshingTests, CustomBlockModelEmitsBoxFacesForNonFullCubeVariant) {
  const arv::voxel::BlockRegistry registry = make_model_registry();
  const arv::voxel::BlockRenderData render_data = make_slab_render_data(registry);
  const arv::voxel::BlockVariantID slab =
      registry.find_variant("arvane:stone_slab").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, slab));

  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry, &render_data);

  EXPECT_EQ(face_count(mesh.opaque), 6U);
  EXPECT_EQ(face_count(mesh.transparent), 0U);
  EXPECT_TRUE(find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {0, 8, 16},
      arv::voxel::BlockFace::PositiveY));
}

TEST(ChunkMeshingTests, CustomBlockModelBoundaryFacesRespectNeighborOcclusion) {
  const arv::voxel::BlockRegistry registry = make_model_registry();
  const arv::voxel::BlockRenderData render_data = make_slab_render_data(registry);
  const arv::voxel::BlockVariantID slab =
      registry.find_variant("arvane:stone_slab").value();
  const arv::voxel::BlockVariantID stone =
      registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, slab));
  ASSERT_TRUE(chunk.set_block({{2, 1, 1}}, stone));

  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry, &render_data);

  EXPECT_EQ(face_count(mesh.opaque), 11U);
  EXPECT_FALSE(find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {16, 0, 16},
      arv::voxel::BlockFace::PositiveX));
}

TEST(ChunkMeshingTests, DirectionalBlockModelRotationIsAppliedDuringMeshing) {
  const arv::voxel::BlockRegistry registry = make_directional_model_registry();
  const arv::voxel::BlockRenderData render_data = make_stair_render_data(registry);
  const arv::voxel::BlockVariantID east_bottom =
      registry.find_variant("arvane:stone_stairs[facing=east,half=bottom]").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, east_bottom));

  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry, &render_data);

  EXPECT_TRUE(find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {16, 16, 0},
      arv::voxel::BlockFace::PositiveY));
}

TEST(ChunkMeshingTests, FluidCellsEmitIntoTransparentMesh) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::render::FluidRenderData fluid_render_data =
      make_fluid_render_data(registry);
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_fluid({{4, 4, 4}}, arv::voxel::make_fluid_source(water)));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh = arv::render::build_chunk_mesh(
      arv::WorldID{1},
      snapshot,
      registry,
      nullptr,
      &fluid_render_data);

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

TEST(ChunkMeshingTests, FluidCellsSkipMeshWithoutRenderBinding) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_fluid({{4, 4, 4}}, arv::voxel::make_fluid_source(water)));

  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);

  EXPECT_EQ(face_count(mesh.opaque), 0U);
  EXPECT_EQ(face_count(mesh.transparent), 0U);
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
  world.configure_height({0, 1});
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

TEST(ChunkMeshingTests, GeneratedMissingAboveLightsSectionCeilingSurfaceFaces) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_generation_context(123U, flat_generation_config(15));
  world.ensure_chunk({{0, 0, -6}});
  ASSERT_TRUE(world.set_block({{10, 15, -85}}, stone));

  const std::optional<arv::voxel::ChunkSnapshot> snapshot =
      world.make_meshing_snapshot(
          {{0, 0, -6}},
          arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);

  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, *snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{10, 15, 11}},
      {0, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(arv::render::unpack_chunk_sky_light(vertex->light_face_material_hints), 15U);
}

TEST(ChunkMeshingTests, GeneratedDirtyLoadedAboveLightsSectionCeilingSurfaceFaces) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_generation_context(123U, flat_generation_config(15));
  world.ensure_chunk({{1, 0, -2}});
  world.ensure_chunk({{1, 1, -2}});
  world.mark_chunk_lighting_dirty({{1, 1, -2}});
  ASSERT_TRUE(world.set_block({{30, 15, -18}}, stone));

  const std::optional<arv::voxel::ChunkSnapshot> snapshot =
      world.make_meshing_snapshot(
          {{1, 0, -2}},
          arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);

  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, *snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{14, 15, 14}},
      {0, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(arv::render::unpack_chunk_sky_light(vertex->light_face_material_hints), 15U);
}

TEST(ChunkMeshingTests, GeneratedMissingAboveStaysDarkBelowSurface) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_generation_context(123U, flat_generation_config(20));
  world.ensure_chunk({{0, 0, -6}});
  ASSERT_TRUE(world.set_block({{10, 15, -85}}, stone));

  const std::optional<arv::voxel::ChunkSnapshot> snapshot =
      world.make_meshing_snapshot(
          {{0, 0, -6}},
          arv::world::MissingNeighborPolicy::TreatMissingAsAir);
  ASSERT_TRUE(snapshot);

  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, *snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{10, 15, 11}},
      {0, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(arv::render::unpack_chunk_sky_light(vertex->light_face_material_hints), 0U);
}

TEST(ChunkMeshingTests, MeshVerticesAverageFaceSideAndCornerLighting) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_light({{1, 2, 1}}, arv::voxel::pack_light(8, 8, 8, 8)));
  ASSERT_TRUE(chunk.set_light({{0, 2, 1}}, arv::voxel::pack_light(4, 0, 0, 4)));
  ASSERT_TRUE(chunk.set_light({{1, 2, 2}}, arv::voxel::pack_light(0, 6, 0, 6)));
  ASSERT_TRUE(chunk.set_light({{0, 2, 2}}, arv::voxel::pack_light(0, 0, 2, 2)));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {0, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(arv::render::unpack_chunk_block_light_r(vertex->light_face_material_hints), 3U);
  EXPECT_EQ(arv::render::unpack_chunk_block_light_g(vertex->light_face_material_hints), 3U);
  EXPECT_EQ(arv::render::unpack_chunk_block_light_b(vertex->light_face_material_hints), 2U);
  EXPECT_EQ(arv::render::unpack_chunk_sky_light(vertex->light_face_material_hints), 5U);
}

TEST(ChunkMeshingTests, MeshVerticesIgnoreZeroSmoothLightSamplesInDivisor) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_block({{1, 1, 1}}, stone));
  ASSERT_TRUE(chunk.set_light({{1, 2, 1}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 8)));
  ASSERT_TRUE(chunk.set_light({{1, 2, 2}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 4)));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.opaque,
      {{1, 1, 1}},
      {0, 16, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(arv::render::unpack_chunk_sky_light(vertex->light_face_material_hints), 6U);
}

TEST(ChunkMeshingTests, PartialHeightFluidSmoothLightingDoesNotOffsetVerticalSamples) {
  const arv::voxel::BlockRegistry registry = make_registry();
  const arv::render::FluidRenderData fluid_render_data =
      make_fluid_render_data(registry);
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  ASSERT_TRUE(chunk.set_fluid({{1, 1, 1}}, arv::voxel::make_fluid_source(water)));
  ASSERT_TRUE(chunk.set_light({{1, 2, 1}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 8)));
  ASSERT_TRUE(chunk.set_light({{0, 1, 1}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 4)));
  ASSERT_TRUE(chunk.set_light({{1, 1, 2}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 6)));
  ASSERT_TRUE(chunk.set_light({{0, 1, 2}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 2)));
  ASSERT_TRUE(chunk.set_light({{0, 2, 1}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 15)));
  ASSERT_TRUE(chunk.set_light({{1, 2, 2}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 15)));
  ASSERT_TRUE(chunk.set_light({{0, 2, 2}}, arv::voxel::with_sky_light(arv::voxel::NoLight, 15)));

  const arv::voxel::ChunkSnapshot snapshot = arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
  const arv::render::ChunkMeshData mesh =
      arv::render::build_chunk_mesh(
          arv::WorldID{1},
          snapshot,
          registry,
          nullptr,
          &fluid_render_data);
  const std::optional<arv::render::ChunkVertex> vertex = find_vertex(
      mesh.transparent,
      {{1, 1, 1}},
      {0, 14, 16},
      arv::voxel::BlockFace::PositiveY);

  ASSERT_TRUE(vertex);
  EXPECT_EQ(arv::render::unpack_chunk_sky_light(vertex->light_face_material_hints), 5U);
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

  arv::sched::JobSystemConfig job_config;
  job_config.critical_cpu_workers = 1;
  job_config.background_cpu_workers = 1;
  job_config.blocking_io_workers = 1;
  arv::sched::JobSystem jobs{job_config};
  jobs.start();
  arv::sched::JobDesc job_desc;
  job_desc.name = "chunk_mesh_snapshot_test";
  job_desc.lane = arv::sched::JobLane::BackgroundCpu;
  job_desc.domain = arv::sched::JobDomain::Meshing;
  auto job = jobs.submit_result(std::move(job_desc), [snapshot, &registry] {
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
