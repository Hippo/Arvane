#include "arv/render/chunk_mesher.hpp"

#include <array>

namespace arv::render {
namespace {

struct ChunkFaceGeometry {
  voxel::BlockFace face{};
  Vec3i neighbor_offset{};
  std::array<Vec3i, 4> corners{};
  std::array<u8, 6> indices{};
  std::array<u8, 6> flipped_indices{};
};

constexpr std::array<ChunkFaceGeometry, voxel::BlockFaceCount> ChunkCubeFaces{{
    {
        voxel::BlockFace::PositiveX,
        {1, 0, 0},
        {{{16, 0, 16}, {16, 0, 0}, {16, 16, 16}, {16, 16, 0}}},
        {{2, 1, 3, 1, 2, 0}},
        {{3, 2, 0, 0, 1, 3}},
    },
    {
        voxel::BlockFace::NegativeX,
        {-1, 0, 0},
        {{{0, 0, 0}, {0, 0, 16}, {0, 16, 0}, {0, 16, 16}}},
        {{3, 2, 0, 0, 1, 3}},
        {{2, 1, 3, 1, 2, 0}},
    },
    {
        voxel::BlockFace::PositiveY,
        {0, 1, 0},
        {{{0, 16, 16}, {16, 16, 16}, {0, 16, 0}, {16, 16, 0}}},
        {{2, 1, 3, 1, 2, 0}},
        {{3, 2, 0, 0, 1, 3}},
    },
    {
        voxel::BlockFace::NegativeY,
        {0, -1, 0},
        {{{16, 0, 16}, {0, 0, 16}, {16, 0, 0}, {0, 0, 0}}},
        {{3, 2, 0, 0, 1, 3}},
        {{2, 1, 3, 1, 2, 0}},
    },
    {
        voxel::BlockFace::PositiveZ,
        {0, 0, 1},
        {{{0, 0, 16}, {16, 0, 16}, {0, 16, 16}, {16, 16, 16}}},
        {{0, 1, 3, 3, 2, 0}},
        {{1, 2, 0, 2, 1, 3}},
    },
    {
        voxel::BlockFace::NegativeZ,
        {0, 0, -1},
        {{{0, 0, 0}, {16, 0, 0}, {0, 16, 0}, {16, 16, 0}}},
        {{0, 3, 1, 3, 0, 2}},
        {{1, 0, 2, 2, 3, 1}},
    },
}};

void emit_cube_face(
    CpuMeshBuffer& output,
    SectionLocalPos local,
    const ChunkFaceGeometry& face,
    std::array<Vec2i, 4> uvs,
    voxel::LightWord light,
    std::array<u8, 4> ao) {
  const u32 base_index = static_cast<u32>(chunk_vertex_count(output));

  append_chunk_vertex(output, make_chunk_vertex(
                                  local,
                                  face.corners[0],
                                  static_cast<u16>(uvs[0].x),
                                  static_cast<u16>(uvs[0].y),
                                  face.face,
                                  ao[0],
                                  voxel::light_sky(light),
                                  voxel::light_r(light),
                                  voxel::light_g(light),
                                  voxel::light_b(light)));
  append_chunk_vertex(output, make_chunk_vertex(
                                  local,
                                  face.corners[1],
                                  static_cast<u16>(uvs[1].x),
                                  static_cast<u16>(uvs[1].y),
                                  face.face,
                                  ao[1],
                                  voxel::light_sky(light),
                                  voxel::light_r(light),
                                  voxel::light_g(light),
                                  voxel::light_b(light)));
  append_chunk_vertex(output, make_chunk_vertex(
                                  local,
                                  face.corners[2],
                                  static_cast<u16>(uvs[2].x),
                                  static_cast<u16>(uvs[2].y),
                                  face.face,
                                  ao[2],
                                  voxel::light_sky(light),
                                  voxel::light_r(light),
                                  voxel::light_g(light),
                                  voxel::light_b(light)));
  append_chunk_vertex(output, make_chunk_vertex(
                                  local,
                                  face.corners[3],
                                  static_cast<u16>(uvs[3].x),
                                  static_cast<u16>(uvs[3].y),
                                  face.face,
                                  ao[3],
                                  voxel::light_sky(light),
                                  voxel::light_r(light),
                                  voxel::light_g(light),
                                  voxel::light_b(light)));

  const std::array<u8, 6>& face_indices = should_flip_voxel_ao_quad(ao)
                                              ? face.flipped_indices
                                              : face.indices;
  for (const u8 index : face_indices) {
    output.indices.push_back(base_index + index);
  }
}

[[nodiscard]] constexpr SectionLocalPos offset_local(SectionLocalPos local, Vec3i offset) noexcept {
  return {{local.value.x + offset.x, local.value.y + offset.y, local.value.z + offset.z}};
}

[[nodiscard]] bool is_ao_occluder(
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    SectionLocalPos local) noexcept {
  const voxel::BlockVariantRecord* variant = registry.variant(voxel::snapshot_block(snapshot, local));
  return variant != nullptr && variant->properties.ao_occluder;
}

[[nodiscard]] std::array<Vec3i, 3> corner_ao_offsets(
    const ChunkFaceGeometry& face,
    Vec3i corner) noexcept {
  const i32 x_offset = corner.x == 0 ? -1 : 1;
  const i32 y_offset = corner.y == 0 ? -1 : 1;
  const i32 z_offset = corner.z == 0 ? -1 : 1;

  switch (face.face) {
    case voxel::BlockFace::PositiveX:
    case voxel::BlockFace::NegativeX:
      return {{
          {face.neighbor_offset.x, y_offset, 0},
          {face.neighbor_offset.x, 0, z_offset},
          {face.neighbor_offset.x, y_offset, z_offset},
      }};
    case voxel::BlockFace::PositiveY:
    case voxel::BlockFace::NegativeY:
      return {{
          {x_offset, face.neighbor_offset.y, 0},
          {0, face.neighbor_offset.y, z_offset},
          {x_offset, face.neighbor_offset.y, z_offset},
      }};
    case voxel::BlockFace::PositiveZ:
    case voxel::BlockFace::NegativeZ:
      return {{
          {x_offset, 0, face.neighbor_offset.z},
          {0, y_offset, face.neighbor_offset.z},
          {x_offset, y_offset, face.neighbor_offset.z},
      }};
  }

  return {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
}

[[nodiscard]] u8 corner_ao(
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    SectionLocalPos local,
    const ChunkFaceGeometry& face,
    Vec3i corner) noexcept {
  const std::array<Vec3i, 3> samples = corner_ao_offsets(face, corner);
  const bool side0_occluded = is_ao_occluder(snapshot, registry, offset_local(local, samples[0]));
  const bool side1_occluded = is_ao_occluder(snapshot, registry, offset_local(local, samples[1]));
  const bool corner_occluded = is_ao_occluder(snapshot, registry, offset_local(local, samples[2]));
  return voxel_ambient_occlusion_value(side0_occluded, side1_occluded, corner_occluded);
}

[[nodiscard]] std::array<u8, 4> face_ao(
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    SectionLocalPos local,
    const ChunkFaceGeometry& face) noexcept {
  return {{
      corner_ao(snapshot, registry, local, face, face.corners[0]),
      corner_ao(snapshot, registry, local, face, face.corners[1]),
      corner_ao(snapshot, registry, local, face, face.corners[2]),
      corner_ao(snapshot, registry, local, face, face.corners[3]),
  }};
}

} // namespace

ChunkMeshData build_chunk_mesh(
    WorldID world_id,
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    const voxel::BlockRenderData* render_data) {
  ChunkMeshData mesh;
  mesh.world_id = world_id;
  mesh.coord = snapshot.coord;
  mesh.source_version = snapshot.source_version;
  mesh.source_content_version = snapshot.source_content_version;
  mesh.source_light_version = snapshot.source_light_version;

  mesh.opaque.vertex_bytes.reserve(voxel::ChunkVolume * 3U * sizeof(ChunkVertex));
  mesh.opaque.indices.reserve(voxel::ChunkVolume * 6U);

  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 y = 0; y < ChunkSize; ++y) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const SectionLocalPos local{{x, y, z}};
        const voxel::BlockVariantID self_id = snapshot.blocks()[voxel::local_index(local)];
        const voxel::BlockVariantRecord* self = registry.variant(self_id);
        if (self == nullptr || self->render_category == voxel::RenderCategory::None) {
          continue;
        }

