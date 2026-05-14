#pragma once

#include <atomic>

namespace arv::core {

template <typename T>
void update_atomic_maximum(std::atomic<T>& target, T value) noexcept {
  T current = target.load(std::memory_order_relaxed);
  while (current < value &&
         !target.compare_exchange_weak(
             current,
             value,
             std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

} // namespace arv::core
