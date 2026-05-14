#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/voxel/storage.hpp"

#include <array>

namespace arv::world::detail {

using ChunkDependencyMask = std::array<bool, voxel::ChunkSnapshotNeighborhoodVolume>;

inline void add_dependency_offset(
    ChunkDependencyMask& mask,
    i32 x,
    i32 y,
    i32 z) noexcept {
  if (x == 0 && y == 0 && z == 0) {
    return;
  }
  mask[voxel::snapshot_storage_index(x, y, z)] = true;
}

inline void add_all_neighbor_dependency_offsets(ChunkDependencyMask& mask) noexcept {
  for (usize index = 0; index < voxel::ChunkSnapshotNeighborhoodVolume; ++index) {
    const Vec3i offset = voxel::snapshot_storage_offset(index);
    add_dependency_offset(mask, offset.x, offset.y, offset.z);
  }
}

inline void add_boundary_dependency_offsets(
    ChunkDependencyMask& mask,
    SectionLocalPos local) noexcept {
  const i32 min_x = local.value.x == 0 ? -1 : 0;
  const i32 max_x = local.value.x == ChunkSize - 1 ? 1 : 0;
  const i32 min_y = local.value.y == 0 ? -1 : 0;
  const i32 max_y = local.value.y == ChunkSize - 1 ? 1 : 0;
  const i32 min_z = local.value.z == 0 ? -1 : 0;
  const i32 max_z = local.value.z == ChunkSize - 1 ? 1 : 0;

  for (usize index = 0; index < voxel::ChunkSnapshotNeighborhoodVolume; ++index) {
    const Vec3i offset = voxel::snapshot_storage_offset(index);
    if (offset.x < min_x || offset.x > max_x ||
        offset.y < min_y || offset.y > max_y ||
        offset.z < min_z || offset.z > max_z) {
      continue;
    }
    add_dependency_offset(mask, offset.x, offset.y, offset.z);
  }
}

} // namespace arv::world::detail
