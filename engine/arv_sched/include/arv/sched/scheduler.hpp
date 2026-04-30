#pragma once

#include "arv/core/arena.hpp"
#include "arv/core/id.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/core/types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <taskflow/taskflow.hpp>

namespace arv::sched {

struct JobTag {};
using JobID = ID<JobTag, u64>;

enum class JobKind {
  Background,
  MainThreadHandoff,
  SimulationHandoff,
  RenderHandoff,
};

struct WorkerThreadConfig {
  // 0 means automatic: hardware_concurrency minus reserved_thread_count.
  usize worker_thread_count{};
  usize reserved_thread_count{2};
  usize min_worker_thread_count{1};
  usize max_worker_thread_count{64};
};

struct RuntimeThreadBudgetConfig {
  WorkerThreadConfig workers{};
  usize main_thread_count{1};
  usize simulation_thread_count{1};
  usize render_thread_count{1};
  usize network_thread_count{1};
  bool split_primary_secondary_pools{};
};

struct RuntimeThreadBudgetPlan {
  usize hardware_thread_count{};
  usize reserved_thread_count{};
  usize total_worker_thread_count{};
  usize primary_worker_thread_count{};
  usize secondary_worker_thread_count{};
  WorkerThreadConfig primary_workers{};
  WorkerThreadConfig secondary_workers{};
};

[[nodiscard]] usize hardware_thread_count_or_one() noexcept;

[[nodiscard]] usize resolve_worker_thread_count(WorkerThreadConfig config) noexcept;

[[nodiscard]] RuntimeThreadBudgetPlan plan_runtime_thread_budget(
    RuntimeThreadBudgetConfig config) noexcept;

template<typename T>
class JobHandle {
public:
  JobHandle(JobID id, std::future<T>&& future) : id_(id), future_(std::move(future)) {
  }

  [[nodiscard]] JobID id() const noexcept {
    return id_;
  }

  [[nodiscard]] bool is_complete() const {
    return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  }

  T get() {
    return future_.get();
  }

private:
  JobID id_{};
  std::future<T> future_;
};

class Scheduler {
public:
  explicit Scheduler(usize worker_count = default_worker_count());

  explicit Scheduler(WorkerThreadConfig config);

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  ~Scheduler();

  [[nodiscard]] usize worker_count() const noexcept;

  template<typename Fn>
  auto submit(std::string name, Fn&& fn) {
    using Result = std::invoke_result_t<std::decay_t<Fn>>;
    const JobID id{next_id_.fetch_add(1, std::memory_order_relaxed)};
    std::chrono::steady_clock::time_point submitted_at{};
    if constexpr (core::perf_stats_enabled()) {
      submitted_at = std::chrono::steady_clock::now();
    }
    std::string job_name = std::move(name);
    auto wrapped = [submitted_at,
                    job_name,
                    fn = std::forward<Fn>(fn)]() mutable -> Result {
      static thread_local bool named_thread = false;
      if (!named_thread) {
        core::set_profile_thread_name("worker");
        named_thread = true;
      }

      ARV_PROFILE_ZONE_DYNAMIC(job_name.data(), job_name.size());
      worker_thread_arena().reset();
      if constexpr (core::perf_stats_enabled()) {
        const auto started_at = std::chrono::steady_clock::now();
        core::record_thread_timing(
            core::PerfMetric::JobQueueLatency,
            std::chrono::duration<double, std::milli>(started_at - submitted_at).count());
      }
      if constexpr (std::is_void_v<Result>) {
        std::invoke(fn);
      } else {
        return std::invoke(fn);
      }
    };

    std::future<Result> future;
    if (job_name.empty()) {
      future = executor_.async(std::move(wrapped));
    } else {
      future = executor_.async(tf::TaskParams{std::move(job_name)}, std::move(wrapped));
    }

    return JobHandle<Result>{id, std::move(future)};
  }

  void wait_for_all();

  [[nodiscard]] static usize default_worker_count() noexcept;

private:
  tf::Executor executor_;
  std::atomic<u64> next_id_{1};
};

} // namespace arv::sched
