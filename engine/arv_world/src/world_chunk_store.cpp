#include "world_runtime_internal.hpp"

#include "arv/world/fluid_solver.hpp"

#include <algorithm>

namespace arv::world::detail {

voxel::ChunkSection& WorldChunkStore::ensure_chunk(
    ChunkCoord coord,
    voxel::ChunkLifecycleState state,
    WorldDirtyTracker& dirty) {
  auto [iter, inserted] = chunks_.try_emplace(coord, coord);
  if (inserted) {
    iter->second.set_lifecycle_state(state);
    ++lifecycle_counts_[lifecycle_index(state)];
    if (state == voxel::ChunkLifecycleState::Active) {
      ++active_chunk_count_;
    }
    dirty.enqueue_dirty_mesh_chunk(coord);
  }
  return iter->second;
}

void WorldChunkStore::request_chunk_load(ChunkCoord coord, WorldDirtyTracker& dirty) {
  if (find_chunk(coord) != nullptr) {
    return;
  }
  (void)ensure_chunk(coord, voxel::ChunkLifecycleState::Requested, dirty);
}

bool WorldChunkStore::unload_chunk(ChunkCoord coord) noexcept {
  const auto found = chunks_.find(coord);
  if (found == chunks_.end()) {
    return false;
  }

  const voxel::ChunkLifecycleState state = found->second.lifecycle_state();
  usize& state_count = lifecycle_counts_[lifecycle_index(state)];
  if (state_count > 0U) {
    --state_count;
  }
  if (state == voxel::ChunkLifecycleState::Active && active_chunk_count_ > 0U) {
    --active_chunk_count_;
  }
  chunks_.erase(found);
  return true;
}

voxel::ChunkSection* WorldChunkStore::find_chunk(ChunkCoord coord) noexcept {
  const auto found = chunks_.find(coord);
  return found == chunks_.end() ? nullptr : &found->second;
}

const voxel::ChunkSection* WorldChunkStore::find_chunk(ChunkCoord coord) const noexcept {
  const auto found = chunks_.find(coord);
  return found == chunks_.end() ? nullptr : &found->second;
}

usize WorldChunkStore::loaded_count() const noexcept {
  return chunks_.size();
}

usize WorldChunkStore::active_count() const noexcept {
  return active_chunk_count_;
}

bool WorldChunkStore::is_active(ChunkCoord coord) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  return chunk != nullptr && chunk->lifecycle_state() == voxel::ChunkLifecycleState::Active;
}

usize WorldChunkStore::lifecycle_count(voxel::ChunkLifecycleState state) const noexcept {
  return lifecycle_counts_[lifecycle_index(state)];
}

std::vector<ChunkCoord> WorldChunkStore::loaded_coords() const {
  std::vector<ChunkCoord> coords;
  coords.reserve(chunks_.size());
  for (const auto& [coord, chunk] : chunks_) {
    (void)chunk;
    coords.emplace_back(coord);
  }
  return coords;
}

ChunkWorkloadSnapshot WorldChunkStore::workload_snapshot(
    bool lighting_work_pending,
    usize last_lighting_snapshot_chunk_count,
    usize dirty_mesh_chunk_count) const noexcept {
  ChunkWorkloadSnapshot snapshot;
  snapshot.loaded_chunks = chunks_.size();
  snapshot.active_chunks = active_chunk_count_;
  snapshot.chunks_awaiting_generation =
      lifecycle_count(voxel::ChunkLifecycleState::Requested) +
      lifecycle_count(voxel::ChunkLifecycleState::Loading) +
      lifecycle_count(voxel::ChunkLifecycleState::Generating) +
      lifecycle_count(voxel::ChunkLifecycleState::Populating);
  snapshot.chunk_sections_awaiting_lighting =
      lighting_work_pending ? std::max<usize>(last_lighting_snapshot_chunk_count, 1U) : 0U;
  snapshot.chunk_sections_awaiting_meshing = dirty_mesh_chunk_count;
  return snapshot;
}

bool WorldChunkStore::set_lifecycle_state(
    ChunkCoord coord,
    voxel::ChunkLifecycleState state) noexcept {
  voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr) {
    return false;
  }
  set_lifecycle_state_internal(*chunk, state);
  return true;
}

void WorldChunkStore::set_lifecycle_state_internal(
    voxel::ChunkSection& chunk,
    voxel::ChunkLifecycleState state) noexcept {
  const voxel::ChunkLifecycleState previous = chunk.lifecycle_state();
  if (previous == state) {
    return;
  }

  usize& previous_count = lifecycle_counts_[lifecycle_index(previous)];
  if (previous_count > 0U) {
    --previous_count;
  }
  ++lifecycle_counts_[lifecycle_index(state)];

  if (previous == voxel::ChunkLifecycleState::Active && active_chunk_count_ > 0U) {
    --active_chunk_count_;
  }
  if (state == voxel::ChunkLifecycleState::Active) {
    ++active_chunk_count_;
  }

  chunk.set_lifecycle_state(state);
}

