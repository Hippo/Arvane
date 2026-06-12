#include "web_chunk_mesh.hpp"

#include "arv/voxel/storage.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace {

arv::protocol::ClientboundChunkSectionData make_section(arv::ChunkSectionCoord coord = {{0, 0, 0}}) {
  arv::protocol::ClientboundChunkSectionData section;
  section.world_id = arv::WorldID{1};
  section.coord = coord;
  section.chunk_content_version = 1;
  section.blocks.fill(arv::voxel::AirBlockVariantID);
  return section;
}

void set_block(
    arv::protocol::ClientboundChunkSectionData& section,
    int x,
    int y,
    int z,
    arv::voxel::BlockVariantID block) {
  section.blocks[arv::voxel::local_index(arv::SectionLocalPos{{x, y, z}})] = block;
}

struct Bounds {
  arv::Vec3f min{};
  arv::Vec3f max{};
};

Bounds bounds_for(std::span<const arv::web::WebChunkVertex> vertices) {
  Bounds bounds{vertices.front().position, vertices.front().position};
  for (const arv::web::WebChunkVertex& vertex : vertices) {
    bounds.min.x = std::min(bounds.min.x, vertex.position.x);
    bounds.min.y = std::min(bounds.min.y, vertex.position.y);
    bounds.min.z = std::min(bounds.min.z, vertex.position.z);
    bounds.max.x = std::max(bounds.max.x, vertex.position.x);
    bounds.max.y = std::max(bounds.max.y, vertex.position.y);
    bounds.max.z = std::max(bounds.max.z, vertex.position.z);
  }
  return bounds;
}

void expect_all_finite(const arv::web::Mat4& matrix) {
  for (float value : matrix.values) {
    EXPECT_TRUE(std::isfinite(value));
  }
}

} // namespace

TEST(WebChunkMeshTests, AirChunkProducesNoFallbackVertices) {
  const auto vertices = arv::web::build_fallback_chunk_vertices(make_section());
  EXPECT_TRUE(vertices.empty());
}

TEST(WebChunkMeshTests, SingleServerBlockProducesSixOpaqueCubeFacesAtWorldPosition) {
  auto section = make_section({{2, -1, -3}});
  set_block(section, 1, 2, 3, arv::voxel::BlockVariantID{7});

  const auto vertices = arv::web::build_fallback_chunk_vertices(section);

  ASSERT_EQ(vertices.size(), 36U);
  const Bounds bounds = bounds_for(vertices);
  EXPECT_EQ(bounds.min, (arv::Vec3f{33.0F, -14.0F, -45.0F}));
  EXPECT_EQ(bounds.max, (arv::Vec3f{34.0F, -13.0F, -44.0F}));
  for (const arv::web::WebChunkVertex& vertex : vertices) {
    EXPECT_GE(vertex.color.x, 0.0F);
    EXPECT_GE(vertex.color.y, 0.0F);
    EXPECT_GE(vertex.color.z, 0.0F);
    EXPECT_LE(vertex.color.x, 1.0F);
    EXPECT_LE(vertex.color.y, 1.0F);
    EXPECT_LE(vertex.color.z, 1.0F);
  }
}

TEST(WebChunkMeshTests, AdjacentServerBlocksHideSharedFace) {
  auto section = make_section();
  set_block(section, 1, 2, 3, arv::voxel::BlockVariantID{7});
  set_block(section, 2, 2, 3, arv::voxel::BlockVariantID{8});

  const auto vertices = arv::web::build_fallback_chunk_vertices(section);

  EXPECT_EQ(vertices.size(), 60U);
  const Bounds bounds = bounds_for(vertices);
  EXPECT_EQ(bounds.min, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
  EXPECT_EQ(bounds.max, (arv::Vec3f{3.0F, 3.0F, 4.0F}));
}

TEST(WebChunkMeshTests, EntityMarkersBuildUprightFallbackCubes) {
  const std::array<arv::web::WebEntityMarker, 2> markers{{
      {{4.0F, 5.0F, 6.0F}, arv::EntityTypeID{2}, 0U},
      {{8.0F, 9.0F, 10.0F}, arv::EntityTypeID{3}, arv::protocol::EntityStateFlagHasHealth},
  }};

  const auto vertices = arv::web::build_fallback_entity_vertices(markers);

  ASSERT_EQ(vertices.size(), 72U);
  const Bounds bounds = bounds_for(vertices);
  EXPECT_EQ(bounds.min, (arv::Vec3f{3.7F, 5.0F, 5.7F}));
  EXPECT_EQ(bounds.max, (arv::Vec3f{8.3F, 10.8F, 10.3F}));
  for (const arv::web::WebChunkVertex& vertex : vertices) {
    EXPECT_GE(vertex.color.x, 0.0F);
    EXPECT_GE(vertex.color.y, 0.0F);
    EXPECT_GE(vertex.color.z, 0.0F);
    EXPECT_LE(vertex.color.x, 1.0F);
    EXPECT_LE(vertex.color.y, 1.0F);
    EXPECT_LE(vertex.color.z, 1.0F);
  }
}

TEST(WebChunkMeshTests, DeadEntityMarkersUseDistinctFallbackColor) {
  const std::array<arv::web::WebEntityMarker, 1> markers{{
      {{0.0F, 0.0F, 0.0F}, arv::EntityTypeID{1}, arv::protocol::EntityStateFlagDead},
  }};

  const auto vertices = arv::web::build_fallback_entity_vertices(markers);

  ASSERT_FALSE(vertices.empty());
  EXPECT_EQ(vertices.front().color, (arv::Vec3f{0.45F, 0.12F, 0.14F}));
}

TEST(WebChunkMeshTests, CameraMatricesAreColumnMajorAndFiniteForWebGlUniforms) {
  const arv::web::Mat4 identity_view =
      arv::web::look_at({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F});
  const arv::web::Mat4 projection =
      arv::web::perspective(arv::degrees_to_radians(70.0F), 16.0F / 9.0F, 0.05F, 512.0F);
  const arv::web::Mat4 combined = arv::web::multiply(projection, identity_view);

  expect_all_finite(identity_view);
  expect_all_finite(projection);
  expect_all_finite(combined);
  EXPECT_FLOAT_EQ(identity_view.values[0], 1.0F);
  EXPECT_FLOAT_EQ(identity_view.values[5], 1.0F);
  EXPECT_FLOAT_EQ(identity_view.values[10], 1.0F);
  EXPECT_FLOAT_EQ(identity_view.values[15], 1.0F);
  EXPECT_FLOAT_EQ(projection.values[11], -1.0F);
  for (arv::usize i = 0; i < projection.values.size(); ++i) {
    EXPECT_FLOAT_EQ(combined.values[i], projection.values[i]);
  }
}
