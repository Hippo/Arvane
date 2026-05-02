#include "world_runtime_internal.hpp"

#include "arv/world/world_lighting.hpp"

#include <algorithm>

namespace arv::world::detail {

u64 WorldDirtyTracker::lighting_revision() const noexcept {
  return lighting_revision_;
}

u64 WorldDirtyTracker::mesh_revision() const noexcept {
  return mesh_revision_;
}

void WorldDirtyTracker::mark_lighting_content_dirty() noexcept {
  ++lighting_revision_;
  lighting_work_pending_ = true;
  last_lighting_snapshot_chunk_count_ = std::max<usize>(last_lighting_snapshot_chunk_count_, 1U);
}

void WorldDirtyTracker::mark_mesh_content_dirty() noexcept {
  ++mesh_revision_;
}

bool WorldDirtyTracker::has_lighting_work() const noexcept {
  return lighting_work_pending_;
}

void WorldDirtyTracker::set_lighting_work_pending(bool pending) noexcept {
  lighting_work_pending_ = pending;
}

void WorldDirtyTracker::recompute_lighting_work_pending(const WorldRuntime& world) noexcept {
  lighting_work_pending_ = WorldLighting::has_work(world);
}

usize WorldDirtyTracker::last_lighting_snapshot_chunk_count() const noexcept {
  return last_lighting_snapshot_chunk_count_;
}

void WorldDirtyTracker::set_last_lighting_snapshot_chunk_count(usize count) const noexcept {
  last_lighting_snapshot_chunk_count_ = count;
}

void WorldDirtyTracker::enqueue_dirty_mesh_chunk(ChunkCoord coord) {
  if (dirty_mesh_chunk_set_.insert(coord).second) {
    dirty_mesh_chunks_.emplace_back(coord);
  }
}

usize WorldDirtyTracker::dirty_mesh_chunk_count() const noexcept {
  return dirty_mesh_chunk_set_.size();
}

std::vector<ChunkCoord> WorldDirtyTracker::dirty_chunks(
    const WorldRuntime& world,
    voxel::ChunkDirtyFlags flag) const {
  std::vector<ChunkCoord> coords;
  for (const ChunkCoord coord : world.loaded_chunk_coords()) {
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk != nullptr && chunk->is_dirty(flag)) {
      coords.emplace_back(coord);
    }
  }
  return coords;
}

std::vector<ChunkCoord> WorldDirtyTracker::consume_dirty_mesh_chunks(
    const WorldRuntime& world,
    voxel::ChunkDirtyFlags flag) {
  std::vector<ChunkCoord> coords;
  coords.reserve(dirty_mesh_chunks_.size());
  while (!dirty_mesh_chunks_.empty()) {
    const ChunkCoord coord = dirty_mesh_chunks_.front();
    dirty_mesh_chunks_.pop_front();
    dirty_mesh_chunk_set_.erase(coord);

    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk != nullptr && chunk->is_dirty(flag)) {
      coords.emplace_back(coord);
    }
  }
  return coords;
}

void WorldDirtyTracker::mark_boundary_neighbors_dirty(
    WorldRuntime& world,
    ChunkCoord coord,
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
        if (x == 0 && y == 0 && z == 0) {
          continue;
        }

        const ChunkCoord neighbor{{
            coord.value.x + x,
            coord.value.y + y,
            coord.value.z + z,
        }};
        const int axis_count =
            static_cast<int>(x != 0) + static_cast<int>(y != 0) + static_cast<int>(z != 0);
        if (axis_count == 1) {
          mark_neighbor_mesh_dependency_dirty(world, neighbor);
        } else {
          mark_neighbor_mesh_only_dependency_dirty(world, neighbor);
        }
      }
    }
  }
}

void WorldDirtyTracker::mark_loaded_chunk_neighbors_dirty(
    WorldRuntime& world,
    ChunkCoord coord) noexcept {
  for (i32 z = -1; z <= 1; ++z) {
    for (i32 y = -1; y <= 1; ++y) {
      for (i32 x = -1; x <= 1; ++x) {
        if (x == 0 && y == 0 && z == 0) {
          continue;
        }

        const ChunkCoord neighbor{{
            coord.value.x + x,
            coord.value.y + y,
            coord.value.z + z,
        }};
        const int axis_count =
            static_cast<int>(x != 0) + static_cast<int>(y != 0) + static_cast<int>(z != 0);
        if (axis_count == 1) {
          mark_neighbor_mesh_dependency_dirty(world, neighbor);
        } else {
          mark_neighbor_mesh_only_dependency_dirty(world, neighbor);
        }
      }
    }
  }
}

void WorldDirtyTracker::mark_light_dependency_neighbors_dirty(
    WorldRuntime& world,
    ChunkCoord coord) noexcept {
  for (const Vec3i& offset : NeighborOffsets) {
    mark_neighbor_light_dependency_dirty(
        world,
        {{
            coord.value.x + offset.x,
            coord.value.y + offset.y,
            coord.value.z + offset.z,
        }});
  }
}

void WorldDirtyTracker::mark_neighbor_mesh_dependency_dirty(
    WorldRuntime& world,
    ChunkCoord coord) noexcept {
  voxel::ChunkSection* neighbor = world.find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_mesh_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    neighbor->mark_dirty(voxel::ChunkDirtyFlags::Lighting | voxel::ChunkDirtyFlags::NeedsFluidStep);
    mark_mesh_content_dirty();
    lighting_work_pending_ = true;
  }
}

void WorldDirtyTracker::mark_neighbor_mesh_only_dependency_dirty(
    WorldRuntime& world,
    ChunkCoord coord) noexcept {
  voxel::ChunkSection* neighbor = world.find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_mesh_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    mark_mesh_content_dirty();
  }
}

void WorldDirtyTracker::mark_neighbor_light_dependency_dirty(
    WorldRuntime& world,
    ChunkCoord coord) noexcept {
  voxel::ChunkSection* neighbor = world.find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_light_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    mark_mesh_content_dirty();
  }
}

} // namespace arv::world::detail

namespace arv::world {

u64 WorldRuntime::lighting_revision() const noexcept {
  return impl_->dirty.lighting_revision();
}

u64 WorldRuntime::mesh_revision() const noexcept {
  return impl_->dirty.mesh_revision();
}

std::vector<ChunkCoord> WorldRuntime::dirty_chunks(voxel::ChunkDirtyFlags flag) const {
  return impl_->dirty.dirty_chunks(*this, flag);
}

std::vector<ChunkCoord> WorldRuntime::consume_dirty_mesh_chunks(
    voxel::ChunkDirtyFlags flag) {
  return impl_->dirty.consume_dirty_mesh_chunks(*this, flag);
}

} // namespace arv::world