        if (!self->properties.full_cube) {
          continue;
        }

        CpuMeshBuffer& output = self->render_category == voxel::RenderCategory::Transparent
                                    ? mesh.transparent
                                    : mesh.opaque;
        if (self->render_category == voxel::RenderCategory::Transparent &&
            output.vertex_bytes.empty()) {
          output.vertex_bytes.reserve(voxel::ChunkVolume * sizeof(ChunkVertex));
          output.indices.reserve(voxel::ChunkVolume * 6U);
        }

        for (const ChunkFaceGeometry& face : ChunkCubeFaces) {
          const SectionLocalPos neighbor_local = offset_local(local, face.neighbor_offset);
          const voxel::BlockVariantID neighbor_id = voxel::snapshot_block(snapshot, neighbor_local);
          if (registry.should_emit_face(self_id, neighbor_id, face.face)) {
            std::array<Vec2i, 4> uvs = voxel::default_cube_face_uvs(face.face);
            if (render_data != nullptr) {
              if (const voxel::BlockFaceRenderData* face_data = render_data->face(self_id, face.face);
                  face_data != nullptr) {
                uvs = face_data->uvs;
              }
            }
            emit_cube_face(
                output,
                local,
                face,
                uvs,
                voxel::snapshot_light(snapshot, neighbor_local),
                face_ao(snapshot, registry, local, face));
          }
        }
      }
    }
  }

  return mesh;
}

} // namespace arv::render
