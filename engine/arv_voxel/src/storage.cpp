#include "arv/voxel/storage.hpp"

#include <utility>

namespace arv::voxel {

ChunkSection::ChunkSection(ChunkCoord coord) : coord_(coord) {
  blocks_.fill(AirBlockVariantID);
  lights_.fill(NoLight);
  fluids_.fill(empty_fluid_cell());
}

bool ChunkSection::set_block(SectionLocalPos local, BlockVariantID variant) noexcept {
  if (!is_valid_local(local)) {
    return false;
  }

  BlockVariantID& cell = blocks_[local_index(local)];
  if (cell == variant) {
    return false;
  }

  cell = variant;
  ++version_;
  mark_dirty(
      ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::NeedsMesh |
      ChunkDirtyFlags::NeedsRelight | ChunkDirtyFlags::NeedsFluidStep);
  return true;
}

bool ChunkSection::set_light(SectionLocalPos local, LightWord light) noexcept {
  if (!is_valid_local(local)) {
    return false;
  }

  LightWord& cell = lights_[local_index(local)];
  if (cell == light) {
    return false;
  }

  cell = light;
  ++version_;
  mark_dirty(ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::NeedsMesh);
  return true;
}

bool ChunkSection::set_fluid(SectionLocalPos local, FluidCell fluid, bool affects_lighting) noexcept {
  if (!is_valid_local(local)) {
    return false;
  }

  FluidCell& cell = fluids_[local_index(local)];
  if (cell == fluid) {
    return false;
  }

  cell = fluid;
  ++version_;
  ChunkDirtyFlags flags = ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::NeedsMesh |
                          ChunkDirtyFlags::NeedsFluidStep;
  if (affects_lighting) {
    flags |= ChunkDirtyFlags::NeedsRelight;
  }
  mark_dirty(flags);
  return true;
}

void ChunkSection::replace_generated_blocks(std::array<BlockVariantID, ChunkVolume> blocks) noexcept {
  blocks_ = std::move(blocks);
  lights_.fill(NoLight);
  fluids_.fill(empty_fluid_cell());
  ++version_;
  mark_dirty(ChunkDirtyFlags::NeedsMesh | ChunkDirtyFlags::NeedsRelight);
}

bool ChunkSection::replace_lights(std::array<LightWord, ChunkVolume> lights) noexcept {
  if (lights_ == lights) {
    return false;
  }

  lights_ = std::move(lights);
  ++version_;
  mark_dirty(ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::NeedsMesh);
  return true;
}

void ChunkSection::mark_dirty(ChunkDirtyFlags flags) noexcept {
  dirty_flags_ |= flags;
  if (has_flag(flags, ChunkDirtyFlags::NeedsMesh)) {
    mesh_state_ = ChunkMeshState::Dirty;
  }
}

void ChunkSection::mark_mesh_dependency_dirty() noexcept {
  ++version_;
  mark_dirty(ChunkDirtyFlags::NeedsMesh);
}

void ChunkSection::clear_dirty(ChunkDirtyFlags flags) noexcept {
  dirty_flags_ = static_cast<ChunkDirtyFlags>(static_cast<u32>(dirty_flags_) & ~static_cast<u32>(flags));
  if (!has_flag(dirty_flags_, ChunkDirtyFlags::NeedsMesh) && mesh_state_ == ChunkMeshState::Dirty) {
    mesh_state_ = ChunkMeshState::Clean;
  }
}

void ChunkSection::mark_mesh_ready(u64 published_version) noexcept {
  published_mesh_version_ = published_version;
  mesh_state_ = ChunkMeshState::Ready;
  clear_dirty(ChunkDirtyFlags::NeedsMesh);
}

ChunkBoundarySnapshot air_boundary_snapshot() {
  ChunkBoundarySnapshot snapshot;
  snapshot.blocks.fill(AirBlockVariantID);
  snapshot.lights.fill(NoLight);
  snapshot.available = true;
  return snapshot;
}

ChunkSnapshot make_chunk_snapshot_without_neighbors(const ChunkSection& chunk) {
  ChunkSnapshot snapshot;
  snapshot.coord = chunk.coord();
  snapshot.source_version = chunk.version();
  snapshot.blocks = chunk.blocks();
  snapshot.lights = chunk.lights();
  for (ChunkBoundarySnapshot& boundary : snapshot.neighbors) {
    boundary = air_boundary_snapshot();
  }
  snapshot.halo_blocks.fill(AirBlockVariantID);
  snapshot.halo_lights.fill(NoLight);
  return snapshot;
}

BlockVariantID snapshot_block(const ChunkSnapshot& snapshot, SectionLocalPos local) noexcept {
  if (is_valid_local(local)) {
    return snapshot.blocks[local_index(local)];
  }

  const bool out_x = local.value.x < 0 || local.value.x >= ChunkSize;
  const bool out_y = local.value.y < 0 || local.value.y >= ChunkSize;
  const bool out_z = local.value.z < 0 || local.value.z >= ChunkSize;
  const int out_count = static_cast<int>(out_x) + static_cast<int>(out_y) + static_cast<int>(out_z);
  if (out_count > 1) {
    return is_valid_halo_local(local) ? snapshot.halo_blocks[halo_index(local)] : AirBlockVariantID;
  }

  if (local.value.x < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeX)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::NegativeX, {{0, local.value.y, local.value.z}})]
               : AirBlockVariantID;
  }
  if (local.value.x >= ChunkSize) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveX)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::PositiveX, {{0, local.value.y, local.value.z}})]
               : AirBlockVariantID;
  }
  if (local.value.y < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeY)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::NegativeY, {{local.value.x, 0, local.value.z}})]
               : AirBlockVariantID;
  }
  if (local.value.y >= ChunkSize) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveY)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::PositiveY, {{local.value.x, 0, local.value.z}})]
               : AirBlockVariantID;
  }
  if (local.value.z < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeZ)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::NegativeZ, {{local.value.x, local.value.y, 0}})]
               : AirBlockVariantID;
  }

  const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveZ)];
  return boundary.available
             ? boundary.blocks[boundary_index_for_face(BlockFace::PositiveZ, {{local.value.x, local.value.y, 0}})]
             : AirBlockVariantID;
}

