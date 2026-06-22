#include "world_runtime_internal.hpp"

#include "arv/world/fluid_solver.hpp"

#include <algorithm>
#include <cassert>

#include <arv/core/log.hpp>

namespace arv::world {

Chunk::Chunk(ChunkCoord coord, WorldHeightSettings height)
    : coord_(coord),
      height_(normalize_world_height_settings(height)),
      sections_(height_.section_count()) {
}

std::vector<voxel::ChunkLifecycleState> Chunk::configure_height(WorldHeightSettings settings) {
  settings = normalize_world_height_settings(settings);      
  if (settings.min_chunk_y == height_.min_chunk_y &&
      settings.max_chunk_y == height_.max_chunk_y) {
    return {};
  }

  std::vector<std::unique_ptr<voxel::ChunkSection>> next_sections(settings.section_count());
  std::vector<voxel::ChunkLifecycleState> removed_states;
  for (usize old_index = 0; old_index < sections_.size(); ++old_index) {
    std::unique_ptr<voxel::ChunkSection>& section = sections_[old_index];
    if (!section) {
      continue;
    }

    const i32 section_y = height_.min_chunk_y + static_cast<i32>(old_index);
    const std::optional<usize> next_index = settings.section_index(section_y);
    if (next_index.has_value()) {
      next_sections[next_index.value()] = std::move(section);
    } else {
      removed_states.emplace_back(section->lifecycle_state());
      section.reset();
    }
  }

  height_ = settings;
  sections_ = std::move(next_sections);
  return removed_states;
}

voxel::ChunkSection* Chunk::find_section(i32 chunk_y) noexcept {
  const std::optional<usize> index = section_index(chunk_y);
  if (!index.has_value()) {
    return nullptr;
  }
  const std::unique_ptr<voxel::ChunkSection>& section = sections_[index.value()];
  return section.get();
}

const voxel::ChunkSection* Chunk::find_section(i32 chunk_y) const noexcept {
  const std::optional<usize> index = section_index(chunk_y);
  if (!index.has_value()) {
    return nullptr;
  }
  const std::unique_ptr<voxel::ChunkSection>& section = sections_[index.value()];
  return section.get();
}

voxel::ChunkSection* Chunk::find_section(ChunkSectionCoord coord) noexcept {
  if (coord.value.x != coord_.x || coord.value.z != coord_.z) {
    return nullptr;
  }
  return find_section(coord.value.y);
}

const voxel::ChunkSection* Chunk::find_section(ChunkSectionCoord coord) const noexcept {
  if (coord.value.x != coord_.x || coord.value.z != coord_.z) {
    return nullptr;
  }
  return find_section(coord.value.y);
}

voxel::ChunkSection& Chunk::ensure_section(
    i32 chunk_y,
    voxel::ChunkLifecycleState state) {
  const std::optional<usize> index = section_index(chunk_y);
  assert(index.has_value() && "chunk section y outside world height settings");
  std::unique_ptr<voxel::ChunkSection>& section = sections_[index.value()];
  if (!section) {
    section = std::make_unique<voxel::ChunkSection>(
        ChunkSectionCoord{{coord_.x, chunk_y, coord_.z}});
    section->set_lifecycle_state(state);
  }
  return *section;
}

voxel::ChunkSection& Chunk::ensure_section(
    ChunkSectionCoord coord,
    voxel::ChunkLifecycleState state) {
  assert(coord.value.x == coord_.x && coord.value.z == coord_.z);
  return ensure_section(coord.value.y, state);
}

bool Chunk::unload_section(i32 chunk_y) noexcept {
  const std::optional<usize> index = section_index(chunk_y);
  if (!index.has_value() || !sections_[index.value()]) {
    return false;
  }
  sections_[index.value()].reset();
  return true;
}

usize Chunk::loaded_section_count() const noexcept {
  usize count = 0;
  for (const auto& section : sections_) {
    if (section) {
      ++count;
    }
  }
  return count;
}

std::vector<ChunkSectionCoord> Chunk::loaded_section_coords() const {
  std::vector<ChunkSectionCoord> coords;
  coords.reserve(loaded_section_count());
  for (i32 y = height_.min_chunk_y; y <= height_.max_chunk_y; ++y) {
    if (find_section(y) != nullptr) {
      coords.emplace_back(ChunkSectionCoord{{coord_.x, y, coord_.z}});
    }
  }
  return coords;
}

void Chunk::set_generation_surface_profile(GenerationSurfaceProfile profile) {
  generation_surface_profile_ = profile;
}

void Chunk::clear_generation_surface_profile() noexcept {
  generation_surface_profile_.reset();
}

const GenerationSurfaceProfile* Chunk::generation_surface_profile() const noexcept {
  return generation_surface_profile_ ? &generation_surface_profile_.value() : nullptr;
}

} // namespace arv::world

