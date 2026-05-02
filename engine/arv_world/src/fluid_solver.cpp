#include "arv/world/fluid_solver.hpp"

#include "arv/core/perf_stats.hpp"
#include "world_runtime_internal.hpp"

#include <array>
#include <deque>
#include <optional>
#include <utility>

namespace arv::world {
namespace {

inline constexpr std::array<Vec3i, 4> HorizontalOffsets{{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 0, 1},
    {0, 0, -1},
}};

inline constexpr std::array<Vec3i, 6> NeighborOffsets{{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
}};

struct SimulationChunkState {
  bool active{};
  u64 source_content_version{};
  voxel::ChunkStorageView storage{};
};

class FluidSimulationState {
public:
  explicit FluidSimulationState(FluidSimulationRequest request)
      : current_tick_(request.current_tick),
        budget_(request.budget) {
    for (FluidSimulationChunk& chunk : request.chunks) {
      chunks_.emplace(
          chunk.coord,
          SimulationChunkState{
              chunk.active,
              chunk.source_content_version,
              std::move(chunk.storage)});
    }
    for (const FluidUpdate& update : request.updates) {
      enqueue_update(update);
    }
  }

  [[nodiscard]] FluidSimulationResult run(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition) {
    definition_flow_interval_ticks_ = definition.flow_interval_ticks;
    while (result_.processed < budget_ && !updates_.empty()) {
      const FluidUpdate update = updates_.front();
      updates_.pop_front();
      queued_positions_.erase(update.pos);

      const ChunkCoord chunk_coord = block_to_chunk(update.pos);
      const SimulationChunkState* chunk = find_chunk(chunk_coord);
      if (!can_process_update(update, chunk)) {
        result_.deferred_updates.emplace_back(update);
        ++result_.processed;
        continue;
      }

      process_single_update(registry, definition, update.pos);
      ++result_.processed;
    }

    while (!updates_.empty()) {
      result_.followup_updates.emplace_back(updates_.front());
      updates_.pop_front();
    }
    queued_positions_.clear();
    while (!followup_updates_.empty()) {
      result_.followup_updates.emplace_back(followup_updates_.front());
      followup_updates_.pop_front();
    }
    queued_followup_positions_.clear();
    return std::move(result_);
  }

private:
  [[nodiscard]] static bool can_process_inactive(FluidUpdateReason reason) noexcept {
    return reason == FluidUpdateReason::FluidRemoved ||
           reason == FluidUpdateReason::BlockChanged;
  }

  [[nodiscard]] static bool can_process_update(
      FluidUpdate update,
      const SimulationChunkState* chunk) noexcept {
    return chunk != nullptr &&
           chunk->storage &&
           (chunk->active || can_process_inactive(update.reason));
  }

  [[nodiscard]] static BlockPos offset_block(BlockPos pos, Vec3i offset) noexcept {
    return {{pos.value.x + offset.x, pos.value.y + offset.y, pos.value.z + offset.z}};
  }

  [[nodiscard]] SimulationChunkState* find_chunk(ChunkCoord coord) noexcept {
    const auto found = chunks_.find(coord);
    return found == chunks_.end() ? nullptr : &found->second;
  }

  [[nodiscard]] const SimulationChunkState* find_chunk(ChunkCoord coord) const noexcept {
    const auto found = chunks_.find(coord);
    return found == chunks_.end() ? nullptr : &found->second;
  }

  [[nodiscard]] const std::array<voxel::FluidCell, voxel::ChunkVolume>* fluids_for(
      const SimulationChunkState& chunk) const noexcept {
    return chunk.storage ? &chunk.storage->fluids : nullptr;
  }

  [[nodiscard]] voxel::BlockVariantID block_at(BlockPos pos) const noexcept {
    const SimulationChunkState* chunk = find_chunk(block_to_chunk(pos));
    if (chunk == nullptr || !chunk->storage) {
      return voxel::AirBlockVariantID;
    }
    return chunk->storage->blocks[voxel::local_index(block_to_local(pos))];
  }

  [[nodiscard]] voxel::FluidCell fluid_at(BlockPos pos) const noexcept {
    const SimulationChunkState* chunk = find_chunk(block_to_chunk(pos));
    if (chunk == nullptr) {
      return voxel::empty_fluid_cell();
    }
    const auto* fluids = fluids_for(*chunk);
    return fluids == nullptr
               ? voxel::empty_fluid_cell()
               : (*fluids)[voxel::local_index(block_to_local(pos))];
  }

  [[nodiscard]] bool is_active(ChunkCoord coord) const noexcept {
    const SimulationChunkState* chunk = find_chunk(coord);
    return chunk != nullptr && chunk->active;
  }

  void enqueue_update(FluidUpdate update) {
    const bool urgent =
        update.reason == FluidUpdateReason::FluidRemoved ||
        update.reason == FluidUpdateReason::BlockChanged;
    if (queued_positions_.insert(update.pos).second) {
      if (urgent) {
        updates_.emplace_front(update);
      } else {
        updates_.emplace_back(update);
      }
      return;
    }
    if (urgent) {
      updates_.emplace_front(update);
    }
  }

