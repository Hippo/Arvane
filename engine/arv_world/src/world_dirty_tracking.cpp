#include "world_runtime_internal.hpp"

#include "arv/world/world_lighting.hpp"

#include <algorithm>

namespace arv::world::detail {
namespace {

[[nodiscard]] ChunkSectionCoord offset_chunk(ChunkSectionCoord coord, Vec3i offset) noexcept {
  return {{
      coord.value.x + offset.x,
      coord.value.y + offset.y,
      coord.value.z + offset.z,
  }};
}

[[nodiscard]] int nonzero_axis_count(Vec3i offset) noexcept {
  return static_cast<int>(offset.x != 0) +
         static_cast<int>(offset.y != 0) +
         static_cast<int>(offset.z != 0);
}

} // namespace

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

void WorldDirtyTracker::recompute_lighting_work_pending(const WorldRuntime& world) {
  (void)dirty_lighting_chunks(world);
}

usize WorldDirtyTracker::last_lighting_snapshot_chunk_count() const noexcept {
  return last_lighting_snapshot_chunk_count_;
}

void WorldDirtyTracker::set_last_lighting_snapshot_chunk_count(usize count) const noexcept {
  last_lighting_snapshot_chunk_count_ = count;
}

std::vector<ChunkSectionCoord> WorldDirtyTracker::dirty_lighting_chunks(
    const WorldRuntime& world) {
  std::vector<ChunkSectionCoord> coords;
  coords.reserve(dirty_lighting_chunks_.size());
  for (auto iter = dirty_lighting_chunks_.begin(); iter != dirty_lighting_chunks_.end();) {
    const voxel::ChunkSection* chunk = world.find_chunk(*iter);
    if (chunk == nullptr || !chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting)) {
      iter = dirty_lighting_chunks_.erase(iter);
      continue;
    }
    coords.emplace_back(*iter);
    ++iter;
  }
  lighting_work_pending_ = !dirty_lighting_chunks_.empty();
  last_lighting_snapshot_chunk_count_ = dirty_lighting_chunks_.size();
  return coords;
}

usize WorldDirtyTracker::dirty_lighting_chunk_count() const noexcept {
  return dirty_lighting_chunks_.size();
}

void WorldDirtyTracker::mark_chunk_lighting_dirty(
    WorldRuntime& world,
    ChunkSectionCoord coord) noexcept {
  voxel::ChunkSection* chunk = world.find_chunk(coord);
  if (chunk == nullptr) {
    return;
  }
  chunk->mark_dirty(voxel::ChunkDirtyFlags::Lighting);
  dirty_lighting_chunks_.insert(coord);
  lighting_work_pending_ = true;
  last_lighting_snapshot_chunk_count_ =
      std::max<usize>(last_lighting_snapshot_chunk_count_, dirty_lighting_chunks_.size());
}

void WorldDirtyTracker::forget_lighting_chunk(ChunkSectionCoord coord) noexcept {
  dirty_lighting_chunks_.erase(coord);
  lighting_work_pending_ = !dirty_lighting_chunks_.empty();
  last_lighting_snapshot_chunk_count_ = dirty_lighting_chunks_.size();
}

void WorldDirtyTracker::enqueue_dirty_mesh_chunk(ChunkSectionCoord coord) {
  if (dirty_mesh_chunk_set_.insert(coord).second) {
    dirty_mesh_chunks_.emplace_back(coord);
  }
}

usize WorldDirtyTracker::dirty_mesh_chunk_count() const noexcept {
  return dirty_mesh_chunk_set_.size();
}

std::vector<ChunkSectionCoord> WorldDirtyTracker::dirty_chunks(
    const WorldRuntime& world,
    voxel::ChunkDirtyFlags flag) const {
  std::vector<ChunkSectionCoord> coords;
  for (const ChunkSectionCoord coord : world.loaded_chunk_coords()) {
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk != nullptr && chunk->is_dirty(flag)) {
      coords.emplace_back(coord);
    }
  }
  return coords;
}

