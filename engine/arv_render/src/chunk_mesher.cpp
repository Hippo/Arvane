#include "arv/render/chunk_mesher.hpp"

#include <algorithm>
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

[[nodiscard]] const ChunkFaceGeometry& face_geometry(voxel::BlockFace face) noexcept {
  return ChunkCubeFaces[voxel::face_index(face)];
}

void ensure_transparent_capacity(CpuMeshBuffer& output) {
  if (!output.vertex_bytes.empty() || !output.indices.empty()) {
    return;
  }
  output.vertex_bytes.reserve(voxel::ChunkVolume * sizeof(ChunkVertex));
  output.indices.reserve(voxel::ChunkVolume * 6U);
}

void emit_quad(
    CpuMeshBuffer& output,
    SectionLocalPos local,
    voxel::BlockFace block_face,
    const std::array<Vec3i, 4>& corners,
    const std::array<u8, 6>& indices,
    const std::array<u8, 6>& flipped_indices,
    std::array<Vec2i, 4> uvs,
    voxel::LightWord light,
    std::array<u8, 4> ao,
    VertexMaterialHints material_hints) {
  const u32 base_index = static_cast<u32>(chunk_vertex_count(output));

  for (usize index = 0; index < corners.size(); ++index) {
    append_chunk_vertex(output, make_chunk_vertex(
                                    local,
                                    corners[index],
                                    static_cast<u16>(uvs[index].x),
                                    static_cast<u16>(uvs[index].y),
                                    block_face,
                                    ao[index],
                                    voxel::light_sky(light),
                                    voxel::light_r(light),
                                    voxel::light_g(light),
                                    voxel::light_b(light),
                                    material_hints));
  }

  const std::array<u8, 6>& face_indices = should_flip_voxel_ao_quad(ao)
                                              ? flipped_indices
                                              : indices;
  for (const u8 index : face_indices) {
    output.indices.push_back(base_index + index);
  }
}

