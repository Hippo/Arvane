#include "world_runtime_internal.hpp"

#include "arv/world/fluid_solver.hpp"

#include <algorithm>
#include <tuple>

namespace arv::world {
namespace {

struct PendingBlockMutation {
  ChunkSectionCoord section{};
  SectionLocalPos local{};
  BlockPos pos{};
  voxel::BlockVariantID new_state{};
};

[[nodiscard]] auto section_sort_key(ChunkSectionCoord coord) noexcept {
  return std::tuple{coord.value.x, coord.value.y, coord.value.z};
}

void push_sorted_unique(
    std::vector<ChunkSectionCoord>& coords,
    HashSet<ChunkSectionCoord>& seen,
    ChunkSectionCoord coord) {
  if (seen.insert(coord).second) {
    coords.push_back(coord);
  }
}

void collect_boundary_dirty_sections(
    ChunkSectionCoord coord,
    SectionLocalPos local,
    std::vector<ChunkSectionCoord>& coords,
    HashSet<ChunkSectionCoord>& seen) {
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
    push_sorted_unique(
        coords,
        seen,
        offset_chunk_section(coord, offset));
  }
}

void sort_section_coords(std::vector<ChunkSectionCoord>& coords) {
  std::sort(coords.begin(), coords.end(), [](ChunkSectionCoord lhs, ChunkSectionCoord rhs) {
    return section_sort_key(lhs) < section_sort_key(rhs);
  });
}

} // namespace

bool WorldRuntime::Impl::set_fluid_cell(
    WorldRuntime& world,
    BlockPos pos,
    voxel::FluidCell fluid,
    const voxel::FluidDefinition& definition,
    bool enqueue_updates) noexcept {
  const ChunkSectionCoord chunk_coord = block_to_chunk(pos);
  voxel::ChunkSection* chunk = world.find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return false;
  }

  const SectionLocalPos local = block_to_local(pos);
  const bool affects_lighting = definition.reduces_sky_light || definition.emits_light;
  if (!chunk->set_fluid(local, fluid, affects_lighting)) {
    return false;
  }

  dirty.mark_mesh_content_dirty();
  dirty.enqueue_dirty_mesh_chunk(chunk_coord);
  dirty.mark_boundary_neighbors_dirty(world, chunk_coord, local);
  if (affects_lighting) {
    dirty.mark_chunk_lighting_dirty(world, chunk_coord);
    dirty.mark_loaded_chunk_lighting_dependencies_dirty(world, chunk_coord);
    dirty.mark_lighting_content_dirty();
  }
  if (enqueue_updates) {
    FluidSolver::enqueue_neighbor_updates(
        world,
        pos,
        fluid.empty() ? FluidUpdateReason::FluidRemoved : FluidUpdateReason::FluidAdded,
        definition.flow_interval_ticks);
  }
  return true;
}

voxel::BlockVariantID WorldRuntime::get_block(BlockPos pos) const noexcept {
  const ChunkSectionCoord chunk_coord = block_to_chunk(pos);
  const voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return voxel::AirBlockVariantID;
  }
  return chunk->block(block_to_local(pos));
}

voxel::LightWord WorldRuntime::get_light(BlockPos pos) const noexcept {
  const ChunkSectionCoord chunk_coord = block_to_chunk(pos);
  const voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return voxel::NoLight;
  }
  return chunk->light(block_to_local(pos));
}

voxel::FluidCell WorldRuntime::get_fluid(BlockPos pos) const noexcept {
  const ChunkSectionCoord chunk_coord = block_to_chunk(pos);
  const voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return voxel::empty_fluid_cell();
  }
  return chunk->fluid(block_to_local(pos));
}

bool WorldRuntime::set_block(BlockPos pos, voxel::BlockVariantID variant) noexcept {
  assert_owner_thread();
  const ChunkSectionCoord chunk_coord = block_to_chunk(pos);
  voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return false;
  }

  const SectionLocalPos local = block_to_local(pos);
  const voxel::BlockVariantID old_variant = chunk->block(local);
  if (!chunk->set_block(local, variant)) {
    return false;
  }

  impl_->dirty.mark_mesh_content_dirty();
  impl_->dirty.enqueue_dirty_mesh_chunk(chunk_coord);
  impl_->dirty.mark_boundary_neighbors_dirty(*this, chunk_coord, local);
  impl_->dirty.mark_chunk_lighting_dirty(*this, chunk_coord);
  impl_->dirty.mark_loaded_chunk_lighting_dependencies_dirty(*this, chunk_coord);
  impl_->dirty.mark_lighting_content_dirty();
  impl_->tick_queues.enqueue_neighbor_block_updates(pos);
  FluidSolver::enqueue_neighbor_updates(*this, pos, FluidUpdateReason::BlockChanged);
  impl_->replicated_block_updates.push_back({pos, old_variant, variant, impl_->tick_index});
  return true;
}

