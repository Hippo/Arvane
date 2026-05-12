#pragma once

#include "arv/sched/completion_queue.hpp"
#include "arv/core/types.hpp"

#include <cassert>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace arv::sched {

namespace detail {

template<typename Result, bool IsReference = std::is_reference_v<Result>>
struct OwnerThreadCallStorage {
  std::optional<Result> value;

  template<typename Work>
  void invoke(Work& work) {
    value.emplace(std::invoke(work));
  }

  Result take() {
    return std::move(*value);
  }
};

template<typename Result>
struct OwnerThreadCallStorage<Result, true> {
  using Pointee = std::remove_reference_t<Result>;

  Pointee* value{};

  template<typename Work>
  void invoke(Work& work) {
    value = std::addressof(std::invoke(work));
  }

  Result take() {
    return *value;
  }
};

} // namespace detail

class OwnerThread {
public:
  explicit OwnerThread(std::string name);
  virtual ~OwnerThread();

  OwnerThread(const OwnerThread&) = delete;
  OwnerThread& operator=(const OwnerThread&) = delete;

  [[nodiscard]] bool start();
  void request_stop() noexcept;
  void join();

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] bool stop_requested() const noexcept;
  [[nodiscard]] usize owner_thread_hash() const noexcept;
  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;
  void assert_owner_thread() const noexcept;

  template<typename Fn>
  bool post(Fn&& fn) {
    return enqueue(std::function<void()>{std::forward<Fn>(fn)});
  }

  template<typename Fn>
  auto call(Fn&& fn) -> std::invoke_result_t<std::decay_t<Fn>&> {
    using Work = std::decay_t<Fn>;
    using Result = std::invoke_result_t<Work&>;

    Work work{std::forward<Fn>(fn)};
    if (is_owner_thread()) {
      if constexpr (std::is_void_v<Result>) {
        std::invoke(work);
        return;
      } else {
        return std::invoke(work);
      }
    }

    std::atomic_bool complete{false};
    std::exception_ptr failure;
    if constexpr (std::is_void_v<Result>) {
      if (!enqueue([&work, &complete, &failure] {
            try {
              std::invoke(work);
            } catch (...) {
              failure = std::current_exception();
            }
            complete.store(true, std::memory_order_release);
            complete.notify_one();
          })) {
        throw std::runtime_error{"owner thread is not accepting commands"};
      }
      while (!complete.load(std::memory_order_acquire)) {
        complete.wait(false, std::memory_order_acquire);
      }
      if (failure) {
        std::rethrow_exception(failure);
      }
      return;
    } else {
      detail::OwnerThreadCallStorage<Result> storage;
      if (!enqueue([&work, &storage, &complete, &failure] {
            try {
              storage.invoke(work);
            } catch (...) {
              failure = std::current_exception();
            }
            complete.store(true, std::memory_order_release);
            complete.notify_one();
          })) {
        throw std::runtime_error{"owner thread is not accepting commands"};
      }
      while (!complete.load(std::memory_order_acquire)) {
        complete.wait(false, std::memory_order_acquire);
      }
      if (failure) {
        std::rethrow_exception(failure);
      }
      return storage.take();
    }
  }

protected:
  virtual void on_started() {
  }

private:
  bool enqueue(std::function<void()> command);
  void run();
  [[nodiscard]] static usize current_thread_hash() noexcept;
  void notify_owner_thread() noexcept;
  void notify_stop() noexcept;

  std::string name_;
  CompletionQueue<std::function<void()>> commands_;
  std::thread thread_;
  std::atomic<usize> owner_thread_hash_{0U};
  std::atomic<u64> wake_sequence_{0U};
  std::atomic<u32> active_enqueues_{0U};
  std::atomic<u32> outstanding_commands_{0U};
  std::atomic_bool running_{false};
  std::atomic_bool accepting_{false};
  std::atomic_bool stop_requested_{false};
};

class RenderThread final : public OwnerThread {
public:
  RenderThread() : OwnerThread("render") {
  }
};

class NetworkThread final : public OwnerThread {
public:
  NetworkThread() : OwnerThread("network") {
  }
};

class WorldThread final : public OwnerThread {
public:
  WorldThread() : OwnerThread("world") {
  }
};

} // namespace arv::sched