  void enqueue_followup_update(FluidUpdate update) {
    if (queued_followup_positions_.insert(update.pos).second) {
      followup_updates_.emplace_back(update);
    }
  }

  void enqueue_followup_update(BlockPos pos, FluidUpdateReason reason) {
    enqueue_followup_update(
        {pos, reason, current_tick_ + definition_flow_interval_ticks_});
  }

  void enqueue_neighbor_updates(BlockPos pos, FluidUpdateReason reason) {
    enqueue_followup_update(pos, reason);
    enqueue_adjacent_updates(pos, reason);
  }

  void enqueue_adjacent_updates(BlockPos pos, FluidUpdateReason reason) {
    for (const Vec3i offset : NeighborOffsets) {
      const BlockPos neighbor = offset_block(pos, offset);
      const ChunkCoord origin_chunk = block_to_chunk(pos);
      const ChunkCoord neighbor_chunk_coord = block_to_chunk(neighbor);
      if (origin_chunk != neighbor_chunk_coord &&
          !is_active(neighbor_chunk_coord) &&
          !can_process_inactive(reason)) {
        result_.deferred_updates.emplace_back(
            neighbor,
            reason,
            current_tick_ + definition_flow_interval_ticks_);
        continue;
      }
      enqueue_followup_update(neighbor, reason);
    }
  }

  [[nodiscard]] bool can_fluid_occupy(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) const noexcept {
    if (!definition.can_replace_air || find_chunk(block_to_chunk(pos)) == nullptr) {
      return false;
    }

    const voxel::BlockVariantRecord* variant = registry.variant(block_at(pos));
    return variant != nullptr &&
           (registry.is_air(variant->id) || variant->properties.replaceable);
  }

  [[nodiscard]] bool can_flow_down_from(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) const noexcept {
    return definition.can_flow_down &&
           can_fluid_occupy(registry, definition, offset_block(pos, {0, -1, 0}));
  }

