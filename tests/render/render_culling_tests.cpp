#include "arv/render/render_culling.hpp"

#include <gtest/gtest.h>

namespace {

[[nodiscard]] arv::render::RenderView make_test_view() {
  arv::render::RenderView view;
  view.world_id = arv::WorldID{1};
  view.position = {8.0F, 8.0F, -32.0F};
  view.forward = {0.0F, 0.0F, 1.0F};
  view.right = {1.0F, 0.0F, 0.0F};
  view.up = {0.0F, 1.0F, 0.0F};
  view.near_plane = 0.1F;
  view.far_plane = 96.0F;
  view.fov_y_degrees = 90.0F;
  view.viewport_width = 100;
  view.viewport_height = 100;
  return view;
}

} // namespace

TEST(RenderCullingTests, KeepsChunkInFrontOfCamera) {
  EXPECT_TRUE(arv::render::chunk_aabb_intersects_view(
      make_test_view(),
      arv::ChunkSectionCoord{{0, 0, 0}}));
}

TEST(RenderCullingTests, RejectsChunkBehindCamera) {
  EXPECT_FALSE(arv::render::chunk_aabb_intersects_view(
      make_test_view(),
      arv::ChunkSectionCoord{{0, 0, -4}}));
}

TEST(RenderCullingTests, RejectsChunkOutsideHorizontalFov) {
  EXPECT_FALSE(arv::render::chunk_aabb_intersects_view(
      make_test_view(),
      arv::ChunkSectionCoord{{10, 0, 0}}));
}

TEST(RenderCullingTests, RejectsChunkOutsideVerticalFov) {
  EXPECT_FALSE(arv::render::chunk_aabb_intersects_view(
      make_test_view(),
      arv::ChunkSectionCoord{{0, 10, 0}}));
}

TEST(RenderCullingTests, KeepsChunkCrossingFrustumEdge) {
  EXPECT_TRUE(arv::render::chunk_aabb_intersects_view(
      make_test_view(),
      arv::ChunkSectionCoord{{3, 0, 0}}));
}

TEST(RenderCullingTests, RejectsChunkBeyondFarPlane) {
  EXPECT_FALSE(arv::render::chunk_aabb_intersects_view(
      make_test_view(),
      arv::ChunkSectionCoord{{0, 0, 9}}));
}
