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

template<typename T>
class JobHandle {
public:
  JobHandle(JobID id, std::future<T>&& future) : id_(id), future_(std::move(future)) {}

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
  explicit Scheduler(usize worker_count = default_worker_count())
      : executor_(std::max<usize>(worker_count, 1)) {}

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  ~Scheduler() {
    wait_for_all();
  }

  [[nodiscard]] usize worker_count() const noexcept {
    return executor_.num_workers();
  }

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

  void wait_for_all() {
    executor_.wait_for_all();
  }

  [[nodiscard]] static usize default_worker_count() noexcept {
    const unsigned hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads <= 1) {
      return 1;
    }
    return static_cast<usize>(hardware_threads - 1);
  }

private:
  tf::Executor executor_;
  std::atomic<u64> next_id_{1};
};

} // namespace arv::sched
