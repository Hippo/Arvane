#include "arv/sched/job_system.hpp"

#include "arv/core/atomic.hpp"

#include "arv/core/arena.hpp"
#include "arv/core/log.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/sched/completion_queue.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#include <taskflow/taskflow.hpp>

namespace arv::sched {
namespace {

constexpr usize LaneCount = 3U;
constexpr usize DomainCount = 11U;
constexpr usize CompletionTargetCount = 5U;

[[nodiscard]] usize lane_index(JobLane lane) noexcept {
  switch (lane) {
    case JobLane::CriticalCpu:
      return 0U;
    case JobLane::BackgroundCpu:
      return 1U;
    case JobLane::BlockingIo:
      return 2U;
  }
  return 1U;
}

[[nodiscard]] usize domain_index(JobDomain domain) noexcept {
  switch (domain) {
    case JobDomain::Generic:
      return 0U;
    case JobDomain::ChunkGeneration:
      return 1U;
    case JobDomain::Meshing:
      return 2U;
    case JobDomain::Lighting:
      return 3U;
    case JobDomain::Liquids:
      return 4U;
    case JobDomain::EntityAi:
      return 5U;
    case JobDomain::Pathfinding:
      return 6U;
    case JobDomain::SaveLoad:
      return 7U;
    case JobDomain::Assets:
      return 8U;
    case JobDomain::Networking:
      return 9U;
    case JobDomain::Rendering:
      return 10U;
  }
  return 0U;
}

[[nodiscard]] usize completion_target_index(JobCompletionTarget target) noexcept {
  switch (target) {
    case JobCompletionTarget::None:
      return 0U;
    case JobCompletionTarget::Main:
      return 1U;
    case JobCompletionTarget::World:
      return 2U;
    case JobCompletionTarget::Render:
      return 3U;
    case JobCompletionTarget::Network:
      return 4U;
  }
  return 0U;
}

[[nodiscard]] usize hardware_thread_count_or_one_impl() noexcept {
  const unsigned hardware_threads = std::thread::hardware_concurrency();
  return hardware_threads == 0U ? 1U : static_cast<usize>(hardware_threads);
}

[[nodiscard]] u32 hardware_thread_count_or_four() noexcept {
  const unsigned hardware_threads = std::thread::hardware_concurrency();
  return hardware_threads == 0U ? 4U : static_cast<u32>(hardware_threads);
}

[[nodiscard]] JobSystemConfig resolve_config(JobSystemConfig config) noexcept {
  const u32 hardware_threads = hardware_thread_count_or_four();
  if (config.critical_cpu_workers == 0U && config.background_cpu_workers == 0U) {
    constexpr u32 ReservedOwnerThreads = 2U;
    const u32 usable_workers =
        std::max<u32>(2U, hardware_threads > ReservedOwnerThreads
                              ? hardware_threads - ReservedOwnerThreads
                              : 2U);
    config.critical_cpu_workers = hardware_threads >= 8U ? 2U : 1U;
    config.background_cpu_workers =
        std::max<u32>(1U, usable_workers > config.critical_cpu_workers
                              ? usable_workers - config.critical_cpu_workers
                              : 1U);
  } else {
    if (config.critical_cpu_workers == 0U) {
      config.critical_cpu_workers = hardware_threads >= 8U ? 2U : 1U;
    }
    if (config.background_cpu_workers == 0U) {
      config.background_cpu_workers = 1U;
    }
  }
  config.blocking_io_workers = std::max<u32>(config.blocking_io_workers, 1U);
  return config;
}

[[nodiscard]] const char* lane_thread_name(JobLane lane) noexcept {
  switch (lane) {
    case JobLane::CriticalCpu:
      return "worker/critical";
    case JobLane::BackgroundCpu:
      return "worker/background";
    case JobLane::BlockingIo:
      return "worker/io";
  }
  return "worker";
}

} // namespace

const char* job_lane_name(JobLane lane) noexcept {
  switch (lane) {
    case JobLane::CriticalCpu:
      return "CriticalCpu";
    case JobLane::BackgroundCpu:
      return "BackgroundCpu";
    case JobLane::BlockingIo:
      return "BlockingIo";
  }
  return "Unknown";
}

const char* job_domain_name(JobDomain domain) noexcept {
  switch (domain) {
    case JobDomain::Generic:
      return "Generic";
    case JobDomain::ChunkGeneration:
      return "ChunkGeneration";
    case JobDomain::Meshing:
      return "Meshing";
    case JobDomain::Lighting:
      return "Lighting";
    case JobDomain::Liquids:
      return "Liquids";
    case JobDomain::EntityAi:
      return "EntityAi";
    case JobDomain::Pathfinding:
      return "Pathfinding";
    case JobDomain::SaveLoad:
      return "SaveLoad";
    case JobDomain::Assets:
      return "Assets";
    case JobDomain::Networking:
      return "Networking";
    case JobDomain::Rendering:
      return "Rendering";
  }
  return "Unknown";
}

const char* job_completion_target_name(JobCompletionTarget target) noexcept {
  switch (target) {
    case JobCompletionTarget::None:
      return "None";
    case JobCompletionTarget::Main:
      return "Main";
    case JobCompletionTarget::World:
      return "World";
    case JobCompletionTarget::Render:
      return "Render";
    case JobCompletionTarget::Network:
      return "Network";
  }
  return "Unknown";
}

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> canceled) noexcept
    : canceled_(std::move(canceled)) {
}

