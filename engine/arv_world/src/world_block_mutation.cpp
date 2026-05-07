#include "world_runtime_internal.hpp"

#include "arv/world/fluid_solver.hpp"

namespace arv::world {

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
  const ChunkSectionCoord chunk_coord = block_to_chunk(pos);
  voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return false;
  }

  const SectionLocalPos local = block_to_local(pos);
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
  impl_->replicated_block_updates.push_back({pos, variant, impl_->tick_index});
  return true;
}

bool WorldRuntime::set_fluid(
    BlockPos pos,
    voxel::FluidCell fluid,
    const voxel::FluidDefinition& definition) noexcept {
  return impl_->set_fluid_cell(*this, pos, fluid, definition, true);
}

bool WorldRuntime::place_fluid_source(
    BlockPos pos,
    const voxel::FluidDefinition& definition) noexcept {
  return set_fluid(pos, voxel::make_fluid_source(definition), definition);
}

std::vector<ReplicatedBlockUpdate> WorldRuntime::drain_replicated_block_updates() {
  std::vector<ReplicatedBlockUpdate> drained = std::move(impl_->replicated_block_updates);
  impl_->replicated_block_updates.clear();
  return drained;
}

} // namespace arv::world
