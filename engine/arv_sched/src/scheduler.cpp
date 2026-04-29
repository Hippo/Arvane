#include "arv/sched/scheduler.hpp"

namespace arv::sched {

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

void Scheduler::wait_for_all() {
  executor_.wait_for_all();
}

usize Scheduler::default_worker_count() noexcept {
  return resolve_worker_thread_count({});
}

} // namespace arv::sched
