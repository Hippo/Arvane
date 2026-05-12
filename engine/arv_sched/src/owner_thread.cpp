#include "arv/sched/owner_thread.hpp"

#include "arv/core/profiling.hpp"

#include <functional>

namespace arv::sched {

OwnerThread::OwnerThread(std::string name) : name_(std::move(name)) {
}

OwnerThread::~OwnerThread() {
  request_stop();
  join();
}

bool OwnerThread::start() {
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }
  if (thread_.joinable()) {
    thread_.join();
  }

  std::function<void()> stale_command;
  while (commands_.try_pop(stale_command)) {
    stale_command = {};
  }

  owner_thread_hash_.store(0U, std::memory_order_release);
  wake_sequence_.store(0U, std::memory_order_release);
  active_enqueues_.store(0U, std::memory_order_release);
  outstanding_commands_.store(0U, std::memory_order_release);
  running_.store(false, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  accepting_.store(true, std::memory_order_release);
  thread_ = std::thread([this] {
    run();
  });
  while (!running_.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  return true;
}

void OwnerThread::request_stop() noexcept {
  accepting_.store(false, std::memory_order_release);
  stop_requested_.store(true, std::memory_order_release);
  while (active_enqueues_.load(std::memory_order_acquire) != 0U) {
    std::this_thread::yield();
  }
  notify_stop();
}

void OwnerThread::join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool OwnerThread::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

bool OwnerThread::stop_requested() const noexcept {
  return stop_requested_.load(std::memory_order_acquire);
}

usize OwnerThread::owner_thread_hash() const noexcept {
  return owner_thread_hash_.load(std::memory_order_acquire);
}

const std::string& OwnerThread::name() const noexcept {
  return name_;
}

bool OwnerThread::is_owner_thread() const noexcept {
  return running_.load(std::memory_order_acquire) &&
         owner_thread_hash_.load(std::memory_order_acquire) == current_thread_hash();
}

void OwnerThread::assert_owner_thread() const noexcept {
  assert(is_owner_thread() && "owner-thread command ran on the wrong thread");
}

bool OwnerThread::enqueue(std::function<void()> command) {
  active_enqueues_.fetch_add(1U, std::memory_order_acq_rel);
  if (!accepting_.load(std::memory_order_acquire)) {
    active_enqueues_.fetch_sub(1U, std::memory_order_acq_rel);
    return false;
  }

  outstanding_commands_.fetch_add(1U, std::memory_order_acq_rel);
  if (!commands_.try_push(std::move(command))) {
    outstanding_commands_.fetch_sub(1U, std::memory_order_acq_rel);
    active_enqueues_.fetch_sub(1U, std::memory_order_acq_rel);
    return false;
  }
  active_enqueues_.fetch_sub(1U, std::memory_order_acq_rel);
  notify_owner_thread();
  return true;
}

void OwnerThread::run() {
  core::set_profile_thread_name(name_.c_str());
  owner_thread_hash_.store(current_thread_hash(), std::memory_order_release);
  running_.store(true, std::memory_order_release);
  on_started();

  u32 idle_spins = 0U;
  for (;;) {
    const u64 observed_wake = wake_sequence_.load(std::memory_order_acquire);
    std::function<void()> command;
    if (!commands_.try_pop(command)) {
      if (stop_requested_.load(std::memory_order_acquire) &&
          outstanding_commands_.load(std::memory_order_acquire) == 0U &&
          active_enqueues_.load(std::memory_order_acquire) == 0U) {
        break;
      }
      const bool enqueue_or_command_in_flight =
          active_enqueues_.load(std::memory_order_acquire) != 0U ||
          outstanding_commands_.load(std::memory_order_acquire) != 0U;
      if (idle_spins < 64U || enqueue_or_command_in_flight) {
        ++idle_spins;
        std::this_thread::yield();
      } else {
        wake_sequence_.wait(observed_wake, std::memory_order_acquire);
      }
      continue;
    }
    idle_spins = 0U;
    if (!command) {
      if (stop_requested_.load(std::memory_order_acquire) &&
          outstanding_commands_.load(std::memory_order_acquire) == 0U &&
          active_enqueues_.load(std::memory_order_acquire) == 0U) {
        break;
      }
      continue;
    }
    command();

    const u32 previous_outstanding =
        outstanding_commands_.fetch_sub(1U, std::memory_order_acq_rel);
    if (previous_outstanding == 1U &&
        stop_requested_.load(std::memory_order_acquire) &&
        active_enqueues_.load(std::memory_order_acquire) == 0U) {
      break;
    }
  }

  running_.store(false, std::memory_order_release);
  owner_thread_hash_.store(0U, std::memory_order_release);
  accepting_.store(false, std::memory_order_release);
}

usize OwnerThread::current_thread_hash() noexcept {
  const usize hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
  return hash == 0U ? 1U : hash;
}

void OwnerThread::notify_owner_thread() noexcept {
  wake_sequence_.fetch_add(1U, std::memory_order_release);
  wake_sequence_.notify_one();
}

void OwnerThread::notify_stop() noexcept {
  (void)commands_.try_push(std::function<void()>{});
  notify_owner_thread();
}

} // namespace arv::sched
