#include "arv/core/perf_stats.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace arv::core {
namespace {

[[nodiscard]] u32 clamp_to_microseconds(double milliseconds) noexcept {
  if (!std::isfinite(milliseconds) || milliseconds <= 0.0) {
    return 0;
  }

  const double microseconds = milliseconds * 1000.0;
  constexpr double MaxSample = static_cast<double>(~u32{0});
  return microseconds >= MaxSample ? ~u32{0} : static_cast<u32>(microseconds);
}

[[nodiscard]] u32 percentile_sample(std::array<u32, TimingHistogram::Capacity> samples, u32 count, u32 percentile) noexcept {
  if (count == 0U) {
    return 0;
  }

  const usize active_count = static_cast<usize>(count);
  std::sort(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(active_count));
  const u32 rank = static_cast<u32>(
      (static_cast<u64>(count - 1U) * static_cast<u64>(percentile) + 99ULL) / 100ULL);
  return samples[rank];
}

void update_maximum(std::atomic<u32>& target, u32 value) noexcept {
  u32 current = target.load(std::memory_order_relaxed);
  while (current < value &&
         !target.compare_exchange_weak(
             current,
             value,
             std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

class AtomicTimingHistogram {
public:
  static constexpr usize Capacity = 2048;

  void record_ms(double milliseconds) noexcept {
    const u32 sample_us = clamp_to_microseconds(milliseconds);
    const u32 write_index = write_index_.fetch_add(1U, std::memory_order_relaxed);
    samples_us_[write_index % static_cast<u32>(Capacity)].store(
        sample_us,
        std::memory_order_relaxed);

    u32 sample_count = sample_count_.load(std::memory_order_relaxed);
    while (sample_count < Capacity &&
           !sample_count_.compare_exchange_weak(
               sample_count,
               sample_count + 1U,
               std::memory_order_release,
               std::memory_order_relaxed)) {
    }

    latest_us_.store(sample_us, std::memory_order_relaxed);
    update_maximum(max_us_, sample_us);
  }

  [[nodiscard]] TimingMetricSnapshot snapshot() const noexcept {
    TimingMetricSnapshot result;
    const u32 sample_count = std::min<u32>(
        sample_count_.load(std::memory_order_acquire),
        static_cast<u32>(Capacity));
    result.percentiles.sample_count = sample_count;
    result.latest_ms =
        static_cast<double>(latest_us_.load(std::memory_order_relaxed)) / 1000.0;
    result.max_ms = static_cast<double>(max_us_.load(std::memory_order_relaxed)) / 1000.0;
    if (sample_count == 0U) {
      return result;
    }

    std::array<u32, Capacity> samples{};
    for (u32 index = 0; index < sample_count; ++index) {
      samples[index] = samples_us_[index].load(std::memory_order_relaxed);
    }

    std::sort(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(sample_count));
    auto sample_for_percentile = [&](u32 percentile) {
      const u32 rank = static_cast<u32>(
          (static_cast<u64>(sample_count - 1U) * static_cast<u64>(percentile) + 99ULL) /
          100ULL);
      return static_cast<double>(samples[rank]) / 1000.0;
    };

    result.percentiles.p50_ms = sample_for_percentile(50);
    result.percentiles.p95_ms = sample_for_percentile(95);
    result.percentiles.p99_ms = sample_for_percentile(99);
    return result;
  }

  void clear() noexcept {
    for (std::atomic<u32>& sample : samples_us_) {
      sample.store(0U, std::memory_order_relaxed);
    }
    write_index_.store(0U, std::memory_order_relaxed);
    sample_count_.store(0U, std::memory_order_release);
    latest_us_.store(0U, std::memory_order_relaxed);
    max_us_.store(0U, std::memory_order_relaxed);
  }

private:
  std::array<std::atomic<u32>, Capacity> samples_us_{};
  std::atomic<u32> write_index_{};
  std::atomic<u32> sample_count_{};
  std::atomic<u32> latest_us_{};
  std::atomic<u32> max_us_{};
};

using GlobalTimingHistograms =
    std::array<AtomicTimingHistogram, static_cast<usize>(PerfMetric::Count)>;

[[nodiscard]] GlobalTimingHistograms& global_timing_histograms() noexcept {
  static GlobalTimingHistograms histograms;
  return histograms;
}

[[nodiscard]] bool valid_metric(PerfMetric metric) noexcept {
  return static_cast<usize>(metric) < static_cast<usize>(PerfMetric::Count);
}

} // namespace

void TimingHistogram::record_ms(double milliseconds) noexcept {
  samples_us_[write_index_] = clamp_to_microseconds(milliseconds);
  write_index_ = (write_index_ + 1U) % static_cast<u32>(Capacity);
  if (sample_count_ < Capacity) {
    ++sample_count_;
  }
}

TimingPercentiles TimingHistogram::percentiles() const noexcept {
  TimingPercentiles result;
  result.sample_count = sample_count_;
  if (sample_count_ == 0U) {
    return result;
  }

  std::array<u32, Capacity> samples{};
  for (u32 index = 0; index < sample_count_; ++index) {
    samples[index] = samples_us_[index];
  }

  result.p50_ms = static_cast<double>(percentile_sample(samples, sample_count_, 50)) / 1000.0;
  result.p95_ms = static_cast<double>(percentile_sample(samples, sample_count_, 95)) / 1000.0;
  result.p99_ms = static_cast<double>(percentile_sample(samples, sample_count_, 99)) / 1000.0;
  return result;
}

void TimingHistogram::clear() noexcept {
  samples_us_.fill(0);
  write_index_ = 0;
  sample_count_ = 0;
}

ThreadTimingHistograms& thread_timing_histograms() noexcept {
  thread_local ThreadTimingHistograms histograms;
  return histograms;
}

void record_thread_timing(PerfMetric metric, double milliseconds) noexcept {
#if !ARVANE_ENABLE_PROFILING
  (void)metric;
  (void)milliseconds;
  return;
#else
  if (!valid_metric(metric)) {
    return;
  }
  thread_timing_histograms().metrics[static_cast<usize>(metric)].record_ms(milliseconds);
  global_timing_histograms()[static_cast<usize>(metric)].record_ms(milliseconds);
#endif
}

TimingPercentiles thread_timing_percentiles(PerfMetric metric) noexcept {
#if !ARVANE_ENABLE_PROFILING
  (void)metric;
  return {};
#else
  if (!valid_metric(metric)) {
    return {};
  }
  return thread_timing_histograms().metrics[static_cast<usize>(metric)].percentiles();
#endif
}

const char* perf_metric_name(PerfMetric metric) noexcept {
  switch (metric) {
    case PerfMetric::MeshingTask:
      return "Meshing tasks";
    case PerfMetric::LightingTask:
      return "Lighting tasks";
    case PerfMetric::JobQueueLatency:
      return "Job queue latency";
    case PerfMetric::Count:
      break;
  }
  return "Unknown";
}

TimingMetricSnapshot global_timing_snapshot(PerfMetric metric) noexcept {
#if !ARVANE_ENABLE_PROFILING
  (void)metric;
  return {};
#else
  if (!valid_metric(metric)) {
    return {};
  }
  return global_timing_histograms()[static_cast<usize>(metric)].snapshot();
#endif
}

void reset_global_timing_stats() noexcept {
#if ARVANE_ENABLE_PROFILING
  for (AtomicTimingHistogram& histogram : global_timing_histograms()) {
    histogram.clear();
  }
#endif
}

ScopedThreadTiming::ScopedThreadTiming(PerfMetric metric) noexcept
    : metric_(metric) {
#if ARVANE_ENABLE_PROFILING
  start_ = std::chrono::steady_clock::now();
#endif
}

ScopedThreadTiming::~ScopedThreadTiming() {
#if ARVANE_ENABLE_PROFILING
  const auto end = std::chrono::steady_clock::now();
  record_thread_timing(
      metric_,
      std::chrono::duration<double, std::milli>(end - start_).count());
#endif
}

} // namespace arv::core
