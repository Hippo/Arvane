#include "web_entity_fallback.hpp"

#include "arv/voxel/block.hpp"

#include <algorithm>
#include <array>

namespace arv::web {
namespace {

void append_vertex(
    std::vector<WebFallbackColorVertex>& vertices,
    Vec3f position,
    Vec3f color) {
  vertices.push_back({position, color});
}

void append_box_face(
    std::vector<WebFallbackColorVertex>& vertices,
    voxel::BlockFace face,
    Vec3f min,
    Vec3f max,
    Vec3f color) {
  std::array<Vec3f, 4> corners{};
  switch (face) {
    case voxel::BlockFace::PositiveX:
      corners = {{{max.x, min.y, min.z}, {max.x, max.y, min.z}, {max.x, max.y, max.z}, {max.x, min.y, max.z}}};
      break;
    case voxel::BlockFace::NegativeX:
      corners = {{{min.x, min.y, max.z}, {min.x, max.y, max.z}, {min.x, max.y, min.z}, {min.x, min.y, min.z}}};
      break;
    case voxel::BlockFace::PositiveY:
      corners = {{{min.x, max.y, max.z}, {max.x, max.y, max.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z}}};
      break;
    case voxel::BlockFace::NegativeY:
      corners = {{{min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, min.y, max.z}, {min.x, min.y, max.z}}};
      break;
    case voxel::BlockFace::PositiveZ:
      corners = {{{max.x, min.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z}, {min.x, min.y, max.z}}};
      break;
    case voxel::BlockFace::NegativeZ:
      corners = {{{min.x, min.y, min.z}, {min.x, max.y, min.z}, {max.x, max.y, min.z}, {max.x, min.y, min.z}}};
      break;
  }

  append_vertex(vertices, corners[0], color);
  append_vertex(vertices, corners[1], color);
  append_vertex(vertices, corners[2], color);
  append_vertex(vertices, corners[0], color);
  append_vertex(vertices, corners[2], color);
  append_vertex(vertices, corners[3], color);
}

[[nodiscard]] Vec3f color_for_entity(WebEntityMarker marker) {
  if (marker.dead) {
    return {0.45F, 0.12F, 0.14F};
  }
  u32 hash = marker.entity_type.value * 2654435761U + 1013904223U;
  hash ^= hash >> 16U;
  const float r = 0.55F + static_cast<float>(hash & 0x7FU) / 255.0F;
  const float g = 0.45F + static_cast<float>((hash >> 8U) & 0x7FU) / 255.0F;
  const float b = 0.35F + static_cast<float>((hash >> 16U) & 0x7FU) / 255.0F;
  return {
      std::clamp(r, 0.0F, 1.0F),
      std::clamp(g, 0.0F, 1.0F),
      std::clamp(b, 0.0F, 1.0F)};
}

} // namespace

std::vector<WebFallbackColorVertex> build_fallback_entity_vertices(
    std::span<const WebEntityMarker> markers) {
  std::vector<WebFallbackColorVertex> vertices;
  vertices.reserve(markers.size() * 36U);
  for (WebEntityMarker marker : markers) {
    const Vec3f min{
        marker.position.x - 0.3F,
        marker.position.y,
        marker.position.z - 0.3F};
    const Vec3f max{
        marker.position.x + 0.3F,
        marker.position.y + 1.8F,
        marker.position.z + 0.3F};
    const Vec3f color = color_for_entity(marker);
    for (voxel::BlockFace face : voxel::AllBlockFaces) {
      append_box_face(vertices, face, min, max, color);
    }
  }
  return vertices;
}

std::vector<WebEntityMarker> build_visible_fallback_entity_markers(
    std::span<const client::ReplicatedEntityMirror> mirrors,
    WorldID world_id,
    std::optional<EntityNetID> local_entity_id,
    std::optional<PlayerID> local_player_id) {
  std::vector<WebEntityMarker> markers;
  markers.reserve(mirrors.size());
  for (const client::ReplicatedEntityMirror& mirror : mirrors) {
    if (mirror.world_id != world_id) {
      continue;
    }
    const bool is_local_player =
        (local_entity_id.has_value() && mirror.entity_id == local_entity_id.value()) ||
        (local_player_id.has_value() && mirror.owning_player.has_value() &&
         mirror.owning_player.value() == local_player_id.value());
    if (is_local_player) {
      continue;
    }
    markers.push_back({mirror.render_position, mirror.entity_type, mirror.dead});
  }
  return markers;
}

} // namespace arv::web