BlockMutationBatchResult WorldRuntime::apply_block_mutation_batch(
    std::span<const BlockMutation> mutations) noexcept {
  assert_owner_thread();
  BlockMutationBatchResult result;
  if (mutations.empty()) {
    return result;
  }

  std::vector<PendingBlockMutation> pending;
  pending.reserve(mutations.size());
  for (const BlockMutation& mutation : mutations) {
    const ChunkSectionCoord chunk_coord = block_to_chunk(mutation.pos);
    voxel::ChunkSection* chunk = find_chunk(chunk_coord);
    if (chunk == nullptr) {
      continue;
    }
    pending.push_back({chunk_coord, block_to_local(mutation.pos), mutation.pos, mutation.new_state});
  }
  std::sort(pending.begin(), pending.end(), [](const auto& lhs, const auto& rhs) {
    if (section_sort_key(lhs.section) != section_sort_key(rhs.section)) {
      return section_sort_key(lhs.section) < section_sort_key(rhs.section);
    }
    return std::tuple{lhs.local.value.x, lhs.local.value.y, lhs.local.value.z} <
           std::tuple{rhs.local.value.x, rhs.local.value.y, rhs.local.value.z};
  });

  HashSet<ChunkSectionCoord> touched_seen;
  HashSet<ChunkSectionCoord> dirty_seen;
  for (const PendingBlockMutation& mutation : pending) {
    voxel::ChunkSection* chunk = find_chunk(mutation.section);
    if (chunk == nullptr) {
      continue;
    }
    const voxel::BlockVariantID old_variant = chunk->block(mutation.local);
    if (!chunk->set_block(mutation.local, mutation.new_state)) {
      continue;
    }

    ++result.applied;
    push_sorted_unique(
        result.touched_sections,
        touched_seen,
        mutation.section);
    push_sorted_unique(
        result.dirty_sections,
        dirty_seen,
        mutation.section);
    collect_boundary_dirty_sections(
        mutation.section,
        mutation.local,
        result.dirty_sections,
        dirty_seen);

    impl_->dirty.mark_boundary_neighbors_dirty(*this, mutation.section, mutation.local);
    impl_->tick_queues.enqueue_neighbor_block_updates(mutation.pos);
    FluidSolver::enqueue_neighbor_updates(*this, mutation.pos, FluidUpdateReason::BlockChanged);
    impl_->replicated_block_updates.push_back(
        {mutation.pos, old_variant, mutation.new_state, impl_->tick_index});
  }

  if (result.applied == 0U) {
    return result;
  }

  for (const ChunkSectionCoord coord : result.touched_sections) {
    impl_->dirty.enqueue_dirty_mesh_chunk(coord);
    impl_->dirty.mark_chunk_lighting_dirty(*this, coord);
    impl_->dirty.mark_loaded_chunk_lighting_dependencies_dirty(*this, coord);
  }
  impl_->dirty.mark_mesh_content_dirty();
  impl_->dirty.mark_lighting_content_dirty();
  sort_section_coords(result.touched_sections);
  sort_section_coords(result.dirty_sections);
  return result;
}

bool WorldRuntime::set_fluid(
    BlockPos pos,
    voxel::FluidCell fluid,
    const voxel::FluidDefinition& definition) noexcept {
  assert_owner_thread();
  return impl_->set_fluid_cell(*this, pos, fluid, definition, true);
}

bool WorldRuntime::place_fluid_source(
    BlockPos pos,
    const voxel::FluidDefinition& definition) noexcept {
  return set_fluid(pos, voxel::make_fluid_source(definition), definition);
}

std::vector<ReplicatedBlockUpdate> WorldRuntime::drain_replicated_block_updates() {
  assert_owner_thread();
  std::vector<ReplicatedBlockUpdate> drained = std::move(impl_->replicated_block_updates);
  impl_->replicated_block_updates.clear();
  return drained;
}

} // namespace arv::world