std::vector<ChunkSectionCoord> WorldDirtyTracker::consume_dirty_mesh_chunks(
    const WorldRuntime& world,
    voxel::ChunkDirtyFlags flag) {
  std::vector<ChunkSectionCoord> coords;
  coords.reserve(dirty_mesh_chunks_.size());
  while (!dirty_mesh_chunks_.empty()) {
    const ChunkSectionCoord coord = dirty_mesh_chunks_.front();
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
    ChunkSectionCoord coord,
    SectionLocalPos local) noexcept {
  const i32 min_x = local.value.x == 0 ? -1 : 0;
  const i32 max_x = local.value.x == ChunkSize - 1 ? 1 : 0;
  const i32 min_y = local.value.y == 0 ? -1 : 0;
  const i32 max_y = local.value.y == ChunkSize - 1 ? 1 : 0;
  const i32 min_z = local.value.z == 0 ? -1 : 0;
  const i32 max_z = local.value.z == ChunkSize - 1 ? 1 : 0;

  for (usize index = 0; index < voxel::ChunkSnapshotNeighborhoodVolume; ++index) {
    const Vec3i offset = voxel::snapshot_storage_offset(index);
    if (offset.x == 0 && offset.y == 0 && offset.z == 0) {
      continue;
    }
    if (offset.x < min_x || offset.x > max_x ||
        offset.y < min_y || offset.y > max_y ||
        offset.z < min_z || offset.z > max_z) {
      continue;
    }

    const ChunkSectionCoord neighbor = offset_chunk(coord, offset);
    if (nonzero_axis_count(offset) == 1) {
      mark_neighbor_mesh_dependency_dirty(world, neighbor);
    } else {
      mark_neighbor_mesh_only_dependency_dirty(world, neighbor);
    }
  }
}

void WorldDirtyTracker::mark_loaded_chunk_neighbors_dirty(
    WorldRuntime& world,
    ChunkSectionCoord coord) noexcept {
  for (usize index = 0; index < voxel::ChunkSnapshotNeighborhoodVolume; ++index) {
    const Vec3i offset = voxel::snapshot_storage_offset(index);
    if (offset.x == 0 && offset.y == 0 && offset.z == 0) {
      continue;
    }

    const ChunkSectionCoord neighbor = offset_chunk(coord, offset);
    if (nonzero_axis_count(offset) == 1) {
      mark_neighbor_mesh_dependency_dirty(world, neighbor);
    } else {
      mark_neighbor_mesh_only_dependency_dirty(world, neighbor);
    }
  }
}

void WorldDirtyTracker::mark_loaded_chunk_lighting_dependencies_dirty(
    WorldRuntime& world,
    ChunkSectionCoord coord) noexcept {
  for (usize index = 0; index < voxel::ChunkSnapshotNeighborhoodVolume; ++index) {
    const Vec3i offset = voxel::snapshot_storage_offset(index);
    if (offset.x == 0 && offset.y == 0 && offset.z == 0) {
      continue;
    }

    mark_neighbor_lighting_dependency_dirty(world, offset_chunk(coord, offset));
  }
}

void WorldDirtyTracker::mark_light_dependency_neighbors_dirty(
    WorldRuntime& world,
    ChunkSectionCoord coord) noexcept {
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
    ChunkSectionCoord coord) noexcept {
  voxel::ChunkSection* neighbor = world.find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_mesh_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    neighbor->mark_lighting_dependency_dirty();
    dirty_lighting_chunks_.insert(coord);
    neighbor->mark_dirty(voxel::ChunkDirtyFlags::NeedsFluidStep);
    mark_mesh_content_dirty();
    lighting_work_pending_ = true;
    last_lighting_snapshot_chunk_count_ =
        std::max<usize>(last_lighting_snapshot_chunk_count_, dirty_lighting_chunks_.size());
  }
}

void WorldDirtyTracker::mark_neighbor_mesh_only_dependency_dirty(
    WorldRuntime& world,
    ChunkSectionCoord coord) noexcept {
  voxel::ChunkSection* neighbor = world.find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_mesh_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    mark_mesh_content_dirty();
  }
}

void WorldDirtyTracker::mark_neighbor_light_dependency_dirty(
    WorldRuntime& world,
    ChunkSectionCoord coord) noexcept {
  voxel::ChunkSection* neighbor = world.find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_light_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    mark_mesh_content_dirty();
  }
}

void WorldDirtyTracker::mark_neighbor_lighting_dependency_dirty(
    WorldRuntime& world,
    ChunkSectionCoord coord) noexcept {
  voxel::ChunkSection* neighbor = world.find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_lighting_dependency_dirty();
    dirty_lighting_chunks_.insert(coord);
    lighting_work_pending_ = true;
    last_lighting_snapshot_chunk_count_ =
        std::max<usize>(last_lighting_snapshot_chunk_count_, dirty_lighting_chunks_.size());
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

void WorldRuntime::mark_chunk_lighting_dirty(ChunkSectionCoord coord) noexcept {
  assert_owner_thread();
  impl_->dirty.mark_chunk_lighting_dirty(*this, coord);
}

std::vector<ChunkSectionCoord> WorldRuntime::dirty_chunks(voxel::ChunkDirtyFlags flag) const {
  return impl_->dirty.dirty_chunks(*this, flag);
}

std::vector<ChunkSectionCoord> WorldRuntime::consume_dirty_mesh_chunks(
    voxel::ChunkDirtyFlags flag) {
  assert_owner_thread();
  return impl_->dirty.consume_dirty_mesh_chunks(*this, flag);
}

} // namespace arv::world
