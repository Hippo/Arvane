#include "arv/sched/scheduler.hpp"

namespace arv::sched {
namespace {

void update_peak(std::atomic<u32>& target, u32 value) noexcept {
  u32 current = target.load(std::memory_order_relaxed);
  while (current < value &&
         !target.compare_exchange_weak(
             current,
             value,
             std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

} // namespace

usize hardware_thread_count_or_one() noexcept {
  const unsigned hardware_threads = std::thread::hardware_concurrency();
  return hardware_threads == 0U ? 1U : static_cast<usize>(hardware_threads);
}

usize resolve_worker_thread_count(WorkerThreadConfig config) noexcept {
  const usize min_workers = std::max<usize>(config.min_worker_thread_count, 1);
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
  const usize named_role_threads =
      config.main_thread_count +
      config.simulation_thread_count +
      config.render_thread_count +
      config.network_thread_count;
  plan.reserved_thread_count =
      std::max(config.workers.reserved_thread_count, named_role_threads);

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

Scheduler::Scheduler(usize worker_count) : executor_(std::max<usize>(worker_count, 1)) {
}

Scheduler::Scheduler(WorkerThreadConfig config) : Scheduler(resolve_worker_thread_count(config)) {
}

Scheduler::~Scheduler() {
  wait_for_all();
}

usize Scheduler::worker_count() const noexcept {
  return executor_.num_workers();
}

SchedulerPressureSnapshot Scheduler::pressure_snapshot() const noexcept {
  SchedulerPressureSnapshot snapshot;
  snapshot.worker_count = worker_count();
  snapshot.current_queued_jobs = queued_jobs_.load(std::memory_order_relaxed);
  snapshot.peak_queued_jobs = peak_queued_jobs_.load(std::memory_order_relaxed);
  snapshot.active_workers = active_workers_.load(std::memory_order_relaxed);
  snapshot.submitted_jobs_total = submitted_jobs_total_.load(std::memory_order_relaxed);
  snapshot.completed_jobs_total = completed_jobs_total_.load(std::memory_order_relaxed);
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

void Scheduler::reset_pressure_stats() noexcept {
  peak_queued_jobs_.store(queued_jobs_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  submitted_jobs_total_.store(0U, std::memory_order_relaxed);
  completed_jobs_total_.store(0U, std::memory_order_relaxed);
}

void Scheduler::wait_for_all() {
  executor_.wait_for_all();
}

usize Scheduler::default_worker_count() noexcept {
  return resolve_worker_thread_count({});
}

void Scheduler::record_job_submitted() noexcept {
  submitted_jobs_total_.fetch_add(1U, std::memory_order_relaxed);
  const u32 queued = queued_jobs_.fetch_add(1U, std::memory_order_relaxed) + 1U;
  update_peak(peak_queued_jobs_, queued);
}

void Scheduler::record_job_started() noexcept {
  u32 queued = queued_jobs_.load(std::memory_order_relaxed);
  while (queued > 0U &&
         !queued_jobs_.compare_exchange_weak(
             queued,
             queued - 1U,
             std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
  active_workers_.fetch_add(1U, std::memory_order_relaxed);
}

void Scheduler::record_job_completed() noexcept {
  u32 active = active_workers_.load(std::memory_order_relaxed);
  while (active > 0U &&
         !active_workers_.compare_exchange_weak(
             active,
             active - 1U,
             std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
  completed_jobs_total_.fetch_add(1U, std::memory_order_relaxed);
}

} // namespace arv::sched
