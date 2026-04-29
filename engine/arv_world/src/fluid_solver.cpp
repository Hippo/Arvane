#include "arv/world/fluid_solver.hpp"

#include "arv/world/world_runtime.hpp"

#include <array>

namespace arv::world {
namespace {

inline constexpr std::array<Vec3i, 4> HorizontalOffsets{{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 0, 1},
    {0, 0, -1},
}};

} // namespace

void FluidSolver::enqueue_neighbor_updates(
    WorldRuntime& world,
    BlockPos pos,
    FluidUpdateReason reason) {
  world.fluid_updates_.push_back({pos, reason});
  for (const Vec3i offset : WorldRuntime::NeighborOffsets) {
    const BlockPos neighbor = WorldRuntime::offset_block(pos, offset);
    const ChunkCoord origin_chunk = block_to_chunk(pos);
    const ChunkCoord neighbor_chunk_coord = block_to_chunk(neighbor);
    if (origin_chunk != neighbor_chunk_coord && !world.is_chunk_active(neighbor_chunk_coord)) {
      store_deferred_boundary_update(world, neighbor_chunk_coord, {neighbor, reason});
      continue;
    }
    world.fluid_updates_.push_back({neighbor, reason});
  }
}

void FluidSolver::enqueue_chunk_activation_updates(WorldRuntime& world, ChunkCoord coord) {
  const voxel::ChunkSection* chunk = world.find_chunk(coord);
  if (chunk == nullptr) {
    return;
  }

  auto deferred = world.deferred_boundary_fluid_updates_.find(coord);
  if (deferred != world.deferred_boundary_fluid_updates_.end()) {
    for (const FluidUpdate& update : deferred->second) {
      world.fluid_updates_.push_back(update);
    }
    world.deferred_boundary_fluid_updates_.erase(deferred);
  }

  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 y = 0; y < ChunkSize; ++y) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const SectionLocalPos local{{x, y, z}};
        if (!chunk->fluid(local).empty()) {
          world.fluid_updates_.push_back(
              {chunk_local_to_block(coord, local), FluidUpdateReason::ChunkActivated});
        }
      }
    }
  }
}

void FluidSolver::store_deferred_boundary_update(
    WorldRuntime& world,
    ChunkCoord target,
    FluidUpdate update) {
  auto& updates = world.deferred_boundary_fluid_updates_[target];
  for (const FluidUpdate& existing : updates) {
    if (existing.pos == update.pos) {
      return;
    }
  }
  updates.push_back(update);
}

u32 FluidSolver::process_updates(
    WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    u32 budget) {
  u32 processed = 0;
  while (processed < budget && !world.fluid_updates_.empty()) {
    FluidUpdate update = world.fluid_updates_.front();
    world.fluid_updates_.pop_front();

    const ChunkCoord chunk_coord = block_to_chunk(update.pos);
    if (!world.is_chunk_active(chunk_coord)) {
      store_deferred_boundary_update(world, chunk_coord, update);
      ++processed;
      continue;
    }

    process_single_update(world, registry, definition, update.pos);
    ++processed;
  }
  return processed;
}

bool FluidSolver::can_fluid_occupy(
    const WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos) noexcept {
  if (!definition.can_replace_air) {
    return false;
  }

  const voxel::ChunkSection* chunk = world.find_chunk(block_to_chunk(pos));
  if (chunk == nullptr) {
    return false;
  }

  const voxel::BlockVariantRecord* variant = registry.variant(chunk->block(block_to_local(pos)));
  return variant != nullptr &&
         (registry.is_air(variant->id) || variant->properties.replaceable);
}

bool FluidSolver::can_flow_down_from(
    const WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos) noexcept {
  return definition.can_flow_down &&
         can_fluid_occupy(
             world,
             registry,
             definition,
             WorldRuntime::offset_block(pos, {0, -1, 0}));
}

voxel::FluidCell FluidSolver::ideal_fluid_at(
    const WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos,
    voxel::FluidCell current) noexcept {
  if (!can_fluid_occupy(world, registry, definition, pos)) {
    return voxel::empty_fluid_cell();
  }

  if (current.source() && current.type == definition.type) {
    return voxel::make_fluid_source(definition);
  }

  const BlockPos above = WorldRuntime::offset_block(pos, {0, 1, 0});
  const voxel::FluidCell above_fluid = world.get_fluid(above);
  if (definition.can_flow_down && above_fluid.type == definition.type && !above_fluid.empty()) {
    return voxel::make_flowing_fluid(
        definition,
        definition.vertical_flow_level,
        voxel::FluidFlags::Falling);
  }

  const BlockPos below = WorldRuntime::offset_block(pos, {0, -1, 0});
  const voxel::FluidCell below_fluid = world.get_fluid(below);
  if (definition.can_create_sources &&
      (!can_fluid_occupy(world, registry, definition, below) ||
       (below_fluid.type == definition.type && below_fluid.source()))) {
    u32 adjacent_sources = 0;
    for (const Vec3i offset : HorizontalOffsets) {
      const voxel::FluidCell neighbor =
          world.get_fluid(WorldRuntime::offset_block(pos, offset));
      if (neighbor.type == definition.type && neighbor.source()) {
        ++adjacent_sources;
      }
    }
    if (adjacent_sources >= 2U) {
      return voxel::make_fluid_source(definition);
    }
  }

  if (!definition.can_flow_sideways) {
    return voxel::empty_fluid_cell();
  }

  u8 best_level = 0;
  for (const Vec3i offset : HorizontalOffsets) {
    const BlockPos neighbor_pos = WorldRuntime::offset_block(pos, offset);
    if (can_flow_down_from(world, registry, definition, neighbor_pos)) {
      continue;
    }

    const voxel::FluidCell neighbor = world.get_fluid(neighbor_pos);
    if (neighbor.type != definition.type || neighbor.empty()) {
      continue;
    }

    const u8 neighbor_level = neighbor.source() ? definition.max_level : neighbor.level;
    if (neighbor_level <= definition.horizontal_decay) {
      continue;
    }

    const u8 candidate = static_cast<u8>(neighbor_level - definition.horizontal_decay);
    if (candidate > best_level) {
      best_level = candidate;
    }
  }

  if (best_level == 0) {
    return voxel::empty_fluid_cell();
  }
  return voxel::make_flowing_fluid(definition, best_level);
}

void FluidSolver::process_single_update(
    WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos) {
  voxel::ChunkSection* chunk = world.find_chunk(block_to_chunk(pos));
  if (chunk == nullptr) {
    return;
  }

  const SectionLocalPos local = block_to_local(pos);
  const voxel::FluidCell current = chunk->fluid(local);
  const voxel::FluidCell ideal = ideal_fluid_at(world, registry, definition, pos, current);
  if (current == ideal) {
    return;
  }

  const bool affects_lighting = definition.reduces_sky_light || definition.emits_light;
  if (!chunk->set_fluid(local, ideal, affects_lighting)) {
    return;
  }
  world.mark_mesh_content_dirty();
  if (affects_lighting) {
    world.mark_lighting_content_dirty();
  }

  world.mark_boundary_neighbors_dirty(block_to_chunk(pos), local);
  enqueue_neighbor_updates(
      world,
      pos,
      ideal.empty() ? FluidUpdateReason::FluidRemoved : FluidUpdateReason::FluidAdded);
}

} // namespace arv::world
