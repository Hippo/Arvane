#include "server_runtime_internal.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/world/fluid_solver.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace arv::server::detail {

ServerWorldJobRuntime::ServerWorldJobRuntime() : job_system_(std::make_unique<sched::JobSystem>()) {
}

void ServerWorldJobRuntime::configure(const ServerStartInfo& start_info) {
  const sched::JobSystemConfig requested_job_config = start_info.job_system;
  max_pending_generation_jobs_ =
      std::max<u32>(
          requested_job_config.max_chunk_gen_in_flight == 0U
              ? start_info.max_pending_generation_jobs
              : requested_job_config.max_chunk_gen_in_flight,
          1U);
  max_generation_results_per_tick_ =
      std::max<u32>(start_info.max_generation_results_per_tick, 1U);
  max_lighting_results_per_tick_ =
      std::max<u32>(start_info.max_lighting_results_per_tick, 1U);
  max_lighting_sections_per_job_ =
      std::max<u32>(start_info.max_lighting_sections_per_job, 1U);
  max_fluid_results_per_tick_ =
      std::max<u32>(start_info.max_fluid_results_per_tick, 1U);
  max_pending_lighting_jobs_ =
      std::max<u32>(
          requested_job_config.max_lighting_jobs_in_flight == 0U
              ? max_pending_lighting_jobs_
              : requested_job_config.max_lighting_jobs_in_flight,
          1U);
  max_pending_fluid_jobs_ =
      std::max<u32>(
          requested_job_config.max_liquid_jobs_in_flight == 0U
              ? start_info.max_pending_fluid_jobs
              : requested_job_config.max_liquid_jobs_in_flight,
          1U);
  max_pending_natural_spawn_jobs_ =
      std::max<u32>(
          requested_job_config.max_ai_jobs_in_flight == 0U
              ? 1U
              : requested_job_config.max_ai_jobs_in_flight,
          1U);
  fluid_update_interval_ = start_info.world_tick_policy.fluid_interval;
  fluid_update_budget_ = start_info.world_tick_policy.fluid_budget == 0U
                             ? world::WorldTickPolicy{}.fluid_budget
                             : start_info.world_tick_policy.fluid_budget;
  const usize configured_workers = sched::resolve_worker_thread_count(start_info.worker_threads);
  const u32 critical_workers = requested_job_config.critical_cpu_workers == 0U
                                   ? 1U
                                   : requested_job_config.critical_cpu_workers;
  sched::JobSystemConfig job_config;
  job_config.critical_cpu_workers = critical_workers;
  job_config.background_cpu_workers = requested_job_config.background_cpu_workers == 0U
                                          ? static_cast<u32>(std::max<usize>(
                                                1U,
                                                configured_workers > critical_workers
                                                    ? configured_workers - critical_workers
                                                    : 1U))
                                          : requested_job_config.background_cpu_workers;
  job_config.blocking_io_workers = requested_job_config.blocking_io_workers == 0U
                                       ? 1U
                                       : requested_job_config.blocking_io_workers;
  job_config.max_chunk_gen_in_flight = max_pending_generation_jobs_;
  job_config.max_lighting_jobs_in_flight = max_pending_lighting_jobs_;
  job_config.max_liquid_jobs_in_flight = max_pending_fluid_jobs_;
  job_config.max_ai_jobs_in_flight =
      requested_job_config.max_ai_jobs_in_flight == 0U
          ? 1U
          : requested_job_config.max_ai_jobs_in_flight;
  job_config.max_pathfinding_jobs_in_flight =
      requested_job_config.max_pathfinding_jobs_in_flight == 0U
          ? 1U
          : requested_job_config.max_pathfinding_jobs_in_flight;
  job_system_ = std::make_unique<sched::JobSystem>(job_config);
  job_system_->start();
}

void ServerWorldJobRuntime::wait_for_all() {
  if (job_system_) {
    job_system_->request_stop();
    job_system_->join();
  }
}

sched::JobSystem* ServerWorldJobRuntime::job_system() noexcept {
  return job_system_.get();
}

