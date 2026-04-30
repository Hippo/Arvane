#pragma once

#include "arv/core/types.hpp"

#include <array>
#include <chrono>

#ifndef ARVANE_ENABLE_PROFILING
#define ARVANE_ENABLE_PROFILING 0
#endif

namespace arv::core {

[[nodiscard]] constexpr bool perf_stats_enabled() noexcept {
  return ARVANE_ENABLE_PROFILING != 0;
}

enum class PerfMetric : u8 {
  MeshingTask,
  LightingTask,
  JobQueueLatency,
  Count,
};

struct TimingPercentiles {
  u32 sample_count{};
  double p50_ms{};
  double p95_ms{};
  double p99_ms{};
};

struct TimingMetricSnapshot {
  TimingPercentiles percentiles{};
  double latest_ms{};
  double max_ms{};
};

class TimingHistogram {
public:
  static constexpr usize Capacity = 512;

  void record_ms(double milliseconds) noexcept;

  [[nodiscard]] TimingPercentiles percentiles() const noexcept;

  void clear() noexcept;

private:
  std::array<u32, Capacity> samples_us_{};
  u32 write_index_{};
  u32 sample_count_{};
};

struct ThreadTimingHistograms {
  std::array<TimingHistogram, static_cast<usize>(PerfMetric::Count)> metrics{};
};

[[nodiscard]] ThreadTimingHistograms& thread_timing_histograms() noexcept;

void record_thread_timing(PerfMetric metric, double milliseconds) noexcept;

[[nodiscard]] TimingPercentiles thread_timing_percentiles(PerfMetric metric) noexcept;

[[nodiscard]] const char* perf_metric_name(PerfMetric metric) noexcept;

[[nodiscard]] TimingMetricSnapshot global_timing_snapshot(PerfMetric metric) noexcept;

void reset_global_timing_stats() noexcept;

class ScopedThreadTiming {
public:
  explicit ScopedThreadTiming(PerfMetric metric) noexcept;

  ScopedThreadTiming(const ScopedThreadTiming&) = delete;
  ScopedThreadTiming& operator=(const ScopedThreadTiming&) = delete;

  ~ScopedThreadTiming();

private:
  PerfMetric metric_{};
  std::chrono::steady_clock::time_point start_{};
};

} // namespace arv::core