void emit_cube_face(
    CpuMeshBuffer& output,
    SectionLocalPos local,
    const ChunkFaceGeometry& face,
    std::array<Vec2i, 4> uvs,
    voxel::LightWord light,
    std::array<u8, 4> ao,
    VertexMaterialHints material_hints) {
  emit_quad(
      output,
      local,
      face.face,
      face.corners,
      face.indices,
      face.flipped_indices,
      uvs,
      light,
      ao,
      material_hints);
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

[[nodiscard]] bool same_fluid_type(voxel::FluidCell lhs, voxel::FluidCell rhs) noexcept {
  return !lhs.empty() && !rhs.empty() && lhs.type == rhs.type;
}

[[nodiscard]] bool block_occludes_fluid_face(
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    SectionLocalPos neighbor_local,
    voxel::BlockFace face) noexcept {
  const voxel::BlockVariantRecord* neighbor =
      registry.variant(voxel::snapshot_block(snapshot, neighbor_local));
  return neighbor != nullptr &&
         neighbor->occludes_face[voxel::face_index(voxel::opposite_face(face))];
}

[[nodiscard]] bool current_block_allows_fluid_render(
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    SectionLocalPos local) noexcept {
  const voxel::BlockVariantID block = voxel::snapshot_block(snapshot, local);
  const voxel::BlockVariantRecord* variant = registry.variant(block);
  return variant != nullptr && (registry.is_air(block) || variant->properties.replaceable);
}

[[nodiscard]] u8 fluid_cell_height(voxel::FluidCell fluid) noexcept {
  if (fluid.empty()) {
    return 0;
  }
  const u32 level = fluid.source() ? 8U : fluid.level;
  return static_cast<u8>(std::clamp<u32>(2U + level * 12U / 8U, 2U, 14U));
}

[[nodiscard]] u8 fluid_height_with_above(
    const voxel::ChunkSnapshot& snapshot,
    SectionLocalPos local,
    voxel::FluidCell fluid) noexcept {
  if (same_fluid_type(fluid, voxel::snapshot_fluid(snapshot, offset_local(local, {0, 1, 0})))) {
    return 16U;
  }
  return fluid_cell_height(fluid);
}

[[nodiscard]] u8 fluid_corner_height(
    const voxel::ChunkSnapshot& snapshot,
    SectionLocalPos local,
    voxel::FluidCell self,
    Vec3i corner) noexcept {
  const i32 x_offset = corner.x == 0 ? -1 : 1;
  const i32 z_offset = corner.z == 0 ? -1 : 1;
  const std::array<SectionLocalPos, 4> samples{{
      local,
      offset_local(local, {x_offset, 0, 0}),
      offset_local(local, {0, 0, z_offset}),
      offset_local(local, {x_offset, 0, z_offset}),
  }};

  u32 height_sum = 0;
  u32 height_count = 0;
  for (const SectionLocalPos sample : samples) {
    const voxel::FluidCell fluid = voxel::snapshot_fluid(snapshot, sample);
    if (!same_fluid_type(self, fluid)) {
      continue;
    }
    const u8 height = fluid_height_with_above(snapshot, sample, fluid);
    if (height == 16U) {
      return 16U;
    }
    height_sum += height;
    ++height_count;
  }

  if (height_count == 0U) {
    return fluid_height_with_above(snapshot, local, self);
  }
  return static_cast<u8>(height_sum / height_count);
}

[[nodiscard]] std::array<Vec3i, 4> fluid_top_corners(
    const voxel::ChunkSnapshot& snapshot,
    SectionLocalPos local,
    voxel::FluidCell fluid) noexcept {
  const std::array<Vec3i, 4> corners{{
      {0, 0, 16},
      {16, 0, 16},
      {0, 0, 0},
      {16, 0, 0},
  }};
  std::array<Vec3i, 4> result = corners;
  for (usize index = 0; index < result.size(); ++index) {
    result[index].y = fluid_corner_height(snapshot, local, fluid, corners[index]);
  }
  return result;
}

[[nodiscard]] std::array<Vec3i, 4> fluid_side_corners(
    const voxel::ChunkSnapshot& snapshot,
    SectionLocalPos local,
    voxel::FluidCell fluid,
    voxel::BlockFace face) noexcept {
  const std::array<Vec3i, 4> top = fluid_top_corners(snapshot, local, fluid);
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return {{{16, 0, 16}, {16, 0, 0}, top[1], top[3]}};
    case voxel::BlockFace::NegativeX:
      return {{{0, 0, 0}, {0, 0, 16}, top[2], top[0]}};
    case voxel::BlockFace::PositiveZ:
      return {{{0, 0, 16}, {16, 0, 16}, top[0], top[1]}};
    case voxel::BlockFace::NegativeZ:
      return {{{0, 0, 0}, {16, 0, 0}, top[2], top[3]}};
    case voxel::BlockFace::PositiveY:
    case voxel::BlockFace::NegativeY:
      return face_geometry(face).corners;
  }
  return face_geometry(face).corners;
}

[[nodiscard]] std::array<Vec2i, 4> face_uvs_for(
    voxel::BlockVariantID variant_id,
    voxel::BlockFace face,
    const voxel::BlockRenderData* render_data) noexcept {
  std::array<Vec2i, 4> uvs = voxel::default_cube_face_uvs(face);
  if (render_data != nullptr) {
    if (const voxel::BlockFaceRenderData* face_data = render_data->face(variant_id, face);
        face_data != nullptr) {
      uvs = face_data->uvs;
    }
  }
  return uvs;
}

[[nodiscard]] VertexMaterialHints material_hints_for_block(
    const voxel::BlockVariantRecord& variant) noexcept {
  VertexMaterialHints hints;
  switch (variant.render_category) {
    case voxel::RenderCategory::None:
    case voxel::RenderCategory::Opaque:
      hints.material_class = VertexMaterialClass::Opaque;
      break;
    case voxel::RenderCategory::Cutout:
      hints.material_class = VertexMaterialClass::Cutout;
      break;
    case voxel::RenderCategory::Transparent:
      hints.material_class = VertexMaterialClass::Transparent;
      break;
  }
  return hints;
}

[[nodiscard]] VertexAnimationId water_animation_id(
    voxel::FluidCell fluid,
    voxel::BlockFace face) noexcept {
  if (face == voxel::BlockFace::PositiveY) {
    return VertexAnimationId::WaterSurface;
  }
  if (fluid.falling() || !fluid.source()) {
    return VertexAnimationId::WaterFlow;
  }
  return VertexAnimationId::WaterStill;
}

[[nodiscard]] VertexMaterialHints water_material_hints(
    voxel::FluidCell fluid,
    voxel::BlockFace face) noexcept {
  return {
      VertexMaterialClass::Water,
      water_animation_id(fluid, face),
      VertexMaterialFlag_TintFromFluid | VertexMaterialFlag_DisableAo};
}

