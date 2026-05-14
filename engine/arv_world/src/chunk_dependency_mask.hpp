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
  for (i32 z = -1; z <= 1; ++z) {
    for (i32 y = -1; y <= 1; ++y) {
      for (i32 x = -1; x <= 1; ++x) {
        add_dependency_offset(mask, x, y, z);
      }
    }
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

  for (i32 z = min_z; z <= max_z; ++z) {
    for (i32 y = min_y; y <= max_y; ++y) {
      for (i32 x = min_x; x <= max_x; ++x) {
        add_dependency_offset(mask, x, y, z);
      }
    }
  }
}

} // namespace arv::world::detail