bool CancellationToken::is_canceled() const noexcept {
  return canceled_ != nullptr && canceled_->load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : canceled_(std::make_shared<std::atomic_bool>(false)) {
}

CancellationToken CancellationSource::token() const noexcept {
  return CancellationToken{canceled_};
}

void CancellationSource::cancel() noexcept {
  canceled_->store(true, std::memory_order_release);
}

usize hardware_thread_count_or_one() noexcept {
  return hardware_thread_count_or_one_impl();
}

usize resolve_worker_thread_count(WorkerThreadConfig config) noexcept {
  const usize min_workers = std::max<usize>(config.min_worker_thread_count, 1U);
  const usize max_workers = std::max<usize>(config.max_worker_thread_count, min_workers);

  usize requested = config.worker_thread_count;
  if (requested == 0U) {
    const usize hardware_threads = hardware_thread_count_or_one();
    requested = hardware_threads > config.reserved_thread_count
                    ? hardware_threads - config.reserved_thread_count
                    : 1U;
  }

  return std::clamp(requested, min_workers, max_workers);
}

RuntimeThreadBudgetPlan plan_runtime_thread_budget(
    RuntimeThreadBudgetConfig config) noexcept {
  RuntimeThreadBudgetPlan plan;
  plan.hardware_thread_count = hardware_thread_count_or_one();
  plan.reserved_thread_count = config.workers.reserved_thread_count;

  WorkerThreadConfig total_config = config.workers;
  total_config.reserved_thread_count = plan.reserved_thread_count;
  plan.total_worker_thread_count = resolve_worker_thread_count(total_config);
  if (config.split_primary_secondary_pools) {
    plan.total_worker_thread_count = std::max<usize>(plan.total_worker_thread_count, 2U);
  }

  if (!config.split_primary_secondary_pools || plan.total_worker_thread_count <= 1U) {
    plan.primary_worker_thread_count = plan.total_worker_thread_count;
    plan.secondary_worker_thread_count = 0U;
  } else {
    plan.primary_worker_thread_count =
        std::max<usize>(plan.total_worker_thread_count / 2U, 1U);
    plan.secondary_worker_thread_count =
        plan.total_worker_thread_count - plan.primary_worker_thread_count;
    if (plan.secondary_worker_thread_count == 0U) {
      plan.secondary_worker_thread_count = 1U;
      plan.primary_worker_thread_count =
          plan.primary_worker_thread_count > 1U ? plan.primary_worker_thread_count - 1U : 1U;
    }
  }

  auto explicit_pool_config =
      [reserved = plan.reserved_thread_count](usize worker_count) noexcept {
        WorkerThreadConfig pool;
        pool.worker_thread_count = std::max<usize>(worker_count, 1U);
        pool.reserved_thread_count = reserved;
        pool.min_worker_thread_count = 1U;
        pool.max_worker_thread_count = pool.worker_thread_count;
        return pool;
      };

  plan.primary_workers = explicit_pool_config(plan.primary_worker_thread_count);
  plan.secondary_workers = explicit_pool_config(
      plan.secondary_worker_thread_count == 0U
          ? plan.primary_worker_thread_count
          : plan.secondary_worker_thread_count);
  return plan;
}

struct JobSystem::Impl {
  struct JobRecord {
    JobHandle handle{};
    std::shared_ptr<std::atomic_bool> canceled{};
    bool domain_acquired{};
  };

  explicit Impl(JobSystemConfig requested_config)
      : config(resolve_config(requested_config)),
        critical_executor(config.critical_cpu_workers),
        background_executor(config.background_cpu_workers),
        blocking_io_executor(config.blocking_io_workers) {
    domain_budgets[domain_index(JobDomain::ChunkGeneration)] =
        config.max_chunk_gen_in_flight;
    domain_budgets[domain_index(JobDomain::Meshing)] =
        config.max_mesh_jobs_in_flight;
    domain_budgets[domain_index(JobDomain::Lighting)] =
        config.max_lighting_jobs_in_flight;
    domain_budgets[domain_index(JobDomain::Liquids)] =
        config.max_liquid_jobs_in_flight;
    domain_budgets[domain_index(JobDomain::EntityAi)] =
        config.max_ai_jobs_in_flight;
    domain_budgets[domain_index(JobDomain::Pathfinding)] =
        config.max_pathfinding_jobs_in_flight;
  }

  [[nodiscard]] tf::Executor& executor_for(JobLane lane) noexcept {
    switch (lane) {
      case JobLane::CriticalCpu:
        return critical_executor;
      case JobLane::BackgroundCpu:
        return background_executor;
      case JobLane::BlockingIo:
        return blocking_io_executor;
    }
    return background_executor;
  }

  [[nodiscard]] const tf::Executor& executor_for(JobLane lane) const noexcept {
    switch (lane) {
      case JobLane::CriticalCpu:
        return critical_executor;
      case JobLane::BackgroundCpu:
        return background_executor;
      case JobLane::BlockingIo:
        return blocking_io_executor;
    }
    return background_executor;
  }

  [[nodiscard]] bool try_acquire_domain(JobDomain domain) noexcept {
    const usize index = domain_index(domain);
    const u32 budget = domain_budgets[index];
    u32 current = domain_in_flight[index].load(std::memory_order_relaxed);
    for (;;) {
      if (budget != 0U && current >= budget) {
        return false;
      }
      if (domain_in_flight[index].compare_exchange_weak(
              current,
              current + 1U,
              std::memory_order_relaxed,
              std::memory_order_relaxed)) {
        return true;
      }
    }
  }

  void release_domain(JobDomain domain) noexcept {
    const usize index = domain_index(domain);
    u32 current = domain_in_flight[index].load(std::memory_order_relaxed);
    while (current > 0U &&
           !domain_in_flight[index].compare_exchange_weak(
               current,
               current - 1U,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
  }

  [[nodiscard]] u32 queued_total() const noexcept {
    u32 total = 0U;
    for (const std::atomic<u32>& queued : queued_jobs) {
      total += queued.load(std::memory_order_relaxed);
    }
    return total;
  }

  [[nodiscard]] u32 running_total() const noexcept {
    u32 total = 0U;
    for (const std::atomic<u32>& running : running_jobs) {
      total += running.load(std::memory_order_relaxed);
    }
    return total;
  }

  void record_submitted(JobLane lane) noexcept {
    submitted.fetch_add(1U, std::memory_order_relaxed);
    const usize index = lane_index(lane);
    queued_jobs[index].fetch_add(1U, std::memory_order_relaxed);
    core::update_atomic_maximum(peak_queued_jobs, queued_total());
  }

  void record_started(JobLane lane) noexcept {
    started.fetch_add(1U, std::memory_order_relaxed);
    const usize index = lane_index(lane);
    u32 queued = queued_jobs[index].load(std::memory_order_relaxed);
    while (queued > 0U &&
           !queued_jobs[index].compare_exchange_weak(
               queued,
               queued - 1U,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    running_jobs[index].fetch_add(1U, std::memory_order_relaxed);
  }

  void record_finished(const JobRecord& record, bool failed) noexcept {
    const usize index = lane_index(record.handle.lane);
    u32 running = running_jobs[index].load(std::memory_order_relaxed);
    while (running > 0U &&
           !running_jobs[index].compare_exchange_weak(
               running,
               running - 1U,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    if (failed) {
      this->failed.fetch_add(1U, std::memory_order_relaxed);
    } else {
      completed.fetch_add(1U, std::memory_order_relaxed);
    }
    if (record.domain_acquired) {
      release_domain(record.handle.domain);
    }
    {
      std::lock_guard lock{records_mutex};
      records.erase(record.handle.id);
    }
  }

  void enqueue_completion(JobCompletionTarget target, std::function<void()> completion) {
    if (target == JobCompletionTarget::None || !completion) {
      return;
    }
    completion_queues[completion_target_index(target)].push(std::move(completion));
  }

  void log_configuration_once() {
    bool expected = false;
    if (!logged_configuration.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return;
    }

    core::info(
        "jobs",
        "JobSystem: CriticalCpu workers={} BackgroundCpu workers={} BlockingIo workers={}",
        config.critical_cpu_workers,
        config.background_cpu_workers,
        config.blocking_io_workers);
    core::info(
        "jobs",
        "JobSystem: budgets chunk_gen={} meshing={} lighting={} liquids={} entity_ai={} pathfinding={}",
        config.max_chunk_gen_in_flight,
        config.max_mesh_jobs_in_flight,
        config.max_lighting_jobs_in_flight,
        config.max_liquid_jobs_in_flight,
        config.max_ai_jobs_in_flight,
        config.max_pathfinding_jobs_in_flight);
  }

  JobSystemConfig config{};
  tf::Executor critical_executor;
  tf::Executor background_executor;
  tf::Executor blocking_io_executor;
  std::atomic_bool accepting{true};
  std::atomic_bool logged_configuration{};
  std::atomic<u64> next_id{1};
  std::atomic<u64> submitted{};
  std::atomic<u64> started{};
  std::atomic<u64> completed{};
  std::atomic<u64> canceled{};
  std::atomic<u64> failed{};
  std::array<std::atomic<u32>, LaneCount> queued_jobs{};
  std::array<std::atomic<u32>, LaneCount> running_jobs{};
  std::atomic<u32> peak_queued_jobs{};
  std::array<u32, DomainCount> domain_budgets{};
  std::array<std::atomic<u32>, DomainCount> domain_in_flight{};
  std::mutex records_mutex;
  StableHashMap<u64, std::shared_ptr<JobRecord>> records;
  std::array<CompletionQueue<std::function<void()>>, CompletionTargetCount> completion_queues{};
};

JobSystem::JobSystem(JobSystemConfig config) : impl_(std::make_unique<Impl>(config)) {
}

JobSystem::~JobSystem() {
  request_stop();
  join();
}

JobSystem::JobSystem(JobSystem&&) noexcept = default;

JobSystem& JobSystem::operator=(JobSystem&&) noexcept = default;

void JobSystem::start() {
  impl_->accepting.store(true, std::memory_order_release);
  impl_->log_configuration_once();
}

void JobSystem::request_stop() {
  if (!impl_) {
    return;
  }
  impl_->accepting.store(false, std::memory_order_release);
  std::lock_guard lock{impl_->records_mutex};
  for (auto& [id, record] : impl_->records) {
    (void)id;
    record->canceled->store(true, std::memory_order_release);
  }
}

void JobSystem::join() {
  if (!impl_) {
    return;
  }
  impl_->critical_executor.wait_for_all();
  impl_->background_executor.wait_for_all();
  impl_->blocking_io_executor.wait_for_all();
}

JobHandle JobSystem::submit(
    JobDesc desc,
    std::function<void(const CancellationToken&)> work) {
  if (!impl_->accepting.load(std::memory_order_acquire) || !work) {
    return {};
  }
  if (!impl_->try_acquire_domain(desc.domain)) {
    return {};
  }

  const JobHandle handle{
      impl_->next_id.fetch_add(1U, std::memory_order_relaxed),
      desc.lane,
      desc.domain};
  auto record = std::make_shared<Impl::JobRecord>();
  record->handle = handle;
  record->canceled = std::make_shared<std::atomic_bool>(false);
  record->domain_acquired = true;

  {
    std::lock_guard lock{impl_->records_mutex};
    impl_->records.emplace(handle.id, record);
  }

  impl_->record_submitted(desc.lane);
  std::chrono::steady_clock::time_point submitted_at{};
  if constexpr (core::perf_stats_enabled()) {
    submitted_at = std::chrono::steady_clock::now();
  }
  const JobLane lane = desc.lane;
  std::string job_name = std::move(desc.name);
  auto wrapped = [this,
                  record,
                  lane,
                  domain = desc.domain,
                  submitted_at,
                  job_name,
                  work = std::move(work)]() mutable {
    static thread_local bool named_thread = false;
    if (!named_thread) {
      core::set_profile_thread_name(lane_thread_name(lane));
      named_thread = true;
    }

    ARV_PROFILE_ZONE_DYNAMIC(job_name.data(), job_name.size());
    worker_thread_arena().reset();
    impl_->record_started(lane);
    bool failed = false;
    try {
      if constexpr (core::perf_stats_enabled()) {
        const auto started_at = std::chrono::steady_clock::now();
        core::record_thread_timing(
            core::PerfMetric::JobQueueLatency,
            std::chrono::duration<double, std::milli>(started_at - submitted_at).count(),
            lane_thread_name(lane),
            job_name.empty() ? nullptr : job_name.c_str());
      }
      work(CancellationToken{record->canceled});
    } catch (const std::exception& ex) {
      failed = true;
      core::error(
          "jobs",
          "job '{}' failed lane={} domain={}: {}",
          job_name.empty() ? "<unnamed>" : job_name,
          job_lane_name(lane),
          job_domain_name(domain),
          ex.what());
    } catch (...) {
      failed = true;
      core::error(
          "jobs",
          "job '{}' failed lane={} domain={} with an unknown exception",
          job_name.empty() ? "<unnamed>" : job_name,
          job_lane_name(lane),
          job_domain_name(domain));
    }
    impl_->record_finished(*record, failed);
  };

  try {
    if (job_name.empty()) {
      impl_->executor_for(desc.lane).async(std::move(wrapped));
    } else {
      impl_->executor_for(desc.lane).async(tf::TaskParams{std::move(job_name)}, std::move(wrapped));
    }
  } catch (...) {
    impl_->record_finished(*record, true);
    throw;
  }

  return handle;
}

JobHandle JobSystem::submit_with_completion(
    JobDesc desc,
    std::function<void(const CancellationToken&)> work,
    std::function<void()> completion) {
  const JobCompletionTarget target = desc.completion_target;
  return submit(
      std::move(desc),
      [this,
       target,
       work = std::move(work),
       completion = std::move(completion)](const CancellationToken& token) mutable {
        work(token);
        impl_->enqueue_completion(target, std::move(completion));
      });
}

void JobSystem::cancel(JobHandle handle) noexcept {
  if (!handle.valid() || !impl_) {
    return;
  }

  std::shared_ptr<Impl::JobRecord> record;
  {
    std::lock_guard lock{impl_->records_mutex};
    const auto found = impl_->records.find(handle.id);
    if (found == impl_->records.end()) {
      return;
    }
    record = found->second;
  }

  bool expected = false;
  if (record->canceled->compare_exchange_strong(
          expected,
          true,
          std::memory_order_acq_rel,
          std::memory_order_acquire)) {
    impl_->canceled.fetch_add(1U, std::memory_order_relaxed);
  }
}

void JobSystem::pump_completions(JobCompletionTarget target, u32 max_count) {
  if (target == JobCompletionTarget::None || max_count == 0U) {
    return;
  }

  CompletionQueue<std::function<void()>>& queue =
      impl_->completion_queues[completion_target_index(target)];
  for (u32 pumped = 0; pumped < max_count; ++pumped) {
    std::function<void()> completion;
    if (!queue.try_pop(completion)) {
      return;
    }
    completion();
  }
}

JobStats JobSystem::stats() const {
  JobStats stats;
  stats.submitted = impl_->submitted.load(std::memory_order_relaxed);
  stats.started = impl_->started.load(std::memory_order_relaxed);
  stats.completed = impl_->completed.load(std::memory_order_relaxed);
  stats.canceled = impl_->canceled.load(std::memory_order_relaxed);
  stats.failed = impl_->failed.load(std::memory_order_relaxed);
  stats.queued_critical_cpu =
      impl_->queued_jobs[lane_index(JobLane::CriticalCpu)].load(std::memory_order_relaxed);
  stats.queued_background_cpu =
      impl_->queued_jobs[lane_index(JobLane::BackgroundCpu)].load(std::memory_order_relaxed);
  stats.queued_blocking_io =
      impl_->queued_jobs[lane_index(JobLane::BlockingIo)].load(std::memory_order_relaxed);
  stats.running_critical_cpu =
      impl_->running_jobs[lane_index(JobLane::CriticalCpu)].load(std::memory_order_relaxed);
  stats.running_background_cpu =
      impl_->running_jobs[lane_index(JobLane::BackgroundCpu)].load(std::memory_order_relaxed);
  stats.running_blocking_io =
      impl_->running_jobs[lane_index(JobLane::BlockingIo)].load(std::memory_order_relaxed);
  stats.critical_cpu_workers = impl_->config.critical_cpu_workers;
  stats.background_cpu_workers = impl_->config.background_cpu_workers;
  stats.blocking_io_workers = impl_->config.blocking_io_workers;
  return stats;
}

JobSystemPressureSnapshot JobSystem::pressure_snapshot() const noexcept {
  JobSystemPressureSnapshot snapshot;
  snapshot.worker_count = total_worker_count();
  snapshot.current_queued_jobs = impl_->queued_total();
  snapshot.peak_queued_jobs = impl_->peak_queued_jobs.load(std::memory_order_relaxed);
  snapshot.active_workers = impl_->running_total();
  snapshot.submitted_jobs_total = impl_->submitted.load(std::memory_order_relaxed);
  snapshot.completed_jobs_total =
      impl_->completed.load(std::memory_order_relaxed) +
      impl_->failed.load(std::memory_order_relaxed);
  if (snapshot.active_workers > snapshot.worker_count) {
    snapshot.active_workers = static_cast<u32>(snapshot.worker_count);
  }
  snapshot.idle_workers =
      snapshot.worker_count > snapshot.active_workers
          ? static_cast<u32>(snapshot.worker_count - snapshot.active_workers)
          : 0U;
  snapshot.idle_percent =
      snapshot.worker_count == 0U
          ? 0.0
          : (static_cast<double>(snapshot.idle_workers) /
             static_cast<double>(snapshot.worker_count)) *
                100.0;
  return snapshot;
}

void JobSystem::reset_pressure_stats() noexcept {
  impl_->peak_queued_jobs.store(impl_->queued_total(), std::memory_order_relaxed);
  impl_->submitted.store(0U, std::memory_order_relaxed);
  impl_->completed.store(0U, std::memory_order_relaxed);
  impl_->failed.store(0U, std::memory_order_relaxed);
}

usize JobSystem::worker_count(JobLane lane) const noexcept {
  return impl_->executor_for(lane).num_workers();
}

usize JobSystem::total_worker_count() const noexcept {
  return worker_count(JobLane::CriticalCpu) +
         worker_count(JobLane::BackgroundCpu) +
         worker_count(JobLane::BlockingIo);
}

} // namespace arv::sched
