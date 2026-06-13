#include "web_chunk_mesh.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace {

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

void expect_all_finite(const arv::Mat4f& matrix) {
  for (float value : matrix.values) {
    EXPECT_TRUE(std::isfinite(value));
  }
}

void expect_near_vec3(arv::Vec3f actual, arv::Vec3f expected, float epsilon = 0.0001F) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

} // namespace

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

TEST(WebChunkMeshTests, FallbackEntityMarkersHideLocalPlayerForFirstPersonView) {
  arv::client::ClientSessionWorldJoin join;
  join.world_id = arv::WorldID{1};
  join.player_id = arv::PlayerID{2};
  join.local_entity_id = arv::EntityNetID{3};

  std::array<arv::client::ClientSessionEntityMirror, 4> mirrors{};
  mirrors[0].world_id = arv::WorldID{1};
  mirrors[0].entity_id = arv::EntityNetID{3};
  mirrors[0].entity_type = arv::EntityTypeID{10};
  mirrors[0].position = {1.0F, 2.0F, 3.0F};
  mirrors[1].world_id = arv::WorldID{1};
  mirrors[1].entity_id = arv::EntityNetID{4};
  mirrors[1].entity_type = arv::EntityTypeID{11};
  mirrors[1].owning_player = arv::PlayerID{2};
  mirrors[1].position = {4.0F, 5.0F, 6.0F};
  mirrors[2].world_id = arv::WorldID{1};
  mirrors[2].entity_id = arv::EntityNetID{5};
  mirrors[2].entity_type = arv::EntityTypeID{12};
  mirrors[2].owning_player = arv::PlayerID{9};
  mirrors[2].position = {7.0F, 8.0F, 9.0F};
  mirrors[2].state_flags = arv::protocol::EntityStateFlagHasHealth;
  mirrors[3].world_id = arv::WorldID{99};
  mirrors[3].entity_id = arv::EntityNetID{6};
  mirrors[3].entity_type = arv::EntityTypeID{13};
  mirrors[3].position = {10.0F, 11.0F, 12.0F};

  const auto markers = arv::web::build_visible_fallback_entity_markers(mirrors, join);

  ASSERT_EQ(markers.size(), 1U);
  EXPECT_EQ(markers.front().position, (arv::Vec3f{7.0F, 8.0F, 9.0F}));
  EXPECT_EQ(markers.front().entity_type, arv::EntityTypeID{12});
  EXPECT_EQ(markers.front().state_flags, arv::protocol::EntityStateFlagHasHealth);
}

TEST(WebChunkMeshTests, CameraMatricesAreColumnMajorAndFiniteForWebGlUniforms) {
  const arv::Mat4f identity_view =
      arv::look_at_mat4f({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F});
  const arv::Mat4f projection =
      arv::perspective_mat4f(arv::degrees_to_radians(70.0F), 16.0F / 9.0F, 0.05F, 512.0F);
  const arv::Mat4f combined = arv::multiply_mat4f(projection, identity_view);

  expect_all_finite(identity_view);
  expect_all_finite(projection);
  expect_all_finite(combined);
  EXPECT_FLOAT_EQ(identity_view.values[0], 1.0F);
  EXPECT_FLOAT_EQ(identity_view.values[5], 1.0F);
  EXPECT_FLOAT_EQ(identity_view.values[10], -1.0F);
  EXPECT_FLOAT_EQ(identity_view.values[15], 1.0F);
  EXPECT_FLOAT_EQ(projection.values[11], -1.0F);
  expect_near_vec3(
      arv::transform_point(identity_view, {0.0F, 0.0F, 1.0F}),
      {0.0F, 0.0F, -1.0F});
  expect_near_vec3(
      arv::transform_point(identity_view, {1.0F, 0.0F, 0.0F}),
      {1.0F, 0.0F, 0.0F});
}

TEST(WebChunkMeshTests, MouseLookRotatesFirstPersonViewWithoutMovingEye) {
  const arv::Vec3f eye{8.0F, 2.62F, 8.0F};
  const float yaw = 1.2F;
  const float pitch = -0.35F;
  const arv::Vec3f forward = arv::view_forward(yaw, pitch);
  const arv::Mat4f view = arv::look_at_mat4f(
      eye,
      eye + forward,
      {0.0F, 1.0F, 0.0F});

  expect_near_vec3(arv::transform_point(view, eye), {0.0F, 0.0F, 0.0F});
  expect_near_vec3(
      arv::transform_point(view, eye + forward * 4.0F),
      {0.0F, 0.0F, -4.0F});

  const arv::Vec3f right = arv::horizontal_right(yaw);
  expect_near_vec3(
      arv::transform_point(view, eye + right),
      {1.0F, 0.0F, 0.0F});
}