void WorldChunkStore::update_simulation_radius(
    WorldRuntime& world,
    ChunkCoord center,
    i32 radius) noexcept {
  const std::array<ChunkCoord, 1> centers{{center}};
  update_simulation_radii(world, centers, radius);
}

void WorldChunkStore::update_simulation_radii(
    WorldRuntime& world,
    std::span<const ChunkCoord> centers,
    i32 radius) noexcept {
  for (auto& [coord, chunk] : chunks_) {
    if (chunk.lifecycle_state() != voxel::ChunkLifecycleState::Loaded &&
        chunk.lifecycle_state() != voxel::ChunkLifecycleState::Active) {
      continue;
    }

    bool active = false;
    if (radius >= 0) {
      for (const ChunkCoord center : centers) {
        const i32 dx = coord.value.x - center.value.x;
        const i32 dy = coord.value.y - center.value.y;
        const i32 dz = coord.value.z - center.value.z;
        if (max_abs3(dx, dy, dz) <= radius) {
          active = true;
          break;
        }
      }
    }

    const voxel::ChunkLifecycleState next_state =
        active ? voxel::ChunkLifecycleState::Active : voxel::ChunkLifecycleState::Loaded;
    const bool became_active =
        active && chunk.lifecycle_state() != voxel::ChunkLifecycleState::Active;
    if (chunk.lifecycle_state() != next_state) {
      set_lifecycle_state_internal(chunk, next_state);
    }
    if (became_active) {
      FluidSolver::enqueue_chunk_activation_updates(world, coord);
    }
  }
}

} // namespace arv::world::detail

namespace arv::world {

voxel::ChunkSection& WorldRuntime::ensure_chunk(
    ChunkCoord coord,
    voxel::ChunkLifecycleState state) {
  return impl_->chunks.ensure_chunk(coord, state, impl_->dirty);
}

void WorldRuntime::request_chunk_load(ChunkCoord coord) {
  impl_->chunks.request_chunk_load(coord, impl_->dirty);
}

bool WorldRuntime::unload_chunk(ChunkCoord coord) noexcept {
  if (!impl_->chunks.unload_chunk(coord)) {
    return false;
  }

  impl_->dirty.mark_mesh_content_dirty();
  impl_->dirty.mark_loaded_chunk_neighbors_dirty(*this, coord);
  impl_->dirty.recompute_lighting_work_pending(*this);
  return true;
}

voxel::ChunkSection* WorldRuntime::find_chunk(ChunkCoord coord) noexcept {
  return impl_->chunks.find_chunk(coord);
}

const voxel::ChunkSection* WorldRuntime::find_chunk(ChunkCoord coord) const noexcept {
  return impl_->chunks.find_chunk(coord);
}

usize WorldRuntime::loaded_chunk_count() const noexcept {
  return impl_->chunks.loaded_count();
}

ChunkWorkloadSnapshot WorldRuntime::chunk_workload_snapshot() const noexcept {
  return impl_->chunks.workload_snapshot(
      impl_->dirty.has_lighting_work(),
      impl_->dirty.last_lighting_snapshot_chunk_count(),
      impl_->dirty.dirty_mesh_chunk_count());
}

std::vector<ChunkCoord> WorldRuntime::loaded_chunk_coords() const {
  return impl_->chunks.loaded_coords();
}

bool WorldRuntime::is_chunk_active(ChunkCoord coord) const noexcept {
  return impl_->chunks.is_active(coord);
}

usize WorldRuntime::active_chunk_count() const noexcept {
  return impl_->chunks.active_count();
}

bool WorldRuntime::set_chunk_lifecycle_state(
    ChunkCoord coord,
    voxel::ChunkLifecycleState state) noexcept {
  const bool changed = impl_->chunks.set_lifecycle_state(coord, state);
  if (changed && state == voxel::ChunkLifecycleState::Active) {
    FluidSolver::enqueue_chunk_activation_updates(*this, coord);
  }
  return changed;
}

void WorldRuntime::update_simulation_radius(ChunkCoord center, i32 radius) noexcept {
  impl_->chunks.update_simulation_radius(*this, center, radius);
}

void WorldRuntime::update_simulation_radii(
    std::span<const ChunkCoord> centers,
    i32 radius) noexcept {
  impl_->chunks.update_simulation_radii(*this, centers, radius);
}

} // namespace arv::world
