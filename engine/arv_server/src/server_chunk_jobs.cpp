#include "server_runtime_internal.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/world/fluid_solver.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace arv::server::detail {

ServerWorldJobRuntime::ServerWorldJobRuntime()
    : scheduler_(std::make_unique<sched::Scheduler>()) {
}

void ServerWorldJobRuntime::configure(const ServerStartInfo& start_info) {
  max_pending_generation_jobs_ =
      std::max<u32>(start_info.max_pending_generation_jobs, 1U);
  max_generation_results_per_tick_ =
      std::max<u32>(start_info.max_generation_results_per_tick, 1U);
  max_lighting_results_per_tick_ =
      std::max<u32>(start_info.max_lighting_results_per_tick, 1U);
  max_fluid_results_per_tick_ =
      std::max<u32>(start_info.max_fluid_results_per_tick, 1U);
  scheduler_ = std::make_unique<sched::Scheduler>(start_info.worker_threads);
  max_pending_fluid_jobs_ = std::max<u32>(start_info.max_pending_fluid_jobs, 1U);
  fluid_update_interval_ = start_info.world_tick_policy.fluid_interval;
  fluid_update_budget_ = start_info.world_tick_policy.fluid_budget == 0U
                             ? world::WorldTickPolicy{}.fluid_budget
                             : start_info.world_tick_policy.fluid_budget;
}

void ServerWorldJobRuntime::wait_for_all() {
  if (scheduler_) {
    scheduler_->wait_for_all();
  }
}

sched::Scheduler* ServerWorldJobRuntime::scheduler() noexcept {
  return scheduler_.get();
}

const sched::Scheduler* ServerWorldJobRuntime::scheduler() const noexcept {
  return scheduler_.get();
}

usize ServerWorldJobRuntime::pending_generation_job_count() const noexcept {
  return pending_generation_jobs_.size();
}

usize ServerWorldJobRuntime::pending_lighting_job_count() const noexcept {
  return pending_lighting_jobs_.size();
}

usize ServerWorldJobRuntime::pending_fluid_job_count() const noexcept {
  return pending_fluid_jobs_.size();
}

void ServerWorldJobRuntime::drain_generation_jobs(world::WorldRuntime& world, bool drain_all) {
  ARV_PROFILE_ZONE("Chunk generation");
  u32 drained = 0;
  for (usize index = 0;
       index < pending_generation_jobs_.size() &&
       (drain_all || drained < max_generation_results_per_tick_);) {
    PendingGenerationJob& pending = pending_generation_jobs_[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    world::GeneratedChunkResult generated = pending.job.get();
    voxel::ChunkSection* chunk = world.find_chunk(pending.coord);
    if (generated &&
        chunk != nullptr &&
        chunk->lifecycle_state() == voxel::ChunkLifecycleState::Generating) {
      (void)world.publish_generated_chunk(std::move(generated.value()));
    } else if (chunk != nullptr &&
               chunk->lifecycle_state() == voxel::ChunkLifecycleState::Generating) {
      (void)world.set_chunk_lifecycle_state(
          pending.coord,
          voxel::ChunkLifecycleState::Requested);
    }

    pending_generation_jobs_.erase(
        pending_generation_jobs_.begin() + static_cast<isize>(index));
    ++drained;
  }
}

void ServerWorldJobRuntime::schedule_generation_jobs(
    world::WorldRuntime& world,
    const ServerChunkStreamer& chunk_streamer,
    const std::optional<ServerChunkGenerationSettings>& chunk_generation) {
  ARV_PROFILE_ZONE("Chunk generation");
  if (!chunk_generation.has_value() || !chunk_generation->enabled) {
    return;
  }

  const std::vector<ChunkCoord> candidates =
      chunk_streamer.pending_generation_requests(world);
  for (const ChunkCoord coord : candidates) {
    if (pending_generation_jobs_.size() >= max_pending_generation_jobs_) {
      return;
    }

    const bool already_pending = std::any_of(
        pending_generation_jobs_.begin(),
        pending_generation_jobs_.end(),
        [coord](const PendingGenerationJob& pending) {
          return pending.coord == coord;
        });
    if (already_pending) {
      continue;
    }

    voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr ||
        chunk->lifecycle_state() != voxel::ChunkLifecycleState::Requested) {
      continue;
    }

    if (!world.set_chunk_lifecycle_state(
            coord,
            voxel::ChunkLifecycleState::Generating)) {
      continue;
    }

    world::ChunkGenerationRequest request;
    request.world_id = world.id();
    request.seed = chunk_generation->seed;
    request.coord = coord;
    request.config = chunk_generation->config;
    request.palette = chunk_generation->palette;
    auto job = scheduler_->submit("server_world_generation", [request = std::move(request)] {
      return world::generate_chunk(request);
    });
    pending_generation_jobs_.emplace_back(coord, std::move(job));
  }
}

