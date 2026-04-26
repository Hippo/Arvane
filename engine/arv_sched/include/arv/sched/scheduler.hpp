#pragma once

#include "arv/core/id.hpp"
#include "arv/core/types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
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
  usize reserved_thread_count{4};
  usize min_worker_thread_count{1};
  usize max_worker_thread_count{64};
};

[[nodiscard]] usize hardware_thread_count_or_one() noexcept;

[[nodiscard]] usize resolve_worker_thread_count(WorkerThreadConfig config) noexcept;

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

    std::future<Result> future;
    if (name.empty()) {
      future = executor_.async(std::forward<Fn>(fn));
    } else {
      future = executor_.async(tf::TaskParams{std::move(name)}, std::forward<Fn>(fn));
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