namespace arv::world::detail {
namespace {

[[nodiscard]] bool simulation_membership_state(
    voxel::ChunkLifecycleState state) noexcept {
  return state == voxel::ChunkLifecycleState::Loaded ||
         state == voxel::ChunkLifecycleState::Active;
}

} // namespace

void WorldChunkStore::note_simulation_topology_changed() noexcept {
  ++simulation_topology_revision_;
  has_cached_simulation_activity_ = false;
}

void WorldChunkStore::configure_height(WorldHeightSettings settings) {
  height_ = normalize_world_height_settings(settings);

  std::vector<ChunkCoord> empty_columns;
  bool removed_any = false;
  for (auto& [coord, chunk] : chunks_) {
    const std::vector<voxel::ChunkLifecycleState> removed_states =
        chunk.configure_height(height_);
    removed_any = removed_any || !removed_states.empty();
    for (const voxel::ChunkLifecycleState state : removed_states) {
      usize& state_count = lifecycle_counts_[lifecycle_index(state)];
      if (state_count > 0U) {
        --state_count;
      }
      if (state == voxel::ChunkLifecycleState::Active && active_chunk_count_ > 0U) {
        --active_chunk_count_;
      }
    }
    if (chunk.loaded_section_count() == 0U) {
      empty_columns.emplace_back(coord);
    }
  }
  for (const ChunkCoord coord : empty_columns) {
    chunks_.erase(coord);
  }
  if (removed_any) {
    note_simulation_topology_changed();
  }
}

WorldHeightSettings WorldChunkStore::height_settings() const noexcept {
  return height_;
}

Chunk& WorldChunkStore::ensure_column(ChunkCoord coord) {
  auto [iter, inserted] = chunks_.try_emplace(coord, coord, height_);
  (void)inserted;
  return iter->second;
}

Chunk* WorldChunkStore::find_column(ChunkCoord coord) noexcept {
  const auto found = chunks_.find(coord);
  return found == chunks_.end() ? nullptr : &found->second;
}

const Chunk* WorldChunkStore::find_column(ChunkCoord coord) const noexcept {
  const auto found = chunks_.find(coord);
  return found == chunks_.end() ? nullptr : &found->second;
}

voxel::ChunkSection& WorldChunkStore::ensure_chunk(
    ChunkSectionCoord coord,
    voxel::ChunkLifecycleState state,
    WorldDirtyTracker& dirty) {
  Chunk& column = ensure_column(chunk_to_column(coord));
  voxel::ChunkSection* existing = column.find_section(coord);
  voxel::ChunkSection& section = column.ensure_section(coord, state);
  if (existing == nullptr) {
    ++lifecycle_counts_[lifecycle_index(state)];
    if (state == voxel::ChunkLifecycleState::Active) {
      ++active_chunk_count_;
    }
    note_simulation_topology_changed();
    dirty.enqueue_dirty_mesh_chunk(coord);
  }
  return section;
}

void WorldChunkStore::request_chunk_load(ChunkSectionCoord coord, WorldDirtyTracker& dirty) {
  (void)dirty;
  if (find_chunk(coord) != nullptr) {
    return;
  }
  Chunk& column = ensure_column(chunk_to_column(coord));
  (void)column.ensure_section(coord, voxel::ChunkLifecycleState::Requested);
  ++lifecycle_counts_[lifecycle_index(voxel::ChunkLifecycleState::Requested)];
  note_simulation_topology_changed();
}

bool WorldChunkStore::mark_chunk_column_generating(
    ChunkCoord coord,
    WorldDirtyTracker& dirty) {
  (void)dirty;
  Chunk& column = ensure_column(coord);
  bool marked_any = false;
  bool created_any = false;
  for (i32 y = height_.min_chunk_y; y <= height_.max_chunk_y; ++y) {
    voxel::ChunkSection* section = column.find_section(y);
    if (section == nullptr) {
      (void)column.ensure_section(y, voxel::ChunkLifecycleState::Generating);
      ++lifecycle_counts_[lifecycle_index(voxel::ChunkLifecycleState::Generating)];
      marked_any = true;
      created_any = true;
      continue;
    }
    if (section->lifecycle_state() != voxel::ChunkLifecycleState::Requested) {
      continue;
    }
    set_lifecycle_state_internal(*section, voxel::ChunkLifecycleState::Generating);
    marked_any = true;
  }
  if (created_any) {
    note_simulation_topology_changed();
  }
  return marked_any;
}

bool WorldChunkStore::unload_chunk(ChunkSectionCoord coord) noexcept {
  Chunk* column = find_column(chunk_to_column(coord));
  if (column == nullptr) {
    return false;
  }
  const voxel::ChunkSection* section = column->find_section(coord);
  if (section == nullptr) {
    return false;
  }

  const voxel::ChunkLifecycleState state = section->lifecycle_state();
  usize& state_count = lifecycle_counts_[lifecycle_index(state)];
  if (state_count > 0U) {
    --state_count;
  }
  if (state == voxel::ChunkLifecycleState::Active && active_chunk_count_ > 0U) {
    --active_chunk_count_;
  }
  (void)column->unload_section(coord.value.y);
  if (column->loaded_section_count() == 0U) {
    chunks_.erase(chunk_to_column(coord));
  }
  note_simulation_topology_changed();
  return true;
}

voxel::ChunkSection* WorldChunkStore::find_chunk(ChunkSectionCoord coord) noexcept {
  Chunk* column = find_column(chunk_to_column(coord));
  return column == nullptr ? nullptr : column->find_section(coord);
}

const voxel::ChunkSection* WorldChunkStore::find_chunk(ChunkSectionCoord coord) const noexcept {
  const Chunk* column = find_column(chunk_to_column(coord));
  return column == nullptr ? nullptr : column->find_section(coord);
}

usize WorldChunkStore::loaded_count() const noexcept {
  usize count = 0;
  for (const auto& [coord, chunk] : chunks_) {
    (void)coord;
    count += chunk.loaded_section_count();
  }
  return count;
}

usize WorldChunkStore::active_count() const noexcept {
  return active_chunk_count_;
}

bool WorldChunkStore::is_active(ChunkSectionCoord coord) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  return chunk != nullptr && chunk->lifecycle_state() == voxel::ChunkLifecycleState::Active;
}

