#pragma once

#include <algorithm>

namespace arv::core {

class TokenBucket {
public:
  constexpr TokenBucket() noexcept = default;

  constexpr TokenBucket(double capacity, double refill_per_second) noexcept
      : capacity_(std::max(0.0, capacity)),
        refill_per_second_(std::max(0.0, refill_per_second)),
        tokens_(capacity_) {
  }

  [[nodiscard]] bool try_consume(double amount, double now_seconds) noexcept {
    if (amount <= 0.0) {
      refill(now_seconds);
      return true;
    }
    refill(now_seconds);
    if (tokens_ < amount) {
      return false;
    }
    tokens_ -= amount;
    return true;
  }

  [[nodiscard]] double available(double now_seconds) const noexcept {
    if (now_seconds <= last_refill_seconds_) {
      return tokens_;
    }
    const double elapsed = now_seconds - last_refill_seconds_;
    return std::min(capacity_, tokens_ + elapsed * refill_per_second_);
  }

private:
  void refill(double now_seconds) noexcept {
    tokens_ = available(now_seconds);
    last_refill_seconds_ = std::max(last_refill_seconds_, now_seconds);
  }

  double capacity_{};
  double refill_per_second_{};
  double tokens_{};
  double last_refill_seconds_{};
};

} // namespace arv::core
