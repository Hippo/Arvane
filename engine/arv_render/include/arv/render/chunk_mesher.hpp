#pragma once

#include "arv/render/render_types.hpp"
#include "arv/voxel/storage.hpp"

#include <array>

namespace arv::render {

namespace detail {

struct ChunkFaceGeometry {
  voxel::BlockFace face{};
  Vec3i neighbor_offset{};
  std::array<Vec3i, 4> corners{};
};

inline constexpr std::array<ChunkFaceGeometry, voxel::BlockFaceCount> ChunkCubeFaces{{
    {
        voxel::BlockFace::PositiveX,
        {1, 0, 0},
        {{{16, 0, 0}, {16, 16, 0}, {16, 16, 16}, {16, 0, 16}}},
    },
    {
        voxel::BlockFace::NegativeX,
        {-1, 0, 0},
        {{{0, 0, 16}, {0, 16, 16}, {0, 16, 0}, {0, 0, 0}}},
    },
    {
        voxel::BlockFace::PositiveY,
        {0, 1, 0},
        {{{0, 16, 16}, {16, 16, 16}, {16, 16, 0}, {0, 16, 0}}},
    },
    {
        voxel::BlockFace::NegativeY,
        {0, -1, 0},
        {{{0, 0, 0}, {16, 0, 0}, {16, 0, 16}, {0, 0, 16}}},
    },
    {
        voxel::BlockFace::PositiveZ,
        {0, 0, 1},
        {{{16, 0, 16}, {16, 16, 16}, {0, 16, 16}, {0, 0, 16}}},
    },
    {
        voxel::BlockFace::NegativeZ,
        {0, 0, -1},
        {{{0, 0, 0}, {0, 16, 0}, {16, 16, 0}, {16, 0, 0}}},
    },
}};

inline void emit_cube_face(
    CpuMeshBuffer& output,
    SectionLocalPos local,
    const ChunkFaceGeometry& face,
    voxel::LightWord light) {
  const u32 base_index = static_cast<u32>(chunk_vertex_count(output));

  // TODO(arvane):
  // Replace placeholder atlas texel coordinates with finalized block atlas UVs
  // once the asset/resource pipeline owns terrain atlas metadata.
  append_chunk_vertex(output, make_chunk_vertex(
      local,
      face.corners[0],
      0,
      0,
      face.face,
      0,
      voxel::light_sky(light),
      voxel::light_r(light),
      voxel::light_g(light),
      voxel::light_b(light)));
  append_chunk_vertex(output, make_chunk_vertex(
      local,
      face.corners[1],
      0,
      16,
      face.face,
      0,
      voxel::light_sky(light),
      voxel::light_r(light),
      voxel::light_g(light),
      voxel::light_b(light)));
  append_chunk_vertex(output, make_chunk_vertex(
      local,
      face.corners[2],
      16,
      16,
      face.face,
      0,
      voxel::light_sky(light),
      voxel::light_r(light),
      voxel::light_g(light),
      voxel::light_b(light)));
  append_chunk_vertex(output, make_chunk_vertex(
      local,
      face.corners[3],
      16,
      0,
      face.face,
      0,
      voxel::light_sky(light),
      voxel::light_r(light),
      voxel::light_g(light),
      voxel::light_b(light)));

  output.indices.push_back(base_index + 0);
  output.indices.push_back(base_index + 1);
  output.indices.push_back(base_index + 2);
  output.indices.push_back(base_index + 0);
  output.indices.push_back(base_index + 2);
  output.indices.push_back(base_index + 3);
}

[[nodiscard]] constexpr SectionLocalPos offset_local(SectionLocalPos local, Vec3i offset) noexcept {
  return {{local.value.x + offset.x, local.value.y + offset.y, local.value.z + offset.z}};
}

} // namespace detail

[[nodiscard]] inline ChunkMeshData build_chunk_mesh(
    WorldID world_id,
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry) {
  ChunkMeshData mesh;
  mesh.world_id = world_id;
  mesh.coord = snapshot.coord;
  mesh.source_version = snapshot.source_version;

  mesh.opaque.vertex_bytes.reserve(voxel::ChunkVolume * 3U * sizeof(ChunkVertex));
  mesh.opaque.indices.reserve(voxel::ChunkVolume * 6U);

  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 y = 0; y < ChunkSize; ++y) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const SectionLocalPos local{{x, y, z}};
        const voxel::BlockVariantID self_id = snapshot.blocks[voxel::local_index(local)];
        const voxel::BlockVariantRecord* self = registry.variant(self_id);
        if (self == nullptr || self->render_category == voxel::RenderCategory::None) {
          continue;
        }

        // TODO(arvane):
        // Custom/partial model geometry is intentionally not guessed here.
        // The model-format and asset pipeline need to provide authored geometry
        // descriptors before non-full-cube states can emit correct surfaces.
        if (!self->properties.full_cube) {
          continue;
        }

        CpuMeshBuffer& output = self->render_category == voxel::RenderCategory::Transparent
                                    ? mesh.transparent
                                    : mesh.opaque;

        for (const detail::ChunkFaceGeometry& face : detail::ChunkCubeFaces) {
          const SectionLocalPos neighbor_local = detail::offset_local(local, face.neighbor_offset);
          const voxel::BlockVariantID neighbor_id = voxel::snapshot_block(snapshot, neighbor_local);
          if (registry.should_emit_face(self_id, neighbor_id, face.face)) {
            detail::emit_cube_face(output, local, face, voxel::snapshot_light(snapshot, neighbor_local));
          }
        }
      }
    }
  }

  return mesh;
}

} // namespace arv::render