usize WorldChunkStore::lifecycle_count(voxel::ChunkLifecycleState state) const noexcept {
  return lifecycle_counts_[lifecycle_index(state)];
}

std::vector<ChunkSectionCoord> WorldChunkStore::loaded_coords() const {
  std::vector<ChunkSectionCoord> coords;
  for (const auto& [coord, chunk] : chunks_) {
    (void)coord;
    const std::vector<ChunkSectionCoord> chunk_coords = chunk.loaded_section_coords();
    coords.insert(coords.end(), chunk_coords.begin(), chunk_coords.end());
  }
  return coords;
}

ChunkWorkloadSnapshot WorldChunkStore::workload_snapshot(
    bool lighting_work_pending,
    usize last_lighting_snapshot_chunk_count,
    usize dirty_mesh_chunk_count) const noexcept {
  ChunkWorkloadSnapshot snapshot;
  snapshot.loaded_chunks = loaded_count();
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
    ChunkSectionCoord coord,
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
  if (!(simulation_membership_state(previous) && simulation_membership_state(state))) {
    note_simulation_topology_changed();
  }
}

void WorldChunkStore::update_simulation_radius(
    WorldRuntime& world,
    ChunkSectionCoord center,
    i32 radius) {
  const std::array<ChunkSectionCoord, 1> centers{{center}};
  update_simulation_radii(world, centers, radius);
}

