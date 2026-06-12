#pragma once

#include "arv/sched/job_system.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace arv::sched {

struct TaskPumpBudget {
  std::chrono::microseconds max_duration{2000};
  u32 max_tasks{64};
};

class ITaskExecutor {
public:
  virtual ~ITaskExecutor() = default;

  [[nodiscard]] virtual JobHandle submit(
      JobDesc desc,
      std::function<void(const CancellationToken&)> work) = 0;
  [[nodiscard]] virtual JobHandle submit_with_completion(
      JobDesc desc,
      std::function<void(const CancellationToken&)> work,
      std::function<void()> completion) = 0;
  virtual void cancel(JobHandle handle) noexcept = 0;
  virtual void pump(TaskPumpBudget budget) = 0;
  virtual void pump_completions(JobCompletionTarget target, u32 max_count) = 0;
  [[nodiscard]] virtual JobStats stats() const = 0;
  [[nodiscard]] virtual bool supports_parallelism() const noexcept = 0;
};

class JobSystemTaskExecutor final : public ITaskExecutor {
public:
  explicit JobSystemTaskExecutor(JobSystem& jobs) noexcept;

  [[nodiscard]] JobHandle submit(
      JobDesc desc,
      std::function<void(const CancellationToken&)> work) override;
  [[nodiscard]] JobHandle submit_with_completion(
      JobDesc desc,
      std::function<void(const CancellationToken&)> work,
      std::function<void()> completion) override;
  void cancel(JobHandle handle) noexcept override;
  void pump(TaskPumpBudget budget) override;
  void pump_completions(JobCompletionTarget target, u32 max_count) override;
  [[nodiscard]] JobStats stats() const override;
  [[nodiscard]] bool supports_parallelism() const noexcept override;

private:
  JobSystem* jobs_{};
};

class SingleThreadTaskExecutor final : public ITaskExecutor {
public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  SingleThreadTaskExecutor();
  explicit SingleThreadTaskExecutor(Clock clock);
  ~SingleThreadTaskExecutor() override;

  [[nodiscard]] JobHandle submit(
      JobDesc desc,
      std::function<void(const CancellationToken&)> work) override;
  [[nodiscard]] JobHandle submit_with_completion(
      JobDesc desc,
      std::function<void(const CancellationToken&)> work,
      std::function<void()> completion) override;
  void cancel(JobHandle handle) noexcept override;
  void pump(TaskPumpBudget budget) override;
  void pump_completions(JobCompletionTarget target, u32 max_count) override;
  [[nodiscard]] JobStats stats() const override;
  [[nodiscard]] bool supports_parallelism() const noexcept override;
  void request_stop() noexcept;
  [[nodiscard]] usize queued_count() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

template<typename Fn>
auto submit_task_result(ITaskExecutor& executor, JobDesc desc, Fn&& fn) {
  using Work = std::decay_t<Fn>;
  using Result = detail::JobResultOf<Work>;

  auto promise = std::make_shared<std::promise<Result>>();
  std::future<Result> future = promise->get_future();
  Work work{std::forward<Fn>(fn)};
  JobHandle handle = executor.submit(
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
      throw std::runtime_error{"task was not accepted"};
    } catch (...) {
      promise->set_exception(std::current_exception());
    }
  }

  return JobFuture<Result>{handle, std::move(future)};
}

} // namespace arv::sched
