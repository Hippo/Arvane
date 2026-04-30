#pragma once

#include "arv/core/types.hpp"

#include <concurrentqueue.h>
#include <readerwriterqueue.h>

#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace arv::sched {

template<typename T>
class SpscQueue {
public:
  explicit SpscQueue(usize capacity = 1024U) : values_(capacity) {
  }

  [[nodiscard]] bool try_push(T value) {
    return values_.try_enqueue(std::move(value));
  }

  [[nodiscard]] bool try_pop(T& value) {
    return values_.try_dequeue(value);
  }

  [[nodiscard]] usize size_approx() const noexcept {
    return values_.size_approx();
  }

private:
  moodycamel::ReaderWriterQueue<T> values_;
};

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
    drain_into(drained);
    return drained;
  }

  void drain_into(std::vector<T>& drained) {
    drained.reserve(drained.size() + values_.size_approx());

    std::optional<T> value;
    while (values_.try_dequeue(value)) {
      if (value.has_value()) {
        drained.emplace_back(std::move(value).value());
      }
      value.reset();
    }
  }

  [[nodiscard]] bool empty() const {
    return values_.size_approx() == 0U;
  }

private:
  moodycamel::ConcurrentQueue<std::optional<T>> values_;
};

} // namespace arv::sched
