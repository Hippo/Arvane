#pragma once

#include <concurrentqueue.h>

#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace arv::sched {

template<typename T>
class CompletionQueue {
public:
  void push(T value) {
    if (!values_.enqueue(std::optional<T>{std::move(value)})) {
      throw std::bad_alloc{};
    }
  }

  [[nodiscard]] std::vector<T> drain() {
    std::vector<T> drained;
    drained.reserve(values_.size_approx());

    std::optional<T> value;
    while (values_.try_dequeue(value)) {
      if (value.has_value()) {
        drained.push_back(std::move(value).value());
      }
      value.reset();
    }
    return drained;
  }

  [[nodiscard]] bool empty() const {
    return values_.size_approx() == 0U;
  }

private:
  moodycamel::ConcurrentQueue<std::optional<T>> values_;
};

} // namespace arv::sched
