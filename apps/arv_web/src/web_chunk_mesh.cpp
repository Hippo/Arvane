#include "web_chunk_mesh.hpp"

#include "arv/voxel/block.hpp"
#include "arv/voxel/storage.hpp"

#include <algorithm>
#include <cmath>

namespace arv::web {
namespace {

[[nodiscard]] bool is_solid_block(
    const protocol::ClientboundChunkSectionData& section,
    int x,
    int y,
    int z) noexcept {
  if (x < 0 || x >= ChunkSize ||
      y < 0 || y >= ChunkSize ||
      z < 0 || z >= ChunkSize) {
    return false;
  }
  const usize index = voxel::local_index(SectionLocalPos{{x, y, z}});
  return section.blocks[index] != voxel::AirBlockVariantID;
}

[[nodiscard]] Vec3f color_for_block(voxel::BlockVariantID block, float shade) {
  u32 hash = block.value * 747796405U + 2891336453U;
  hash = ((hash >> ((hash >> 28U) + 4U)) ^ hash) * 277803737U;
  hash = (hash >> 22U) ^ hash;
  const float r = 0.30F + static_cast<float>(hash & 0xFFU) / 255.0F * 0.45F;
  const float g = 0.42F + static_cast<float>((hash >> 8U) & 0xFFU) / 255.0F * 0.42F;
  const float b = 0.28F + static_cast<float>((hash >> 16U) & 0xFFU) / 255.0F * 0.38F;
  return {
      std::clamp(r * shade, 0.0F, 1.0F),
      std::clamp(g * shade, 0.0F, 1.0F),
      std::clamp(b * shade, 0.0F, 1.0F)};
}

void append_vertex(
    std::vector<WebChunkVertex>& vertices,
    Vec3f position,
    Vec3f color) {
  vertices.push_back({position, color});
}

void append_face(
    std::vector<WebChunkVertex>& vertices,
    voxel::BlockFace face,
    float x,
    float y,
    float z,
    Vec3f color) {
  const float x1 = x + 1.0F;
  const float y1 = y + 1.0F;
  const float z1 = z + 1.0F;
  std::array<Vec3f, 4> corners{};
  switch (face) {
    case voxel::BlockFace::PositiveX:
      corners = {{{x1, y, z}, {x1, y1, z}, {x1, y1, z1}, {x1, y, z1}}};
      break;
    case voxel::BlockFace::NegativeX:
      corners = {{{x, y, z1}, {x, y1, z1}, {x, y1, z}, {x, y, z}}};
      break;
    case voxel::BlockFace::PositiveY:
      corners = {{{x, y1, z1}, {x1, y1, z1}, {x1, y1, z}, {x, y1, z}}};
      break;
    case voxel::BlockFace::NegativeY:
      corners = {{{x, y, z}, {x1, y, z}, {x1, y, z1}, {x, y, z1}}};
      break;
    case voxel::BlockFace::PositiveZ:
      corners = {{{x1, y, z1}, {x1, y1, z1}, {x, y1, z1}, {x, y, z1}}};
      break;
    case voxel::BlockFace::NegativeZ:
      corners = {{{x, y, z}, {x, y1, z}, {x1, y1, z}, {x1, y, z}}};
      break;
  }

  append_vertex(vertices, corners[0], color);
  append_vertex(vertices, corners[1], color);
  append_vertex(vertices, corners[2], color);
  append_vertex(vertices, corners[0], color);
  append_vertex(vertices, corners[2], color);
  append_vertex(vertices, corners[3], color);
}

void append_box_face(
    std::vector<WebChunkVertex>& vertices,
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
  if ((marker.state_flags & protocol::EntityStateFlagDead) != 0U) {
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

bool same_chunk_section_coord(ChunkSectionCoord lhs, ChunkSectionCoord rhs) noexcept {
  return lhs.value.x == rhs.value.x &&
         lhs.value.y == rhs.value.y &&
         lhs.value.z == rhs.value.z;
}

std::vector<WebChunkVertex> build_fallback_chunk_vertices(
    const protocol::ClientboundChunkSectionData& section) {
  std::vector<WebChunkVertex> vertices;
  vertices.reserve(4096U);
  const float base_x = static_cast<float>(section.coord.value.x * ChunkSize);
  const float base_y = static_cast<float>(section.coord.value.y * ChunkSize);
  const float base_z = static_cast<float>(section.coord.value.z * ChunkSize);
  for (int z = 0; z < ChunkSize; ++z) {
    for (int y = 0; y < ChunkSize; ++y) {
      for (int x = 0; x < ChunkSize; ++x) {
        const usize index = voxel::local_index(SectionLocalPos{{x, y, z}});
        const voxel::BlockVariantID block = section.blocks[index];
        if (block == voxel::AirBlockVariantID) {
          continue;
        }
        const float world_x = base_x + static_cast<float>(x);
        const float world_y = base_y + static_cast<float>(y);
        const float world_z = base_z + static_cast<float>(z);
        for (voxel::BlockFace face : voxel::AllBlockFaces) {
          const Vec3i offset = voxel::block_face_offset(face);
          if (is_solid_block(section, x + offset.x, y + offset.y, z + offset.z)) {
            continue;
          }
          const float shade = face == voxel::BlockFace::PositiveY
                                  ? 1.15F
                                  : (face == voxel::BlockFace::NegativeY ? 0.65F : 0.88F);
          append_face(vertices, face, world_x, world_y, world_z, color_for_block(block, shade));
        }
      }
    }
  }
  return vertices;
}

std::vector<WebChunkVertex> build_fallback_entity_vertices(
    std::span<const WebEntityMarker> markers) {
  std::vector<WebChunkVertex> vertices;
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
    std::span<const client::ClientSessionEntityMirror> mirrors,
    const std::optional<client::ClientSessionWorldJoin>& join) {
  std::vector<WebEntityMarker> markers;
  markers.reserve(mirrors.size());
  for (const client::ClientSessionEntityMirror& mirror : mirrors) {
    if (join.has_value()) {
      if (mirror.world_id != join->world_id) {
        continue;
      }
      const bool is_local_player =
          mirror.entity_id == join->local_entity_id ||
          (mirror.owning_player.has_value() &&
           mirror.owning_player.value() == join->player_id);
      if (is_local_player) {
        continue;
      }
    }
    markers.push_back({mirror.position, mirror.entity_type, mirror.state_flags});
  }
  return markers;
}

Mat4 multiply(Mat4 lhs, Mat4 rhs) noexcept {
  Mat4 result{};
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      for (int k = 0; k < 4; ++k) {
        result.values[static_cast<usize>(column * 4 + row)] +=
            lhs.values[static_cast<usize>(k * 4 + row)] *
            rhs.values[static_cast<usize>(column * 4 + k)];
      }
    }
  }
  return result;
}

Mat4 perspective(float fov_y_radians, float aspect, float near_z, float far_z) noexcept {
  const float f = 1.0F / std::tan(fov_y_radians * 0.5F);
  Mat4 result{};
  result.values[0] = f / std::max(aspect, 0.001F);
  result.values[5] = f;
  result.values[10] = (far_z + near_z) / (near_z - far_z);
  result.values[11] = -1.0F;
  result.values[14] = (2.0F * far_z * near_z) / (near_z - far_z);
  return result;
}

Mat4 look_at(Vec3f eye, Vec3f center, Vec3f up) noexcept {
  const Vec3f forward = normalize_or_zero(center - eye);
  const Vec3f side = normalize_or_zero(cross(up, forward));
  const Vec3f camera_up = cross(forward, side);
  return {{
      side.x, camera_up.x, -forward.x, 0.0F,
      side.y, camera_up.y, -forward.y, 0.0F,
      side.z, camera_up.z, -forward.z, 0.0F,
      -dot(side, eye), -dot(camera_up, eye), dot(forward, eye), 1.0F}};
}

} // namespace arv::web