const sched::JobSystem* ServerWorldJobRuntime::job_system() const noexcept {
  return job_system_.get();
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

usize ServerWorldJobRuntime::pending_natural_spawn_job_count() const noexcept {
  return pending_natural_spawn_jobs_.size();
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
    bool column_still_generating = false;
    const world::WorldHeightSettings height = world.height_settings();
    for (i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
      const ChunkSectionCoord section_coord = chunk_section(pending.coord, y);
      const voxel::ChunkSection* section = world.find_chunk(section_coord);
      column_still_generating =
          column_still_generating ||
          (section != nullptr &&
           section->lifecycle_state() == voxel::ChunkLifecycleState::Generating);
    }
    if (generated && column_still_generating) {
      (void)world.publish_generated_chunk(std::move(generated.value()));
    } else if (column_still_generating) {
      for (i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
        const ChunkSectionCoord section_coord = chunk_section(pending.coord, y);
        const voxel::ChunkSection* section = world.find_chunk(section_coord);
        if (section != nullptr &&
            section->lifecycle_state() == voxel::ChunkLifecycleState::Generating) {
          (void)world.set_chunk_lifecycle_state(
              section_coord,
              voxel::ChunkLifecycleState::Requested);
        }
      }
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

  if (pending_generation_jobs_.size() >= max_pending_generation_jobs_) {
    return;
  }
  const usize available_slots =
      static_cast<usize>(max_pending_generation_jobs_) - pending_generation_jobs_.size();
  const std::vector<ChunkCoord> candidates =
      chunk_streamer.pending_generation_requests(world, available_slots);
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

    const world::WorldHeightSettings height = world.height_settings();
    bool marked_any = false;
    for (i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
      const ChunkSectionCoord section_coord = chunk_section(coord, y);
      voxel::ChunkSection* section = world.find_chunk(section_coord);
      if (section == nullptr) {
        (void)world.ensure_chunk_section(
            section_coord,
            voxel::ChunkLifecycleState::Generating);
        marked_any = true;
        continue;
      }
      if (section->lifecycle_state() != voxel::ChunkLifecycleState::Requested) {
        continue;
      }
      if (world.set_chunk_lifecycle_state(
              section_coord,
              voxel::ChunkLifecycleState::Generating)) {
        marked_any = true;
      }
    }
    if (!marked_any) {
      continue;
    }

    world::ChunkGenerationRequest request;
    request.world_id = world.id();
    request.seed = chunk_generation->seed;
    request.coord = coord;
    request.min_chunk_y = height.min_chunk_y;
    request.max_chunk_y = height.max_chunk_y;
    request.config = chunk_generation->config;
    request.palette = chunk_generation->palette;
    sched::JobDesc desc;
    desc.name = "server_world_generation";
    desc.lane = sched::JobLane::BackgroundCpu;
    desc.domain = sched::JobDomain::ChunkGeneration;
    desc.completion_target = sched::JobCompletionTarget::World;
    auto job = job_system_->submit_result(
        std::move(desc),
        [request = std::move(request)](const sched::CancellationToken& token) {
          if (token.is_canceled()) {
            return world::GeneratedChunkResult::failure(
                world::WorldGenerationError{"chunk generation canceled"});
          }
          return world::generate_chunk(request);
        });
    if (!job.valid()) {
      for (i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
        const ChunkSectionCoord section_coord = chunk_section(coord, y);
        const voxel::ChunkSection* section = world.find_chunk(section_coord);
        if (section != nullptr &&
            section->lifecycle_state() == voxel::ChunkLifecycleState::Generating) {
          (void)world.set_chunk_lifecycle_state(
              section_coord,
              voxel::ChunkLifecycleState::Requested);
        }
      }
      continue;
    }
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
  sched::JobDesc desc;
  desc.name = "server_world_fluids";
  desc.lane = sched::JobLane::CriticalCpu;
  desc.domain = sched::JobDomain::Liquids;
  desc.completion_target = sched::JobCompletionTarget::World;
  auto job = job_system_->submit_result(
      std::move(desc),
      [request = std::move(request.value()), registry, definition]() mutable {
        return world::FluidSolver::build_simulation_result(
            std::move(request),
            *registry,
            definition);
      });
  if (!job.valid()) {
    return;
  }
  pending_fluid_jobs_.emplace_back(std::move(job));
}

void ServerWorldJobRuntime::drain_natural_spawn_jobs(
    world::WorldRuntime& world,
    world::NaturalSpawnSystem& natural_spawns,
    const voxel::BlockRegistry* block_registry,
    const entity::EntityRegistry* entity_registry,
    const entity::VehicleDefinitionRegistry* vehicle_registry,
    TickIndex tick,
    bool drain_all) {
  ARV_PROFILE_ZONE("Natural spawning");
  if (block_registry == nullptr || entity_registry == nullptr) {
    natural_spawns.record_pending_job_count(
        static_cast<u32>(pending_natural_spawn_jobs_.size()));
    return;
  }

  u32 drained = 0;
  for (usize index = 0;
       index < pending_natural_spawn_jobs_.size() &&
       (drain_all || drained < max_natural_spawn_results_per_tick_);) {
    PendingNaturalSpawnJob& pending = pending_natural_spawn_jobs_[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    world::NaturalSpawnJobResult result = pending.job.get();
    (void)natural_spawns.apply_job_result(
        world,
        *block_registry,
        *entity_registry,
        result,
        tick,
        true,
        vehicle_registry);
    pending_natural_spawn_jobs_.erase(
        pending_natural_spawn_jobs_.begin() + static_cast<isize>(index));
    ++drained;
  }

  natural_spawns.record_pending_job_count(
      static_cast<u32>(pending_natural_spawn_jobs_.size()));
}

void ServerWorldJobRuntime::schedule_natural_spawn_jobs(
    world::WorldRuntime& world,
    world::NaturalSpawnSystem& natural_spawns,
    TickIndex tick) {
  ARV_PROFILE_ZONE("Natural spawning");
  if (job_system_ == nullptr ||
      pending_natural_spawn_jobs_.size() >= max_pending_natural_spawn_jobs_) {
    natural_spawns.record_pending_job_count(
        static_cast<u32>(pending_natural_spawn_jobs_.size()));
    return;
  }

  std::optional<world::NaturalSpawnJobRequest> request =
      natural_spawns.make_job_request(world, tick);
  if (!request.has_value()) {
    natural_spawns.record_pending_job_count(
        static_cast<u32>(pending_natural_spawn_jobs_.size()));
    return;
  }

  const TickIndex source_tick = request->tick;
  sched::JobDesc desc;
  desc.name = "server_natural_spawning";
  desc.lane = sched::JobLane::BackgroundCpu;
  desc.domain = sched::JobDomain::EntityAi;
  desc.completion_target = sched::JobCompletionTarget::World;
  auto job = job_system_->submit_result(
      std::move(desc),
      [request = std::move(request.value())](const sched::CancellationToken& token) mutable {
        if (token.is_canceled()) {
          world::NaturalSpawnJobResult result;
          result.world_id = request.world_id;
          result.source_tick = request.tick;
          result.canceled = true;
          return result;
        }
        return world::NaturalSpawnSystem::build_job_result(std::move(request));
      });
  if (!job.valid()) {
    natural_spawns.record_pending_job_count(
        static_cast<u32>(pending_natural_spawn_jobs_.size()));
    return;
  }

  pending_natural_spawn_jobs_.push_back({source_tick, std::move(job)});
  natural_spawns.record_job_submitted(
      static_cast<u32>(pending_natural_spawn_jobs_.size()));
}

void ServerWorldJobRuntime::schedule_lighting_jobs(
    world::WorldRuntime& world,
    const voxel::BlockRegistry* block_registry) {
  ARV_PROFILE_ZONE("Lighting propagation");
  if (block_registry == nullptr || !world.has_lighting_work()) {
    return;
  }
  if (pending_lighting_jobs_.size() >= max_pending_lighting_jobs_) {
    return;
  }

  const u64 current_revision = world.lighting_revision();
  const bool revision_in_flight = std::any_of(
      pending_lighting_jobs_.begin(),
      pending_lighting_jobs_.end(),
      [revision = current_revision](const PendingLightingJob& pending) {
        return pending.source_revision == revision;
      });
  if (revision_in_flight) {
    return;
  }

  world::LightingBatchSnapshot snapshot =
      world.make_lighting_batch_snapshot(max_lighting_sections_per_job_);
  if (snapshot.snapshots.empty()) {
    return;
  }
  const u64 source_revision = snapshot.source_revision;

  const voxel::BlockRegistry* registry = block_registry;
  core::record_lighting_job_submitted();
  sched::JobDesc desc;
  desc.name = "server_world_lighting";
  desc.lane = sched::JobLane::BackgroundCpu;
  desc.domain = sched::JobDomain::Lighting;
  desc.completion_target = sched::JobCompletionTarget::World;
  auto job = job_system_->submit_result(
      std::move(desc),
      [snapshot = std::move(snapshot), registry] {
        return world::WorldRuntime::build_lighting_result(snapshot, *registry);
      });
  if (!job.valid()) {
    return;
  }
  pending_lighting_jobs_.emplace_back(source_revision, std::move(job));
}

} // namespace arv::server::detail
