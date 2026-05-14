#include "world_runtime_internal.hpp"

#include "arv/world/fluid_solver.hpp"

#include <algorithm>
#include <utility>

namespace arv::world::detail {
namespace {

[[nodiscard]] u8 fluid_update_priority(FluidUpdateReason reason) noexcept {
  switch (reason) {
    case FluidUpdateReason::FluidRemoved:
    case FluidUpdateReason::BlockChanged:
      return 2U;
    case FluidUpdateReason::FluidAdded:
    case FluidUpdateReason::NeighborChanged:
    case FluidUpdateReason::ChunkActivated:
      return 1U;
  }
  return 0U;
}

[[nodiscard]] bool should_replace_fluid_update(
    const FluidUpdate& current,
    const FluidUpdate& candidate) noexcept {
  if (candidate.execute_at != current.execute_at) {
    return candidate.execute_at < current.execute_at;
  }
  return fluid_update_priority(candidate.reason) > fluid_update_priority(current.reason);
}

[[nodiscard]] bool matches_pending_fluid_update(
    const FluidUpdate& pending,
    const FluidUpdate& queued) noexcept {
  return pending.execute_at == queued.execute_at &&
         fluid_update_priority(pending.reason) == fluid_update_priority(queued.reason);
}

[[nodiscard]] BlockPos offset_fluid_probe(BlockPos pos, Vec3i offset) noexcept {
  return {{pos.value.x + offset.x, pos.value.y + offset.y, pos.value.z + offset.z}};
}

void add_required_fluid_chunk(HashSet<ChunkSectionCoord>& chunks, BlockPos pos) {
  chunks.insert(block_to_chunk(pos));
}

void add_required_fluid_chunks(HashSet<ChunkSectionCoord>& chunks, BlockPos pos) {
  static constexpr std::array<Vec3i, 4> HorizontalOffsets{{
      {1, 0, 0},
      {-1, 0, 0},
      {0, 0, 1},
      {0, 0, -1},
  }};
  static constexpr std::array<Vec3i, 6> NeighborOffsets{{
      {1, 0, 0},
      {-1, 0, 0},
      {0, 1, 0},
      {0, -1, 0},
      {0, 0, 1},
      {0, 0, -1},
  }};

  add_required_fluid_chunk(chunks, pos);
  for (const Vec3i offset : NeighborOffsets) {
    add_required_fluid_chunk(chunks, offset_fluid_probe(pos, offset));
  }
  for (const Vec3i offset : HorizontalOffsets) {
    const BlockPos neighbor = offset_fluid_probe(pos, offset);
    add_required_fluid_chunk(chunks, offset_fluid_probe(neighbor, {0, -1, 0}));
  }
}

} // namespace

void WorldFluidRuntime::enqueue_update(
    BlockPos pos,
    TickIndex current_tick,
    FluidUpdateReason reason,
    u32 delay_ticks) {
  enqueue_update_unique({pos, reason, current_tick + delay_ticks});
}

void WorldFluidRuntime::enqueue_update_unique(FluidUpdate update) {
  auto [pending, inserted] =
      pending_fluid_updates_by_position_.try_emplace(update.pos, update);
  if (inserted) {
    fluid_updates_.emplace_back(update);
    return;
  }

  if (!should_replace_fluid_update(pending->second, update)) {
    return;
  }

  pending->second = update;
  fluid_updates_.emplace_back(update);
}

std::vector<FluidUpdate> WorldFluidRuntime::drain_due_updates(
    TickIndex current_tick,
    u32 budget) {
  std::vector<FluidUpdate> due;
  if (budget == 0U || fluid_updates_.empty()) {
    return due;
  }

  due.reserve(std::min<usize>(budget, fluid_updates_.size()));
  std::deque<FluidUpdate> remaining;
  while (!fluid_updates_.empty()) {
    FluidUpdate update = fluid_updates_.front();
    fluid_updates_.pop_front();

    auto pending = pending_fluid_updates_by_position_.find(update.pos);
    if (pending == pending_fluid_updates_by_position_.end() ||
        !matches_pending_fluid_update(pending->second, update)) {
      continue;
    }

    if (update.execute_at > current_tick || due.size() >= budget) {
      remaining.emplace_back(update);
      continue;
    }

    pending_fluid_updates_by_position_.erase(pending);
    due.emplace_back(update);
  }

  fluid_updates_ = std::move(remaining);
  return due;
}

void WorldFluidRuntime::store_deferred_boundary_update(
    ChunkSectionCoord target,
    FluidUpdate update) {
  auto& updates = deferred_boundary_fluid_updates_[target];
  for (FluidUpdate& existing : updates) {
    if (existing.pos == update.pos) {
      if (should_replace_fluid_update(existing, update)) {
        existing = update;
      }
      return;
    }
  }
  updates.emplace_back(update);
}

void WorldFluidRuntime::enqueue_chunk_activation_updates(
    WorldRuntime& world,
    ChunkSectionCoord coord) {
  const voxel::ChunkSection* chunk = world.find_chunk(coord);
  if (chunk == nullptr) {
    return;
  }

  auto deferred = deferred_boundary_fluid_updates_.find(coord);
  if (deferred != deferred_boundary_fluid_updates_.end()) {
    for (const FluidUpdate& update : deferred->second) {
      enqueue_update_unique(update);
    }
    deferred_boundary_fluid_updates_.erase(deferred);
  }

  for (usize index = 0; index < voxel::ChunkVolume; ++index) {
    const SectionLocalPos local = voxel::local_pos(index);
    if (!chunk->fluid(local).empty()) {
      enqueue_update_unique(
          {chunk_local_to_block(coord, local), FluidUpdateReason::ChunkActivated, world.tick_index()});
    }
  }
}

usize WorldFluidRuntime::pending_update_count() const noexcept {
  return pending_fluid_updates_by_position_.size();
}

usize WorldFluidRuntime::deferred_boundary_event_count(ChunkSectionCoord coord) const noexcept {
  const auto found = deferred_boundary_fluid_updates_.find(coord);
  return found == deferred_boundary_fluid_updates_.end() ? 0U : found->second.size();
}

usize WorldFluidRuntime::total_deferred_boundary_event_count() const noexcept {
  usize count = 0;
  for (const auto& [coord, updates] : deferred_boundary_fluid_updates_) {
    (void)coord;
    count += updates.size();
  }
  return count;
}

u32 WorldFluidRuntime::process_updates(
    WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    u32 budget) {
  return FluidSolver::process_updates(world, registry, definition, budget);
}

std::optional<FluidSimulationRequest> WorldFluidRuntime::make_simulation_request(
    const WorldChunkStore& chunks,
    TickIndex current_tick,
    u32 budget) {
  std::vector<FluidUpdate> due_updates = drain_due_updates(current_tick, budget);
  if (budget == 0U || due_updates.empty()) {
    return std::nullopt;
  }

  FluidSimulationRequest request;
  request.current_tick = current_tick;
  request.budget = budget;
  request.updates = std::move(due_updates);

  HashSet<ChunkSectionCoord> required_chunks;
  for (const FluidUpdate& update : request.updates) {
    add_required_fluid_chunks(required_chunks, update.pos);
  }

  std::vector<ChunkSectionCoord> required_coords;
  required_coords.reserve(required_chunks.size());
  for (const ChunkSectionCoord coord : required_chunks) {
    required_coords.emplace_back(coord);
  }
  std::sort(required_coords.begin(), required_coords.end(), chunk_section_less_xyz);

  request.chunks.reserve(required_coords.size());
  for (const ChunkSectionCoord coord : required_coords) {
    const voxel::ChunkSection* chunk = chunks.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }
    request.chunks.emplace_back(
        coord,
        chunk->lifecycle_state() == voxel::ChunkLifecycleState::Active,
        chunk->content_version(),
        chunk->storage_view());
  }

