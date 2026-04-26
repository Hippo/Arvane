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
