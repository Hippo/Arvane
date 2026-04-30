#include "arv/render/opengl_renderer.hpp"
#include "arv/render/render_types.hpp"

#include <gtest/gtest.h>

namespace {

arv::render::ChunkMeshUpload make_chunk_upload(arv::u64 source_version) {
  arv::render::ChunkMeshUpload upload;
  upload.mesh.world_id = arv::WorldID{1};
  upload.mesh.coord = {{0, 0, 0}};
  upload.mesh.source_version = source_version;
  return upload;
}

arv::render::ChunkMeshUpload make_chunk_upload(
    arv::u64 source_version,
    arv::u64 source_light_version) {
  arv::render::ChunkMeshUpload upload = make_chunk_upload(source_version);
  upload.mesh.source_light_version = source_light_version;
  return upload;
}

} // namespace

TEST(RenderMeshHandoffTests, RejectsStaleMeshUpload) {
  arv::render::ChunkRenderState current;
  current.uploaded_version = 4;
  current.resident = true;

  arv::render::ChunkMeshUpload stale;
  stale.mesh.world_id = arv::WorldID{1};
  stale.mesh.coord = {{0, 0, 0}};
  stale.mesh.source_version = 3;

  EXPECT_FALSE(arv::render::should_accept_chunk_mesh_upload(&current, stale));
}

TEST(RenderMeshHandoffTests, KeepsResidentMeshDrawableWhileNewerUploadIsPending) {
  arv::render::ChunkRenderState current;
  current.uploaded_version = 4;
  current.resident = true;

  arv::render::ChunkDrawItem draw_item;
  draw_item.expected_version = 5;

  EXPECT_TRUE(arv::render::should_draw_chunk_mesh(&current, draw_item));
}

TEST(RenderMeshHandoffTests, SkipsChunkWithoutResidentMesh) {
  arv::render::ChunkDrawItem draw_item;
  draw_item.expected_version = 5;

  EXPECT_FALSE(arv::render::should_draw_chunk_mesh(nullptr, draw_item));

  arv::render::ChunkRenderState current;
  current.uploaded_version = 4;
  current.resident = false;
  EXPECT_FALSE(arv::render::should_draw_chunk_mesh(&current, draw_item));
}

TEST(RenderMeshHandoffTests, AcceptsMatchingLiveChunkVersion) {
  arv::render::ChunkMeshData mesh;
  mesh.source_version = 8;

  EXPECT_TRUE(arv::render::chunk_mesh_matches_live_version(mesh, 8));
  EXPECT_FALSE(arv::render::chunk_mesh_matches_live_version(mesh, 7));
}

TEST(RenderMeshHandoffTests, DeduplicatesPendingChunkMeshUploadsByVersion) {
  arv::render::OpenGLRenderer renderer;

  renderer.enqueue_chunk_mesh_upload(make_chunk_upload(5));
  renderer.enqueue_chunk_mesh_upload(make_chunk_upload(4));
  EXPECT_EQ(renderer.pending_mesh_upload_count(), 1U);

  renderer.enqueue_chunk_mesh_upload(make_chunk_upload(6));
  EXPECT_EQ(renderer.pending_mesh_upload_count(), 1U);
}

TEST(RenderMeshHandoffTests, AcceptsNewerLightOnlyMeshUpload) {
  arv::render::ChunkRenderState current;
  current.uploaded_version = 5;
  current.uploaded_light_version = 2;
  current.resident = true;

  EXPECT_TRUE(arv::render::should_accept_chunk_mesh_upload(
      &current,
      make_chunk_upload(5, 3)));
  EXPECT_FALSE(arv::render::should_accept_chunk_mesh_upload(
      &current,
      make_chunk_upload(5, 1)));
}

TEST(RenderMeshHandoffTests, ChunkVertexPackingFollowsDocumentedBitLayout) {
  const arv::render::ChunkVertex vertex = arv::render::make_chunk_vertex(
      {{3, 4, 5}},
      {16, 8, 0},
      12,
      34,
      arv::voxel::BlockFace::PositiveY,
      2,
      15,
      3,
      4,
      5,
      {arv::render::VertexMaterialClass::Water,
       arv::render::VertexAnimationId::WaterSurface,
       arv::render::VertexMaterialFlag_TintFromFluid |
           arv::render::VertexMaterialFlag_DisableAo});

  EXPECT_EQ(vertex.pos_ao & 0x1FU, 3U);
  EXPECT_EQ((vertex.pos_ao >> 5U) & 0x1FU, 4U);
  EXPECT_EQ((vertex.pos_ao >> 10U) & 0x1FU, 5U);
  EXPECT_EQ((vertex.pos_ao >> 15U) & 0x1FU, 16U);
  EXPECT_EQ((vertex.pos_ao >> 20U) & 0x1FU, 8U);
  EXPECT_EQ((vertex.pos_ao >> 30U) & 0x03U, 2U);
  EXPECT_EQ(vertex.uv & 0xFFFFU, 12U);
  EXPECT_EQ((vertex.uv >> 16U) & 0xFFFFU, 34U);
  EXPECT_EQ(
      (vertex.light_face_material_hints >> arv::render::ChunkBlockLightRShift) &
          arv::render::ChunkLightMask,
      3U);
  EXPECT_EQ(
      (vertex.light_face_material_hints >> arv::render::ChunkBlockLightGShift) &
          arv::render::ChunkLightMask,
      4U);
  EXPECT_EQ(
      (vertex.light_face_material_hints >> arv::render::ChunkBlockLightBShift) &
          arv::render::ChunkLightMask,
      5U);
  EXPECT_EQ(arv::render::unpack_chunk_sky_light(vertex.light_face_material_hints), 15U);
  EXPECT_EQ(
      arv::render::unpack_chunk_face(vertex.light_face_material_hints),
      arv::voxel::BlockFace::PositiveY);
  EXPECT_EQ(
      arv::render::unpack_chunk_material_class(vertex.light_face_material_hints),
      arv::render::VertexMaterialClass::Water);
  EXPECT_EQ(
      arv::render::unpack_chunk_animation_id(vertex.light_face_material_hints),
      arv::render::VertexAnimationId::WaterSurface);
  const arv::render::VertexMaterialFlags flags =
      arv::render::unpack_chunk_material_flags(vertex.light_face_material_hints);
  EXPECT_TRUE(arv::render::has_vertex_material_flag(
      flags,
      arv::render::VertexMaterialFlag_TintFromFluid));
  EXPECT_TRUE(arv::render::has_vertex_material_flag(
      flags,
      arv::render::VertexMaterialFlag_DisableAo));
}