  [[nodiscard]] voxel::FluidCell ideal_fluid_at(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos,
      voxel::FluidCell current) const noexcept {
    if (!can_fluid_occupy(registry, definition, pos)) {
      return voxel::empty_fluid_cell();
    }

    if (current.source() && current.type == definition.type) {
      return voxel::make_fluid_source(definition);
    }

    const BlockPos above = offset_block(pos, {0, 1, 0});
    const voxel::FluidCell above_fluid = fluid_at(above);
    if (definition.can_flow_down && above_fluid.type == definition.type && !above_fluid.empty()) {
      return voxel::make_flowing_fluid(
          definition,
          definition.vertical_flow_level,
          voxel::FluidFlags::Falling);
    }

    const BlockPos below = offset_block(pos, {0, -1, 0});
    const voxel::FluidCell below_fluid = fluid_at(below);
    if (definition.can_create_sources &&
        (!can_fluid_occupy(registry, definition, below) ||
         (below_fluid.type == definition.type && below_fluid.source()))) {
      u32 adjacent_sources = 0;
      for (const Vec3i offset : HorizontalOffsets) {
        const voxel::FluidCell neighbor = fluid_at(offset_block(pos, offset));
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
      const BlockPos neighbor_pos = offset_block(pos, offset);
      if (can_flow_down_from(registry, definition, neighbor_pos)) {
        continue;
      }

      const voxel::FluidCell neighbor = fluid_at(neighbor_pos);
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

  void process_single_update(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) {
    SimulationChunkState* chunk = find_chunk(block_to_chunk(pos));
    if (chunk == nullptr || !chunk->storage) {
      return;
    }

    const SectionLocalPos local = block_to_local(pos);
    const voxel::FluidCell current = fluid_at(pos);
    const voxel::FluidCell ideal = ideal_fluid_at(registry, definition, pos, current);
    if (current == ideal) {
      return;
    }

    result_.changes.emplace_back(pos, ideal, chunk->source_content_version);
    const bool removed_or_decreased =
        ideal.empty() ||
        (!current.empty() &&
         (ideal.type != current.type || ideal.level < current.level || current.source() != ideal.source()));
    if (removed_or_decreased) {
      enqueue_followup_update(pos, FluidUpdateReason::FluidRemoved);
    }
    enqueue_adjacent_updates(
        pos,
        removed_or_decreased ? FluidUpdateReason::FluidRemoved : FluidUpdateReason::FluidAdded);
  }

  TickIndex current_tick_{};
  u32 definition_flow_interval_ticks_{};
  u32 budget_{};
  std::deque<FluidUpdate> updates_;
  std::deque<FluidUpdate> followup_updates_;
  HashSet<BlockPos> queued_positions_;
  HashSet<BlockPos> queued_followup_positions_;
  HashMap<ChunkCoord, SimulationChunkState> chunks_;
  FluidSimulationResult result_;
};

[[nodiscard]] bool can_process_inactive(FluidUpdateReason reason) noexcept {
  return reason == FluidUpdateReason::FluidRemoved ||
         reason == FluidUpdateReason::BlockChanged;
}

} // namespace

void FluidSolver::enqueue_neighbor_updates(
    WorldRuntime& world,
    BlockPos pos,
    FluidUpdateReason reason,
    u32 delay_ticks) {
  const TickIndex execute_at = world.tick_index() + delay_ticks;
  world.impl_->fluid.enqueue_update_unique({pos, reason, execute_at});
  for (const Vec3i offset : detail::NeighborOffsets) {
    const BlockPos neighbor = detail::offset_block(pos, offset);
    const ChunkCoord origin_chunk = block_to_chunk(pos);
    const ChunkCoord neighbor_chunk_coord = block_to_chunk(neighbor);
    if (origin_chunk != neighbor_chunk_coord &&
        !world.is_chunk_active(neighbor_chunk_coord) &&
        !can_process_inactive(reason)) {
      store_deferred_boundary_update(world, neighbor_chunk_coord, {neighbor, reason, execute_at});
      continue;
    }
    world.impl_->fluid.enqueue_update_unique({neighbor, reason, execute_at});
  }
}

void FluidSolver::enqueue_chunk_activation_updates(WorldRuntime& world, ChunkCoord coord) {
  world.impl_->fluid.enqueue_chunk_activation_updates(world, coord);
}

void FluidSolver::store_deferred_boundary_update(
    WorldRuntime& world,
    ChunkCoord target,
    FluidUpdate update) {
  world.impl_->fluid.store_deferred_boundary_update(target, update);
}

u32 FluidSolver::process_updates(
    WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    u32 budget) {
  std::vector<FluidUpdate> batch =
      world.impl_->fluid.drain_due_updates(world.tick_index(), budget);

  u32 processed = 0;
  for (const FluidUpdate& update : batch) {
    const ChunkCoord chunk_coord = block_to_chunk(update.pos);
    if (!world.is_chunk_active(chunk_coord) &&
        !can_process_inactive(update.reason)) {
      store_deferred_boundary_update(world, chunk_coord, update);
      ++processed;
      continue;
    }

    process_single_update(world, registry, definition, update.pos);
    ++processed;
  }
  return processed;
}

FluidSimulationResult FluidSolver::build_simulation_result(
    FluidSimulationRequest request,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition) {
  const core::ScopedThreadTiming timing{
      core::PerfMetric::FluidTask,
      "worker/server",
      "queue=server_world_fluids"};
  FluidSimulationState state(std::move(request));
  return state.run(registry, definition);
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
             detail::offset_block(pos, {0, -1, 0}));
}

void FluidSolver::enqueue_adjacent_updates(
    WorldRuntime& world,
    BlockPos pos,
    FluidUpdateReason reason,
    u32 delay_ticks) {
  const TickIndex execute_at = world.tick_index() + delay_ticks;
  for (const Vec3i offset : detail::NeighborOffsets) {
    const BlockPos neighbor = detail::offset_block(pos, offset);
    const ChunkCoord origin_chunk = block_to_chunk(pos);
    const ChunkCoord neighbor_chunk_coord = block_to_chunk(neighbor);
    if (origin_chunk != neighbor_chunk_coord &&
        !world.is_chunk_active(neighbor_chunk_coord) &&
        !can_process_inactive(reason)) {
      store_deferred_boundary_update(world, neighbor_chunk_coord, {neighbor, reason, execute_at});
      continue;
    }
    world.impl_->fluid.enqueue_update_unique({neighbor, reason, execute_at});
  }
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

  const BlockPos above = detail::offset_block(pos, {0, 1, 0});
  const voxel::FluidCell above_fluid = world.get_fluid(above);
  if (definition.can_flow_down && above_fluid.type == definition.type && !above_fluid.empty()) {
    return voxel::make_flowing_fluid(
        definition,
        definition.vertical_flow_level,
        voxel::FluidFlags::Falling);
  }

  const BlockPos below = detail::offset_block(pos, {0, -1, 0});
  const voxel::FluidCell below_fluid = world.get_fluid(below);
  if (definition.can_create_sources &&
      (!can_fluid_occupy(world, registry, definition, below) ||
       (below_fluid.type == definition.type && below_fluid.source()))) {
    u32 adjacent_sources = 0;
      for (const Vec3i offset : HorizontalOffsets) {
        const voxel::FluidCell neighbor =
          world.get_fluid(detail::offset_block(pos, offset));
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
    const BlockPos neighbor_pos = detail::offset_block(pos, offset);
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
  world.impl_->dirty.mark_mesh_content_dirty();
  if (affects_lighting) {
    world.impl_->dirty.mark_lighting_content_dirty();
  }

  world.impl_->dirty.mark_boundary_neighbors_dirty(world, block_to_chunk(pos), local);
  const bool removed_or_decreased =
      ideal.empty() ||
      (!current.empty() &&
       (ideal.type != current.type || ideal.level < current.level || current.source() != ideal.source()));
  if (removed_or_decreased) {
    world.enqueue_fluid_update(
        pos,
        FluidUpdateReason::FluidRemoved,
        definition.flow_interval_ticks);
  }
  enqueue_adjacent_updates(
      world,
      pos,
      removed_or_decreased ? FluidUpdateReason::FluidRemoved : FluidUpdateReason::FluidAdded,
      definition.flow_interval_ticks);
}

} // namespace arv::world