LightWord snapshot_light(const ChunkSnapshot& snapshot, SectionLocalPos local) noexcept {
  if (is_valid_local(local)) {
    return snapshot.lights[local_index(local)];
  }

  const bool out_x = local.value.x < 0 || local.value.x >= ChunkSize;
  const bool out_y = local.value.y < 0 || local.value.y >= ChunkSize;
  const bool out_z = local.value.z < 0 || local.value.z >= ChunkSize;
  const int out_count = static_cast<int>(out_x) + static_cast<int>(out_y) + static_cast<int>(out_z);
  if (out_count > 1) {
    return is_valid_halo_local(local) ? snapshot.halo_lights[halo_index(local)] : NoLight;
  }

  if (local.value.x < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeX)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::NegativeX, {{0, local.value.y, local.value.z}})]
               : NoLight;
  }
  if (local.value.x >= ChunkSize) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveX)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::PositiveX, {{0, local.value.y, local.value.z}})]
               : NoLight;
  }
  if (local.value.y < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeY)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::NegativeY, {{local.value.x, 0, local.value.z}})]
               : NoLight;
  }
  if (local.value.y >= ChunkSize) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveY)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::PositiveY, {{local.value.x, 0, local.value.z}})]
               : NoLight;
  }
  if (local.value.z < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeZ)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::NegativeZ, {{local.value.x, local.value.y, 0}})]
               : NoLight;
  }

  const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveZ)];
  return boundary.available
             ? boundary.lights[boundary_index_for_face(BlockFace::PositiveZ, {{local.value.x, local.value.y, 0}})]
             : NoLight;
}

} // namespace arv::voxel
