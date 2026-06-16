#include "arv/render/chunk_mesher.hpp"

#include "arv/render/block_model_geometry.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

namespace arv::render {
namespace {

struct ChunkFaceGeometry {
  voxel::BlockFace face{};
  Vec3i neighbor_offset{};
  std::array<Vec3i, 4> corners{};
  std::array<u8, 6> indices{};
  std::array<u8, 6> flipped_indices{};
};

struct FaceTextureReference {
  u32 page{};
  std::array<Vec2i, 4> uvs{};
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

constexpr std::array<voxel::BlockFace, 4> HorizontalFluidFaces{{
    voxel::BlockFace::PositiveX,
    voxel::BlockFace::NegativeX,
    voxel::BlockFace::PositiveZ,
    voxel::BlockFace::NegativeZ,
}};

struct MeshReserveHints {
  std::atomic<usize> opaque_vertex_bytes{voxel::ChunkVolume * 3U * sizeof(ChunkVertex)};
  std::atomic<usize> opaque_indices{voxel::ChunkVolume * 6U};
  std::atomic<usize> transparent_vertex_bytes{voxel::ChunkVolume * sizeof(ChunkVertex)};
  std::atomic<usize> transparent_indices{voxel::ChunkVolume * 6U};
};

MeshReserveHints& mesh_reserve_hints() {
  static MeshReserveHints hints;
  return hints;
}

void update_high_water(std::atomic<usize>& high_water, usize value) noexcept {
  usize current = high_water.load(std::memory_order_relaxed);
  while (value > current &&
         !high_water.compare_exchange_weak(
             current,
             value,
             std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

[[nodiscard]] const ChunkFaceGeometry& face_geometry(voxel::BlockFace face) noexcept {
  return ChunkCubeFaces[voxel::face_index(face)];
}

void ensure_transparent_capacity(CpuMeshBuffer& output) {
  if (!output.vertex_bytes.empty() || !output.indices.empty()) {
    return;
  }
  const MeshReserveHints& hints = mesh_reserve_hints();
  output.vertex_bytes.reserve(
      hints.transparent_vertex_bytes.load(std::memory_order_relaxed));
  output.indices.reserve(hints.transparent_indices.load(std::memory_order_relaxed));
}

void emit_quad(
    CpuMeshBuffer& output,
    SectionLocalPos local,
    voxel::BlockFace block_face,
    const std::array<Vec3i, 4>& corners,
    const std::array<u8, 6>& indices,
    const std::array<u8, 6>& flipped_indices,
    FaceTextureReference texture,
    std::array<voxel::LightWord, 4> lights,
    std::array<u8, 4> ao,
    VertexMaterialHints material_hints) {
  const u32 base_index = static_cast<u32>(chunk_vertex_count(output));

  for (usize index = 0; index < corners.size(); ++index) {
    const voxel::LightWord light = lights[index];
    append_chunk_vertex(output, make_chunk_vertex(
                                    local,
                                    corners[index],
                                    static_cast<u16>(texture.uvs[index].x),
                                    static_cast<u16>(texture.uvs[index].y),
                                    static_cast<u16>(texture.page),
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
    output.indices.emplace_back(base_index + index);
  }
}

void emit_cube_face(
    CpuMeshBuffer& output,
    SectionLocalPos local,
    const ChunkFaceGeometry& face,
    FaceTextureReference texture,
    std::array<voxel::LightWord, 4> lights,
    std::array<u8, 4> ao,
    VertexMaterialHints material_hints) {
  emit_quad(
      output,
      local,
      face.face,
      face.corners,
      face.indices,
      face.flipped_indices,
      texture,
      lights,
      ao,
      material_hints);
}

[[nodiscard]] bool box_face_is_on_block_boundary(
    const voxel::BlockModelBoxRenderData& box,
    voxel::BlockFace face) noexcept {
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return box.max.x == 16;
    case voxel::BlockFace::NegativeX:
      return box.min.x == 0;
    case voxel::BlockFace::PositiveY:
      return box.max.y == 16;
    case voxel::BlockFace::NegativeY:
      return box.min.y == 0;
    case voxel::BlockFace::PositiveZ:
      return box.max.z == 16;
    case voxel::BlockFace::NegativeZ:
      return box.min.z == 0;
  }
  return false;
}

[[nodiscard]] constexpr SectionLocalPos offset_local(SectionLocalPos local, Vec3i offset) noexcept {
  return {{local.value.x + offset.x, local.value.y + offset.y, local.value.z + offset.z}};
}

[[nodiscard]] bool is_ao_occluder(
    const voxel::ChunkSnapshotStoragePointers& snapshot,
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

[[nodiscard]] i32 horizontal_model_light_offset(i32 value) noexcept {
  return value == 0 ? -1 : 1;
}

[[nodiscard]] i32 vertical_model_light_offset(i32 value) noexcept {
  if (value == 0) {
    return -1;
  }
  if (value == 16) {
    return 1;
  }
  return 0;
}

[[nodiscard]] std::array<Vec3i, 3> corner_light_offsets(
    const ChunkFaceGeometry& face,
    Vec3i corner) noexcept {
  const i32 x_offset = horizontal_model_light_offset(corner.x);
  const i32 y_offset = vertical_model_light_offset(corner.y);
  const i32 z_offset = horizontal_model_light_offset(corner.z);

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
          {x_offset, y_offset, 0},
          {0, y_offset, z_offset},
          {x_offset, y_offset, z_offset},
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

[[nodiscard]] voxel::LightWord average_corner_light(
    const voxel::ChunkSnapshotStoragePointers& snapshot,
    SectionLocalPos local,
    const ChunkFaceGeometry& face,
    Vec3i corner,
    voxel::LightWord face_light) noexcept {
  u32 r = voxel::light_r(face_light);
  u32 g = voxel::light_g(face_light);
  u32 b = voxel::light_b(face_light);
  u32 sky = voxel::light_sky(face_light);
  u32 count = 1U;

  for (const Vec3i offset : corner_light_offsets(face, corner)) {
    const voxel::LightWord sample =
        voxel::snapshot_light(snapshot, offset_local(local, offset));
    r += voxel::light_r(sample);
    g += voxel::light_g(sample);
    b += voxel::light_b(sample);
    sky += voxel::light_sky(sample);
    if (sample != voxel::NoLight) {
      ++count;
    }
  }

  return voxel::pack_light(
      static_cast<u8>(r / count),
      static_cast<u8>(g / count),
      static_cast<u8>(b / count),
      static_cast<u8>(sky / count));
}

[[nodiscard]] std::array<voxel::LightWord, 4> smooth_face_lights(
    const voxel::ChunkSnapshotStoragePointers& snapshot,
    SectionLocalPos local,
    const ChunkFaceGeometry& face,
    const std::array<Vec3i, 4>& corners,
    voxel::LightWord face_light) noexcept {
  return {{
      average_corner_light(snapshot, local, face, corners[0], face_light),
      average_corner_light(snapshot, local, face, corners[1], face_light),
      average_corner_light(snapshot, local, face, corners[2], face_light),
      average_corner_light(snapshot, local, face, corners[3], face_light),
  }};
}

[[nodiscard]] u8 corner_ao(
    const voxel::ChunkSnapshotStoragePointers& snapshot,
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
    const voxel::ChunkSnapshotStoragePointers& snapshot,
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
    const voxel::ChunkSnapshotStoragePointers& snapshot,
    const voxel::BlockRegistry& registry,
    SectionLocalPos neighbor_local,
    voxel::BlockFace face) noexcept {
  const voxel::BlockVariantRecord* neighbor =
      registry.variant(voxel::snapshot_block(snapshot, neighbor_local));
  return neighbor != nullptr &&
         neighbor->occludes_face[voxel::face_index(voxel::opposite_face(face))];
}

[[nodiscard]] bool current_block_allows_fluid_render(
    const voxel::ChunkSnapshotStoragePointers& snapshot,
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
    const voxel::ChunkSnapshotStoragePointers& snapshot,
    SectionLocalPos local,
    voxel::FluidCell fluid) noexcept {
  if (same_fluid_type(fluid, voxel::snapshot_fluid(snapshot, offset_local(local, {0, 1, 0})))) {
    return 16U;
  }
  return fluid_cell_height(fluid);
}

[[nodiscard]] u8 fluid_corner_height(
    const voxel::ChunkSnapshotStoragePointers& snapshot,
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
    const voxel::ChunkSnapshotStoragePointers& snapshot,
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
    const voxel::ChunkSnapshotStoragePointers& snapshot,
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

[[nodiscard]] FaceTextureReference face_texture_for(
    voxel::BlockVariantID variant_id,
    voxel::BlockFace face,
    const voxel::BlockRenderData* render_data) noexcept {
  FaceTextureReference texture;
  texture.uvs = voxel::default_cube_face_uvs(face);
  if (render_data != nullptr) {
    if (const voxel::BlockFaceRenderData* face_data = render_data->face(variant_id, face);
        face_data != nullptr) {
      texture.page = face_data->page;
      texture.uvs = face_data->uvs;
    }
  }
  return texture;
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
  if (variant.render_tint == voxel::BlockRenderTint::Foliage) {
    hints.material_class = VertexMaterialClass::Foliage;
    hints.material_flags |= VertexMaterialFlag_TintFromBiome;
  }
  return hints;
}

[[nodiscard]] bool should_emit_model_boundary_face(
    const voxel::BlockRegistry& registry,
    const voxel::BlockVariantRecord& self,
    voxel::BlockVariantID self_id,
    voxel::BlockVariantID neighbor_id,
    voxel::BlockFace face) noexcept {
  if (self.render_category == voxel::RenderCategory::None) {
    return false;
  }

  if (self_id == neighbor_id && self.hide_same_variant_faces) {
    return false;
  }

  const voxel::BlockVariantRecord* neighbor = registry.variant(neighbor_id);
  if (neighbor == nullptr) {
    return true;
  }

  return !neighbor->occludes_face[voxel::face_index(voxel::opposite_face(face))];
}

void emit_model_boxes(
    CpuMeshBuffer& output,
    const voxel::ChunkSnapshotStoragePointers& snapshot,
    const voxel::BlockRegistry& registry,
    const voxel::BlockRenderData& render_data,
    voxel::BlockVariantID self_id,
    const voxel::BlockVariantRecord& self,
    SectionLocalPos local,
    VertexMaterialHints material_hints) {
  const std::span<const voxel::BlockModelBoxRenderData> boxes =
      render_data.model_boxes(self_id);
  for (const voxel::BlockModelBoxRenderData& box : boxes) {
    for (const voxel::BlockFace block_face : voxel::AllBlockFaces) {
      const usize face_slot = voxel::face_index(block_face);
      if (!box.faces[face_slot].has_value()) {
        continue;
      }

      const ChunkFaceGeometry& cube_face = face_geometry(block_face);
      const bool boundary_face = box_face_is_on_block_boundary(box, block_face);
      if (boundary_face) {
        const SectionLocalPos neighbor_local = offset_local(local, cube_face.neighbor_offset);
        const voxel::BlockVariantID neighbor_id =
            voxel::snapshot_block(snapshot, neighbor_local);
        if (!should_emit_model_boundary_face(
                registry,
                self,
                self_id,
                neighbor_id,
                block_face)) {
          continue;
        }
      }

      ChunkFaceGeometry model_face = cube_face;
      model_face.corners = block_model_box_face_corners(box, block_face);
      if (!boundary_face) {
        model_face.neighbor_offset = {};
      }

      const SectionLocalPos light_local =
          boundary_face ? offset_local(local, cube_face.neighbor_offset) : local;
      const voxel::LightWord face_light = voxel::snapshot_light(snapshot, light_local);
      emit_quad(
          output,
          local,
          block_face,
          model_face.corners,
          model_face.indices,
          model_face.flipped_indices,
          {box.faces[face_slot]->page, box.faces[face_slot]->uvs},
          smooth_face_lights(
              snapshot,
              local,
              model_face,
              model_face.corners,
              face_light),
          face_ao(snapshot, registry, local, model_face),
          material_hints);
    }
  }
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
    const voxel::ChunkSnapshotStoragePointers& snapshot,
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
    const std::array<Vec3i, 4> top_corners = fluid_top_corners(snapshot, local, fluid);
    const voxel::LightWord face_light = voxel::snapshot_light(snapshot, above);
    emit_quad(
        output,
        local,
        voxel::BlockFace::PositiveY,
        top_corners,
        top_face.indices,
        top_face.flipped_indices,
        face_texture_for(water_variant, voxel::BlockFace::PositiveY, render_data),
        smooth_face_lights(snapshot, local, top_face, top_corners, face_light),
        FluidAo,
        water_material_hints(fluid, voxel::BlockFace::PositiveY));
  }

  for (const voxel::BlockFace face : HorizontalFluidFaces) {
    const ChunkFaceGeometry& geometry = face_geometry(face);
    const SectionLocalPos neighbor = offset_local(local, geometry.neighbor_offset);
    if (same_fluid_type(fluid, voxel::snapshot_fluid(snapshot, neighbor)) ||
        block_occludes_fluid_face(snapshot, registry, neighbor, face)) {
      continue;
    }
    const std::array<Vec3i, 4> corners = fluid_side_corners(snapshot, local, fluid, face);
    const voxel::LightWord face_light = voxel::snapshot_light(snapshot, neighbor);
    emit_quad(
        output,
        local,
        face,
        corners,
        geometry.indices,
        geometry.flipped_indices,
        face_texture_for(water_variant, face, render_data),
        smooth_face_lights(snapshot, local, geometry, corners, face_light),
        FluidAo,
        water_material_hints(fluid, face));
  }

  const SectionLocalPos below = offset_local(local, {0, -1, 0});
  if (!same_fluid_type(fluid, voxel::snapshot_fluid(snapshot, below)) &&
      !block_occludes_fluid_face(snapshot, registry, below, voxel::BlockFace::NegativeY)) {
    const ChunkFaceGeometry& bottom_face = face_geometry(voxel::BlockFace::NegativeY);
    const voxel::LightWord face_light = voxel::snapshot_light(snapshot, below);
    emit_quad(
        output,
        local,
        voxel::BlockFace::NegativeY,
        bottom_face.corners,
        bottom_face.indices,
        bottom_face.flipped_indices,
        face_texture_for(water_variant, voxel::BlockFace::NegativeY, render_data),
        smooth_face_lights(snapshot, local, bottom_face, bottom_face.corners, face_light),
        FluidAo,
        water_material_hints(fluid, voxel::BlockFace::NegativeY));
  }
}

} // namespace

void FluidRenderData::set_fluid_variant(
    voxel::FluidTypeID fluid_type,
    voxel::BlockVariantID variant) {
  for (auto& [existing_type, existing_variant] : variants_) {
    if (existing_type == fluid_type) {
      existing_variant = variant;
      return;
    }
  }
  variants_.push_back({fluid_type, variant});
}

std::optional<voxel::BlockVariantID> FluidRenderData::variant_for(
    voxel::FluidTypeID fluid_type) const noexcept {
  for (const auto& [existing_type, variant] : variants_) {
    if (existing_type == fluid_type) {
      return variant;
    }
  }
  return std::nullopt;
}

ChunkMeshData build_chunk_mesh(
    WorldID world_id,
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    const voxel::BlockRenderData* render_data,
    const FluidRenderData* fluid_render_data) {
  ARV_PROFILE_ZONE("Chunk meshing");
  std::array<char, core::PerfEventContextCapacity> profile_context{};
  if constexpr (core::perf_stats_enabled()) {
    (void)std::snprintf(
        profile_context.data(),
        profile_context.size(),
        "section=(%d,%d,%d)",
        snapshot.coord.value.x,
        snapshot.coord.value.y,
        snapshot.coord.value.z);
  }
  const core::ScopedThreadTiming timing{
      core::PerfMetric::MeshingTask,
      "worker/client",
      profile_context.data()};
  const voxel::ChunkSnapshotStoragePointers snapshot_storage =
      voxel::make_chunk_snapshot_storage_pointers(snapshot);

  ChunkMeshData mesh;
  mesh.world_id = world_id;
  mesh.coord = snapshot.coord;
  mesh.source_version = snapshot.source_version;
  mesh.source_content_version = snapshot.source_content_version;
  mesh.source_light_version = snapshot.source_light_version;

  const MeshReserveHints& hints = mesh_reserve_hints();
  mesh.opaque.vertex_bytes.reserve(
      hints.opaque_vertex_bytes.load(std::memory_order_relaxed));
  mesh.opaque.indices.reserve(hints.opaque_indices.load(std::memory_order_relaxed));
  const auto& center_blocks = snapshot.blocks();
  const auto& center_fluids = snapshot.fluids();

  for (usize cell_index = 0; cell_index < voxel::ChunkVolume; ++cell_index) {
    const SectionLocalPos local = voxel::local_pos(cell_index);
    const voxel::FluidCell fluid = center_fluids[cell_index];
    if (!fluid.empty() && fluid_render_data != nullptr) {
      if (const std::optional<voxel::BlockVariantID> fluid_variant =
              fluid_render_data->variant_for(fluid.type);
          fluid_variant.has_value()) {
        emit_fluid_cell(
            mesh.transparent,
            snapshot_storage,
            registry,
            render_data,
            fluid_variant.value(),
            local,
            fluid);
      }
    }

    const voxel::BlockVariantID self_id = center_blocks[cell_index];
    const voxel::BlockVariantRecord* self = registry.variant(self_id);
    if (self == nullptr || self->render_category == voxel::RenderCategory::None) {
      continue;
    }
    if (self->render_mode == voxel::BlockRenderMode::BlockEntityModel) {
      continue;
    }

    CpuMeshBuffer& output = self->render_category == voxel::RenderCategory::Transparent
                                ? mesh.transparent
                                : mesh.opaque;
    if (self->render_category == voxel::RenderCategory::Transparent) {
      ensure_transparent_capacity(output);
    }
    const VertexMaterialHints material_hints = material_hints_for_block(*self);

    if (!self->model.asset.empty()) {
      if (render_data != nullptr) {
        emit_model_boxes(
            output,
            snapshot_storage,
            registry,
            *render_data,
            self_id,
            *self,
            local,
            material_hints);
      }
      continue;
    }

    if (!self->properties.full_cube) {
      continue;
    }

    for (const ChunkFaceGeometry& face : ChunkCubeFaces) {
      const SectionLocalPos neighbor_local = offset_local(local, face.neighbor_offset);
      const voxel::BlockVariantID neighbor_id =
          voxel::snapshot_block(snapshot_storage, neighbor_local);
      if (registry.should_emit_face(self_id, neighbor_id, face.face)) {
        const voxel::LightWord face_light =
            voxel::snapshot_light(snapshot_storage, neighbor_local);
        emit_cube_face(
            output,
            local,
            face,
            face_texture_for(self_id, face.face, render_data),
            smooth_face_lights(
                snapshot_storage,
                local,
                face,
                face.corners,
                face_light),
            face_ao(snapshot_storage, registry, local, face),
            material_hints);
      }
    }
  }

  update_high_water(mesh_reserve_hints().opaque_vertex_bytes, mesh.opaque.vertex_bytes.size());
  update_high_water(mesh_reserve_hints().opaque_indices, mesh.opaque.indices.size());
  update_high_water(
      mesh_reserve_hints().transparent_vertex_bytes,
      mesh.transparent.vertex_bytes.size());
  update_high_water(mesh_reserve_hints().transparent_indices, mesh.transparent.indices.size());
  return mesh;
}

} // namespace arv::render