void emit_fluid_cell(
    CpuMeshBuffer& output,
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    const voxel::BlockRenderData* render_data,
    voxel::BlockVariantID water_variant,
    SectionLocalPos local,
    voxel::FluidCell fluid) {
  if (fluid.empty() || water_variant == voxel::AirBlockVariantID ||
      !current_block_allows_fluid_render(snapshot, registry, local)) {
    return;
  }

  ensure_transparent_capacity(output);
  constexpr std::array<u8, 4> FluidAo{{3, 3, 3, 3}};

  const SectionLocalPos above = offset_local(local, {0, 1, 0});
  if (!same_fluid_type(fluid, voxel::snapshot_fluid(snapshot, above)) &&
      !block_occludes_fluid_face(snapshot, registry, above, voxel::BlockFace::PositiveY)) {
    const ChunkFaceGeometry& top_face = face_geometry(voxel::BlockFace::PositiveY);
    emit_quad(
        output,
        local,
        voxel::BlockFace::PositiveY,
        fluid_top_corners(snapshot, local, fluid),
        top_face.indices,
        top_face.flipped_indices,
        face_uvs_for(water_variant, voxel::BlockFace::PositiveY, render_data),
        voxel::snapshot_light(snapshot, above),
        FluidAo,
        water_material_hints(fluid, voxel::BlockFace::PositiveY));
  }

  for (const voxel::BlockFace face : {
           voxel::BlockFace::PositiveX,
           voxel::BlockFace::NegativeX,
           voxel::BlockFace::PositiveZ,
           voxel::BlockFace::NegativeZ,
       }) {
    const ChunkFaceGeometry& geometry = face_geometry(face);
    const SectionLocalPos neighbor = offset_local(local, geometry.neighbor_offset);
    if (same_fluid_type(fluid, voxel::snapshot_fluid(snapshot, neighbor)) ||
        block_occludes_fluid_face(snapshot, registry, neighbor, face)) {
      continue;
    }
    emit_quad(
        output,
        local,
        face,
        fluid_side_corners(snapshot, local, fluid, face),
        geometry.indices,
        geometry.flipped_indices,
        face_uvs_for(water_variant, face, render_data),
        voxel::snapshot_light(snapshot, neighbor),
        FluidAo,
        water_material_hints(fluid, face));
  }

  const SectionLocalPos below = offset_local(local, {0, -1, 0});
  if (!same_fluid_type(fluid, voxel::snapshot_fluid(snapshot, below)) &&
      !block_occludes_fluid_face(snapshot, registry, below, voxel::BlockFace::NegativeY)) {
    const ChunkFaceGeometry& bottom_face = face_geometry(voxel::BlockFace::NegativeY);
    emit_quad(
        output,
        local,
        voxel::BlockFace::NegativeY,
        bottom_face.corners,
        bottom_face.indices,
        bottom_face.flipped_indices,
        face_uvs_for(water_variant, voxel::BlockFace::NegativeY, render_data),
        voxel::snapshot_light(snapshot, below),
        FluidAo,
        water_material_hints(fluid, voxel::BlockFace::NegativeY));
  }
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
  const voxel::BlockVariantID water_variant =
      registry.find_variant("arvane:water").value_or(voxel::AirBlockVariantID);

  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 y = 0; y < ChunkSize; ++y) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const SectionLocalPos local{{x, y, z}};
        emit_fluid_cell(
            mesh.transparent,
            snapshot,
            registry,
            render_data,
            water_variant,
            local,
            snapshot.fluids()[voxel::local_index(local)]);

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
        if (self->render_category == voxel::RenderCategory::Transparent) {
          ensure_transparent_capacity(output);
        }
        const VertexMaterialHints material_hints = material_hints_for_block(*self);

        for (const ChunkFaceGeometry& face : ChunkCubeFaces) {
          const SectionLocalPos neighbor_local = offset_local(local, face.neighbor_offset);
          const voxel::BlockVariantID neighbor_id = voxel::snapshot_block(snapshot, neighbor_local);
          if (registry.should_emit_face(self_id, neighbor_id, face.face)) {
            emit_cube_face(
                output,
                local,
                face,
                face_uvs_for(self_id, face.face, render_data),
                voxel::snapshot_light(snapshot, neighbor_local),
                face_ao(snapshot, registry, local, face),
                material_hints);
          }
        }
      }
    }
  }

  return mesh;
}

} // namespace arv::render
