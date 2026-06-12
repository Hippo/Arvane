#include "arv/sched/task_executor.hpp"

#include "arv/core/log.hpp"
#include "arv/sched/completion_queue.hpp"

#include <array>
#include <atomic>
#include <deque>
#include <exception>
#include <memory>
#include <utility>

namespace arv::sched {
namespace {

constexpr usize LaneCount = 3U;
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

} // namespace

JobSystemTaskExecutor::JobSystemTaskExecutor(JobSystem& jobs) noexcept : jobs_(&jobs) {
}

JobHandle JobSystemTaskExecutor::submit(
    JobDesc desc,
    std::function<void(const CancellationToken&)> work) {
  return jobs_ == nullptr ? JobHandle{} : jobs_->submit(std::move(desc), std::move(work));
}

JobHandle JobSystemTaskExecutor::submit_with_completion(
    JobDesc desc,
    std::function<void(const CancellationToken&)> work,
    std::function<void()> completion) {
  return jobs_ == nullptr
             ? JobHandle{}
             : jobs_->submit_with_completion(
                   std::move(desc),
                   std::move(work),
                   std::move(completion));
}

void JobSystemTaskExecutor::cancel(JobHandle handle) noexcept {
  if (jobs_ != nullptr) {
    jobs_->cancel(handle);
  }
}

void JobSystemTaskExecutor::pump(TaskPumpBudget) {
}

void JobSystemTaskExecutor::pump_completions(
    JobCompletionTarget target,
    u32 max_count) {
  if (jobs_ != nullptr) {
    jobs_->pump_completions(target, max_count);
  }
}

JobStats JobSystemTaskExecutor::stats() const {
  return jobs_ == nullptr ? JobStats{} : jobs_->stats();
}

bool JobSystemTaskExecutor::supports_parallelism() const noexcept {
  return true;
}

struct SingleThreadTaskExecutor::Impl {
  struct TaskRecord {
    JobHandle handle{};
    JobCompletionTarget completion_target{JobCompletionTarget::None};
    std::shared_ptr<std::atomic_bool> canceled{};
    std::function<void(const CancellationToken&)> work{};
    std::function<void()> completion{};
    bool canceled_counted{};
  };

  explicit Impl(Clock next_clock)
      : clock(next_clock ? std::move(next_clock)
                         : Clock{[] {
                             return std::chrono::steady_clock::now();
                           }}) {
  }

  [[nodiscard]] std::chrono::steady_clock::time_point now() const {
    return clock();
  }

  void record_submitted(JobLane lane) noexcept {
    ++stats.submitted;
    ++queued[lane_index(lane)];
  }

  void record_started(JobLane lane) noexcept {
    ++stats.started;
    const usize index = lane_index(lane);
    if (queued[index] > 0U) {
      --queued[index];
    }
    ++running[index];
  }

  void record_finished(JobLane lane, bool failed) noexcept {
    const usize index = lane_index(lane);
    if (running[index] > 0U) {
      --running[index];
    }
    if (failed) {
      ++stats.failed;
    } else {
      ++stats.completed;
    }
  }

  void record_canceled(TaskRecord& record) noexcept {
    if (record.canceled_counted) {
      return;
    }
    record.canceled_counted = true;
    ++stats.canceled;
  }

  void enqueue_completion(
      JobCompletionTarget target,
      std::function<void()> completion) {
    if (target == JobCompletionTarget::None || !completion) {
      return;
    }
    completion_queues[completion_target_index(target)].push(std::move(completion));
  }