void ServerWorldJobRuntime::drain_lighting_jobs(world::WorldRuntime& world, bool drain_all) {
  ARV_PROFILE_ZONE("Lighting propagation");
  u32 drained = 0;
  for (usize index = 0;
       index < pending_lighting_jobs_.size() &&
       (drain_all || drained < max_lighting_results_per_tick_);) {
    PendingLightingJob& pending = pending_lighting_jobs_[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    world::LightingResult result = pending.job.get();
    (void)world.publish_lighting_result(std::move(result));
    pending_lighting_jobs_.erase(
        pending_lighting_jobs_.begin() + static_cast<isize>(index));
    ++drained;
  }
}

void ServerWorldJobRuntime::drain_fluid_jobs(
    world::WorldRuntime& world,
    const voxel::FluidDefinition& fluid_definition,
    bool drain_all) {
  u32 drained = 0;
  for (usize index = 0;
       index < pending_fluid_jobs_.size() &&
       (drain_all || drained < max_fluid_results_per_tick_);) {
    PendingFluidJob& pending = pending_fluid_jobs_[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    world::FluidSimulationResult result = pending.job.get();
    (void)world.apply_fluid_simulation_result(
        std::move(result),
        fluid_definition);
    pending_fluid_jobs_.erase(
        pending_fluid_jobs_.begin() + static_cast<isize>(index));
    ++drained;
  }
}

void ServerWorldJobRuntime::schedule_fluid_jobs(
    world::WorldRuntime& world,
    const voxel::BlockRegistry* block_registry,
    const voxel::FluidDefinition& fluid_definition) {
  if (block_registry == nullptr ||
      pending_fluid_jobs_.size() >= max_pending_fluid_jobs_) {
    return;
  }

  if (fluid_update_interval_ != 0U &&
      world.tick_index() % fluid_update_interval_ != 0U) {
    return;
  }
  std::optional<world::FluidSimulationRequest> request =
      world.make_fluid_simulation_request(fluid_update_budget_);
  if (!request.has_value()) {
    return;
  }

  const voxel::BlockRegistry* registry = block_registry;
  const voxel::FluidDefinition definition = fluid_definition;
  auto job = scheduler_->submit(
      "server_world_fluids",
      [request = std::move(request.value()), registry, definition]() mutable {
        return world::FluidSolver::build_simulation_result(
            std::move(request),
            *registry,
            definition);
      });
  pending_fluid_jobs_.emplace_back(std::move(job));
}

void ServerWorldJobRuntime::schedule_lighting_jobs(
    world::WorldRuntime& world,
    const voxel::BlockRegistry* block_registry) {
  ARV_PROFILE_ZONE("Lighting propagation");
  if (block_registry == nullptr || !world.has_lighting_work()) {
    return;
  }

  world::LightingSnapshot snapshot = world.make_lighting_snapshot();
  if (snapshot.chunks.empty()) {
    return;
  }
  const u64 source_revision = snapshot.source_revision;

  const bool revision_in_flight = std::any_of(
      pending_lighting_jobs_.begin(),
      pending_lighting_jobs_.end(),
      [revision = source_revision](const PendingLightingJob& pending) {
        return pending.source_revision == revision;
      });
  if (revision_in_flight || pending_lighting_jobs_.size() >= max_pending_lighting_jobs_) {
    return;
  }

  const voxel::BlockRegistry* registry = block_registry;
  core::record_lighting_job_submitted();
  auto job = scheduler_->submit("server_world_lighting", [snapshot = std::move(snapshot), registry] {
    return world::WorldRuntime::build_lighting_result(snapshot, *registry);
  });
  pending_lighting_jobs_.emplace_back(source_revision, std::move(job));
}

} // namespace arv::server::detail
