#pragma once

#include <mutex>
#include <utility>
#include <vector>

namespace arv::sched {

template<typename T>
class CompletionQueue {
public:
  void push(T value) {
    std::lock_guard lock(mutex_);
    values_.push_back(std::move(value));
  }

  [[nodiscard]] std::vector<T> drain() {
    std::lock_guard lock(mutex_);
    std::vector<T> drained;
    drained.swap(values_);
    return drained;
  }

  [[nodiscard]] bool empty() const {
    std::lock_guard lock(mutex_);
    return values_.empty();
  }

private:
  mutable std::mutex mutex_;
  std::vector<T> values_;
};

} // namespace arv::sched
