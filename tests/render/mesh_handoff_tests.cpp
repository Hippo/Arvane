#include "arv/render/render_types.hpp"

#include <gtest/gtest.h>

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

TEST(RenderMeshHandoffTests, AcceptsMatchingLiveChunkVersion) {
  arv::render::ChunkMeshData mesh;
  mesh.source_version = 8;

  EXPECT_TRUE(arv::render::chunk_mesh_matches_live_version(mesh, 8));
  EXPECT_FALSE(arv::render::chunk_mesh_matches_live_version(mesh, 7));
}

TEST(RenderMeshHandoffTests, ChunkVertexPackingFollowsDocumentedBitLayout) {
  const arv::render::ChunkVertex vertex = arv::render::make_chunk_vertex(
      {{3, 4, 5}},
      {16, 8, 0},
      12,
      34,
      arv::voxel::BlockFace::PositiveY,
      2,
      15);

  EXPECT_EQ(vertex.pos_ao & 0x1FU, 3U);
  EXPECT_EQ((vertex.pos_ao >> 5U) & 0x1FU, 4U);
  EXPECT_EQ((vertex.pos_ao >> 10U) & 0x1FU, 5U);
  EXPECT_EQ((vertex.pos_ao >> 15U) & 0x1FU, 16U);
  EXPECT_EQ((vertex.pos_ao >> 20U) & 0x1FU, 8U);
  EXPECT_EQ((vertex.pos_ao >> 30U) & 0x03U, 2U);
  EXPECT_EQ(vertex.uv & 0xFFFFU, 12U);
  EXPECT_EQ((vertex.uv >> 16U) & 0xFFFFU, 34U);
  EXPECT_EQ((vertex.light_face_flags >> 12U) & 0x0FU, 15U);
  EXPECT_EQ((vertex.light_face_flags >> 16U) & 0x07U, 2U);
}