  return request;
}

} // namespace arv::world::detail

namespace arv::world {

void WorldRuntime::enqueue_fluid_update(
    BlockPos pos,
    FluidUpdateReason reason,
    u32 delay_ticks) {
  assert_owner_thread();
  impl_->fluid.enqueue_update(pos, impl_->tick_index, reason, delay_ticks);
}

usize WorldRuntime::pending_fluid_update_count() const noexcept {
  return impl_->fluid.pending_update_count();
}

usize WorldRuntime::deferred_boundary_fluid_event_count(ChunkSectionCoord coord) const noexcept {
  return impl_->fluid.deferred_boundary_event_count(coord);
}

usize WorldRuntime::total_deferred_boundary_fluid_event_count() const noexcept {
  return impl_->fluid.total_deferred_boundary_event_count();
}

u32 WorldRuntime::process_fluid_updates(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    u32 budget) {
  assert_owner_thread();
  return impl_->fluid.process_updates(*this, registry, definition, budget);
}

std::optional<FluidSimulationRequest> WorldRuntime::make_fluid_simulation_request(
    u32 budget) {
  assert_owner_thread();
  return impl_->fluid.make_simulation_request(impl_->chunks, impl_->tick_index, budget);
}

u32 WorldRuntime::apply_fluid_simulation_result(
    FluidSimulationResult result,
    const voxel::FluidDefinition& definition) {
  assert_owner_thread();
  HashMap<ChunkSectionCoord, bool> accepted_chunks;
  WorldSearch search{*this};
  for (const FluidChange& change : result.changes) {
    const ChunkSectionCoord coord = block_to_chunk(change.pos);
    auto [accepted, inserted] = accepted_chunks.emplace(coord, false);
    if (inserted) {
      const voxel::ChunkSection* chunk = search.find_chunk(coord);
      accepted->second =
          chunk != nullptr && chunk->content_version() == change.source_content_version;
    }

    if (!accepted->second) {
      impl_->fluid.enqueue_update(
          change.pos,
          impl_->tick_index,
          FluidUpdateReason::NeighborChanged,
          0U);
      continue;
    }

    (void)impl_->set_fluid_cell(*this, change.pos, change.fluid, definition, false);
  }

  for (const FluidUpdate& update : result.followup_updates) {
    impl_->fluid.enqueue_update_unique(update);
  }

  for (const FluidUpdate& update : result.deferred_updates) {
    const ChunkSectionCoord coord = block_to_chunk(update.pos);
    if (search.is_chunk_active(coord)) {
      impl_->fluid.enqueue_update_unique(update);
    } else {
      impl_->fluid.store_deferred_boundary_update(coord, update);
    }
  }

  return result.processed;
}

} // namespace arv::world