void WorldChunkStore::update_simulation_radii(
    WorldRuntime& world,
    std::span<const ChunkSectionCoord> centers,
    i32 radius) {
  std::vector<ChunkSectionCoord> normalized_centers{centers.begin(), centers.end()};
  std::sort(
      normalized_centers.begin(),
      normalized_centers.end(),
      chunk_section_less_xyz);
  normalized_centers.erase(
      std::unique(normalized_centers.begin(), normalized_centers.end()),
      normalized_centers.end());
  if (has_cached_simulation_activity_ &&
      cached_simulation_radius_ == radius &&
      cached_simulation_topology_revision_ == simulation_topology_revision_ &&
      cached_simulation_centers_ == normalized_centers) {
    return;
  }

  for_each_loaded_section([&](voxel::ChunkSection& chunk) {
    const ChunkSectionCoord coord = chunk.coord();
    if (chunk.lifecycle_state() != voxel::ChunkLifecycleState::Loaded &&
        chunk.lifecycle_state() != voxel::ChunkLifecycleState::Active) {
      return;
    }

    bool active = false;
    if (radius >= 0) {
      for (const ChunkSectionCoord center : normalized_centers) {
        const i32 dx = coord.value.x - center.value.x;
        const i32 dy = coord.value.y - center.value.y;
        const i32 dz = coord.value.z - center.value.z;
        if (arv::max_abs3(dx, dy, dz) <= radius) {
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
  });
  cached_simulation_centers_ = std::move(normalized_centers);
  cached_simulation_radius_ = radius;
  cached_simulation_topology_revision_ = simulation_topology_revision_;
  has_cached_simulation_activity_ = true;
}

} // namespace arv::world::detail

namespace arv::world {

void WorldRuntime::configure_height(WorldHeightSettings settings) {
  assert_owner_thread();
  impl_->chunks.configure_height(settings);
}

WorldHeightSettings WorldRuntime::height_settings() const noexcept {
  return impl_->chunks.height_settings();
}

Chunk& WorldRuntime::ensure_chunk(ChunkCoord coord) {
  assert_owner_thread();
  return impl_->chunks.ensure_column(coord);
}

Chunk* WorldRuntime::find_chunk(ChunkCoord coord) noexcept {
  return impl_->chunks.find_column(coord);
}

const Chunk* WorldRuntime::find_chunk(ChunkCoord coord) const noexcept {
  return impl_->chunks.find_column(coord);
}

voxel::ChunkSection& WorldRuntime::ensure_chunk_section(
    ChunkSectionCoord coord,
    voxel::ChunkLifecycleState state) {
  assert_owner_thread();
  return impl_->chunks.ensure_chunk(coord, state, impl_->dirty);
}

void WorldRuntime::request_chunk_section_load(ChunkSectionCoord coord) {
  assert_owner_thread();
  impl_->chunks.request_chunk_load(coord, impl_->dirty);
}

bool WorldRuntime::unload_chunk_section(ChunkSectionCoord coord) noexcept {
  assert_owner_thread();
  return unload_chunk(coord);
}

voxel::ChunkSection* WorldRuntime::find_chunk_section(ChunkSectionCoord coord) noexcept {
  return impl_->chunks.find_chunk(coord);
}

const voxel::ChunkSection* WorldRuntime::find_chunk_section(ChunkSectionCoord coord) const noexcept {
  return impl_->chunks.find_chunk(coord);
}

voxel::ChunkSection& WorldRuntime::ensure_chunk(
    ChunkSectionCoord coord,
    voxel::ChunkLifecycleState state) {
  assert_owner_thread();
  return ensure_chunk_section(coord, state);
}

void WorldRuntime::request_chunk_load(ChunkSectionCoord coord) {
  assert_owner_thread();
  request_chunk_section_load(coord);
}

bool WorldRuntime::unload_chunk(ChunkSectionCoord coord) noexcept {
  assert_owner_thread();
  if (!impl_->chunks.unload_chunk(coord)) {
    return false;
  }

  impl_->block_containers.erase(coord);
  impl_->dirty.mark_mesh_content_dirty();
  impl_->dirty.mark_loaded_chunk_neighbors_dirty(*this, coord);
  impl_->dirty.recompute_lighting_work_pending(*this);
  return true;
}

voxel::ChunkSection* WorldRuntime::find_chunk(ChunkSectionCoord coord) noexcept {
  return impl_->chunks.find_chunk(coord);
}

const voxel::ChunkSection* WorldRuntime::find_chunk(ChunkSectionCoord coord) const noexcept {
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

std::vector<ChunkSectionCoord> WorldRuntime::loaded_chunk_coords() const {
  return impl_->chunks.loaded_coords();
}

bool WorldRuntime::is_chunk_active(ChunkSectionCoord coord) const noexcept {
  return impl_->chunks.is_active(coord);
}

usize WorldRuntime::active_chunk_count() const noexcept {
  return impl_->chunks.active_count();
}

bool WorldRuntime::set_chunk_lifecycle_state(
    ChunkSectionCoord coord,
    voxel::ChunkLifecycleState state) noexcept {
  assert_owner_thread();
  const bool changed = impl_->chunks.set_lifecycle_state(coord, state);
  if (changed && state == voxel::ChunkLifecycleState::Active) {
    FluidSolver::enqueue_chunk_activation_updates(*this, coord);
  }
  return changed;
}

bool WorldRuntime::mark_chunk_column_generating(ChunkCoord coord) {
  assert_owner_thread();
  return impl_->chunks.mark_chunk_column_generating(coord, impl_->dirty);
}

void WorldRuntime::update_simulation_radius(ChunkSectionCoord center, i32 radius) {
  assert_owner_thread();
  impl_->chunks.update_simulation_radius(*this, center, radius);
}

void WorldRuntime::update_simulation_radii(
    std::span<const ChunkSectionCoord> centers,
    i32 radius) {
  assert_owner_thread();
  impl_->chunks.update_simulation_radii(*this, centers, radius);
}

} // namespace arv::world