  Clock clock;
  bool accepting{true};
  u64 next_id{1};
  JobStats stats{};
  std::array<u32, LaneCount> queued{};
  std::array<u32, LaneCount> running{};
  std::deque<TaskRecord> tasks{};
  std::array<CompletionQueue<std::function<void()>>, CompletionTargetCount> completion_queues{};
};

SingleThreadTaskExecutor::SingleThreadTaskExecutor()
    : impl_(std::make_unique<Impl>(Clock{})) {
}

SingleThreadTaskExecutor::SingleThreadTaskExecutor(Clock clock)
    : impl_(std::make_unique<Impl>(std::move(clock))) {
}

SingleThreadTaskExecutor::~SingleThreadTaskExecutor() = default;

JobHandle SingleThreadTaskExecutor::submit(
    JobDesc desc,
    std::function<void(const CancellationToken&)> work) {
  if (!impl_->accepting || !work) {
    return {};
  }

  JobHandle handle{
      impl_->next_id++,
      desc.lane,
      desc.domain};
  Impl::TaskRecord record;
  record.handle = handle;
  record.completion_target = desc.completion_target;
  record.canceled = std::make_shared<std::atomic_bool>(false);
  record.work = std::move(work);
  impl_->tasks.push_back(std::move(record));
  impl_->record_submitted(desc.lane);
  return handle;
}

JobHandle SingleThreadTaskExecutor::submit_with_completion(
    JobDesc desc,
    std::function<void(const CancellationToken&)> work,
    std::function<void()> completion) {
  if (!impl_->accepting || !work) {
    return {};
  }

  JobHandle handle{
      impl_->next_id++,
      desc.lane,
      desc.domain};
  Impl::TaskRecord record;
  record.handle = handle;
  record.completion_target = desc.completion_target;
  record.canceled = std::make_shared<std::atomic_bool>(false);
  record.work = std::move(work);
  record.completion = std::move(completion);
  impl_->tasks.push_back(std::move(record));
  impl_->record_submitted(desc.lane);
  return handle;
}

void SingleThreadTaskExecutor::cancel(JobHandle handle) noexcept {
  if (!handle.valid() || !impl_) {
    return;
  }
  for (Impl::TaskRecord& record : impl_->tasks) {
    if (record.handle.id != handle.id) {
      continue;
    }
    record.canceled->store(true, std::memory_order_release);
    impl_->record_canceled(record);
    return;
  }
}

void SingleThreadTaskExecutor::pump(TaskPumpBudget budget) {
  if (!impl_ || budget.max_tasks == 0U || budget.max_duration.count() <= 0) {
    return;
  }

  const auto deadline = impl_->now() + budget.max_duration;
  for (u32 pumped = 0; pumped < budget.max_tasks && !impl_->tasks.empty();) {
    if (impl_->now() >= deadline) {
      return;
    }

    Impl::TaskRecord record = std::move(impl_->tasks.front());
    impl_->tasks.pop_front();
    if (record.canceled->load(std::memory_order_acquire)) {
      const usize index = lane_index(record.handle.lane);
      if (impl_->queued[index] > 0U) {
        --impl_->queued[index];
      }
      impl_->record_canceled(record);
      continue;
    }

    impl_->record_started(record.handle.lane);
    bool failed = false;
    try {
      record.work(CancellationToken{record.canceled});
    } catch (const std::exception& ex) {
      failed = true;
      core::error(
          "jobs",
          "single-thread task failed lane={} domain={}: {}",
          job_lane_name(record.handle.lane),
          job_domain_name(record.handle.domain),
          ex.what());
    } catch (...) {
      failed = true;
      core::error(
          "jobs",
          "single-thread task failed lane={} domain={} with an unknown exception",
          job_lane_name(record.handle.lane),
          job_domain_name(record.handle.domain));
    }
    impl_->record_finished(record.handle.lane, failed);
    if (!failed) {
      impl_->enqueue_completion(record.completion_target, std::move(record.completion));
    }
    ++pumped;
  }
}

void SingleThreadTaskExecutor::pump_completions(
    JobCompletionTarget target,
    u32 max_count) {
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

JobStats SingleThreadTaskExecutor::stats() const {
  JobStats stats = impl_->stats;
  stats.queued_critical_cpu = impl_->queued[lane_index(JobLane::CriticalCpu)];
  stats.queued_background_cpu = impl_->queued[lane_index(JobLane::BackgroundCpu)];
  stats.queued_blocking_io = impl_->queued[lane_index(JobLane::BlockingIo)];
  stats.running_critical_cpu = impl_->running[lane_index(JobLane::CriticalCpu)];
  stats.running_background_cpu = impl_->running[lane_index(JobLane::BackgroundCpu)];
  stats.running_blocking_io = impl_->running[lane_index(JobLane::BlockingIo)];
  stats.critical_cpu_workers = 0U;
  stats.background_cpu_workers = 0U;
  stats.blocking_io_workers = 0U;
  return stats;
}

bool SingleThreadTaskExecutor::supports_parallelism() const noexcept {
  return false;
}

void SingleThreadTaskExecutor::request_stop() noexcept {
  if (!impl_) {
    return;
  }
  impl_->accepting = false;
  for (Impl::TaskRecord& record : impl_->tasks) {
    record.canceled->store(true, std::memory_order_release);
    impl_->record_canceled(record);
  }
}

usize SingleThreadTaskExecutor::queued_count() const noexcept {
  return impl_ == nullptr ? 0U : impl_->tasks.size();
}

} // namespace arv::sched
