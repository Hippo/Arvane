#pragma once

#include "arv/core/types.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace arv::sched {

enum class JobLane : u8 {
  CriticalCpu,
  BackgroundCpu,
  BlockingIo,
};

enum class JobPriority : u8 {
  High,
  Normal,
  Low,
};

enum class JobDomain : u8 {
  Generic,
  ChunkGeneration,
  Meshing,
  Lighting,
  Liquids,
  EntityAi,
  Pathfinding,
  SaveLoad,
  Assets,
  Networking,
  Rendering,
};

enum class JobCompletionTarget : u8 {
  None,
  Main,
  World,
  Render,
  Network,
};

[[nodiscard]] const char* job_lane_name(JobLane lane) noexcept;
[[nodiscard]] const char* job_domain_name(JobDomain domain) noexcept;
[[nodiscard]] const char* job_completion_target_name(JobCompletionTarget target) noexcept;

struct JobDesc {
  std::string name{};
  JobLane lane{JobLane::BackgroundCpu};
  JobPriority priority{JobPriority::Normal};
  JobDomain domain{JobDomain::Generic};
  JobCompletionTarget completion_target{JobCompletionTarget::None};
};

class CancellationToken {
public:
  CancellationToken() = default;

  [[nodiscard]] bool is_canceled() const noexcept;

private:
  friend class CancellationSource;
  friend class JobSystem;
  friend class SingleThreadTaskExecutor;

  explicit CancellationToken(std::shared_ptr<std::atomic_bool> canceled) noexcept;

  std::shared_ptr<std::atomic_bool> canceled_{};
};

class CancellationSource {
public:
  CancellationSource();

  [[nodiscard]] CancellationToken token() const noexcept;
  void cancel() noexcept;

private:
  std::shared_ptr<std::atomic_bool> canceled_;
};

struct JobHandle {
  u64 id{};
  JobLane lane{JobLane::BackgroundCpu};
  JobDomain domain{JobDomain::Generic};

  [[nodiscard]] bool valid() const noexcept {
    return id != 0U;
  }
};

template<typename T>
class JobFuture {
public:
  JobFuture() = default;

  JobFuture(JobHandle handle, std::future<T>&& future)
      : handle_(handle),
        future_(std::move(future)) {
  }

  [[nodiscard]] JobHandle handle() const noexcept {
    return handle_;
  }

  [[nodiscard]] u64 id() const noexcept {
    return handle_.id;
  }

  [[nodiscard]] bool valid() const noexcept {
    return handle_.valid();
  }

  [[nodiscard]] bool is_complete() const {
    return !future_.valid() ||
           future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  }

  T get() {
    return future_.get();
  }

private:
  JobHandle handle_{};
  mutable std::future<T> future_{};
};

struct JobStats {
  u64 submitted{};
  u64 started{};
  u64 completed{};
  u64 canceled{};
  u64 failed{};
  u32 queued_critical_cpu{};
  u32 queued_background_cpu{};
  u32 queued_blocking_io{};
  u32 running_critical_cpu{};
  u32 running_background_cpu{};
  u32 running_blocking_io{};
  u32 critical_cpu_workers{};
  u32 background_cpu_workers{};
  u32 blocking_io_workers{};
};

struct JobSystemPressureSnapshot {
  usize worker_count{};
  u32 current_queued_jobs{};
  u32 peak_queued_jobs{};
  u32 active_workers{};
  u32 idle_workers{};
  double idle_percent{};
  u64 submitted_jobs_total{};
  u64 completed_jobs_total{};
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

struct JobSystemConfig {
  u32 critical_cpu_workers{};
  u32 background_cpu_workers{};
  u32 blocking_io_workers{1};
  u32 max_chunk_gen_in_flight{};
  u32 max_mesh_jobs_in_flight{};
  u32 max_lighting_jobs_in_flight{};
  u32 max_liquid_jobs_in_flight{};
  u32 max_ai_jobs_in_flight{};
  u32 max_pathfinding_jobs_in_flight{};
};

[[nodiscard]] usize hardware_thread_count_or_one() noexcept;

[[nodiscard]] usize resolve_worker_thread_count(WorkerThreadConfig config) noexcept;

[[nodiscard]] RuntimeThreadBudgetPlan plan_runtime_thread_budget(
    RuntimeThreadBudgetConfig config) noexcept;

class JobSystem {
public:
  explicit JobSystem(JobSystemConfig config = {});
  ~JobSystem();

  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  JobSystem(JobSystem&&) noexcept;
  JobSystem& operator=(JobSystem&&) noexcept;

  void start();
  void request_stop();
  void join();

  [[nodiscard]] JobHandle submit(
      JobDesc desc,
      std::function<void(const CancellationToken&)> work);
  [[nodiscard]] JobHandle submit_with_completion(
      JobDesc desc,
      std::function<void(const CancellationToken&)> work,
      std::function<void()> completion);

  template<typename Fn>
  [[nodiscard]] auto submit_result(JobDesc desc, Fn&& fn);

  void cancel(JobHandle handle) noexcept;
  void pump_completions(JobCompletionTarget target, u32 max_count);
  [[nodiscard]] JobStats stats() const;
  [[nodiscard]] JobSystemPressureSnapshot pressure_snapshot() const noexcept;
  void reset_pressure_stats() noexcept;
  [[nodiscard]] usize worker_count(JobLane lane) const noexcept;
  [[nodiscard]] usize total_worker_count() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

namespace detail {

template<typename Fn, bool InvocableWithToken>
struct JobResultOfImpl;

template<typename Fn>
struct JobResultOfImpl<Fn, true> {
  using type = std::invoke_result_t<Fn&, const CancellationToken&>;
};

template<typename Fn>
struct JobResultOfImpl<Fn, false> {
  using type = std::invoke_result_t<Fn&>;
};

template<typename Fn>
using JobResultOf = typename JobResultOfImpl<
    std::decay_t<Fn>,
    std::is_invocable_v<std::decay_t<Fn>&, const CancellationToken&>>::type;

} // namespace detail

template<typename Fn>
auto JobSystem::submit_result(JobDesc desc, Fn&& fn) {
  using Work = std::decay_t<Fn>;
  using Result = detail::JobResultOf<Work>;

  auto promise = std::make_shared<std::promise<Result>>();
  std::future<Result> future = promise->get_future();
  Work work{std::forward<Fn>(fn)};
  JobHandle handle = submit(
      std::move(desc),
      [promise, work = std::move(work)](const CancellationToken& token) mutable {
        try {
          if constexpr (std::is_invocable_v<Work&, const CancellationToken&>) {
            if constexpr (std::is_void_v<Result>) {
              std::invoke(work, token);
              promise->set_value();
            } else {
              promise->set_value(std::invoke(work, token));
            }
          } else {
            if constexpr (std::is_void_v<Result>) {
              std::invoke(work);
              promise->set_value();
            } else {
              promise->set_value(std::invoke(work));
            }
          }
        } catch (...) {
          promise->set_exception(std::current_exception());
          throw;
        }
      });

  if (!handle.valid()) {
    try {
      throw std::runtime_error{"job was not accepted"};
    } catch (...) {
      promise->set_exception(std::current_exception());
    }
  }

  return JobFuture<Result>{handle, std::move(future)};
}

} // namespace arv::sched
