#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"

#include <array>
#include <chrono>
#include <span>

#ifndef ARVANE_ENABLE_PROFILING
#define ARVANE_ENABLE_PROFILING 0
#endif

namespace arv::core {

[[nodiscard]] constexpr bool perf_stats_enabled() noexcept {
  return ARVANE_ENABLE_PROFILING != 0;
}

enum class PerfMetric : u8 {
  FrameDelta,
  CpuFrameWork,
  SimulationTick,
  RenderSubmit,
  GpuWaitTime,
  GpuQueueTiming,
  SnapshotCreation,
  ChunkLookupAccess,
  MeshingTask,
  LightingTask,
  GenerationTask,
  FluidTask,
  JobQueueLatency,
  Count,
};

enum class PerfEventGroup : u8 {
  All,
  Frame,
  Worker,
  Zone,
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

inline constexpr usize PerfEventThreadCapacity = 32;
inline constexpr usize PerfEventContextCapacity = 96;
inline constexpr usize WorstEventRingCapacity = 64;

void format_chunk_section_perf_context(
    std::array<char, PerfEventContextCapacity>& context,
    ChunkSectionCoord coord) noexcept;

struct PerfEventSample {
  PerfMetric metric{};
  u32 duration_us{};
  u64 frame_index{};
  u64 timestamp_us{};
  std::array<char, PerfEventThreadCapacity> thread{};
  std::array<char, PerfEventContextCapacity> context{};

  [[nodiscard]] double duration_ms() const noexcept {
    return static_cast<double>(duration_us) / 1000.0;
  }
};

struct SnapshotCostSnapshot {
  u64 snapshots_created_total{};
  u64 snapshots_reused_total{};
  u64 snapshot_copies_total{};
  u64 snapshot_bytes_copied_total{};
};

struct LightingStatsUpdate {
  u32 nodes_processed{};
  u32 add_nodes_processed{};
  u32 remove_nodes_processed{};
  u32 boundary_deferrals{};
  u32 sections_touched{};
  u32 sections_published{};
  u32 max_queue_depth{};
  u32 remeshes_triggered{};
  bool continued{};
};

struct LightingStatsSnapshot {
  u64 jobs_submitted_total{};
  u64 jobs_completed_total{};
  u64 jobs_continued_total{};
  u64 nodes_processed_total{};
  u64 add_nodes_processed_total{};
  u64 remove_nodes_processed_total{};
  u64 boundary_deferrals_total{};
  u64 sections_touched_total{};
  u64 sections_published_total{};
  u64 remeshes_triggered_total{};
  u32 max_queue_depth{};
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
void record_thread_timing(
    PerfMetric metric,
    double milliseconds,
    const char* thread_category,
    const char* context = nullptr) noexcept;

[[nodiscard]] TimingPercentiles thread_timing_percentiles(PerfMetric metric) noexcept;

[[nodiscard]] const char* perf_metric_name(PerfMetric metric) noexcept;
[[nodiscard]] const char* perf_metric_thread_category(PerfMetric metric) noexcept;
[[nodiscard]] PerfEventGroup perf_metric_group(PerfMetric metric) noexcept;

[[nodiscard]] TimingMetricSnapshot global_timing_snapshot(PerfMetric metric) noexcept;

[[nodiscard]] usize global_worst_event_samples(
    std::span<PerfEventSample> out,
    PerfEventGroup group = PerfEventGroup::All) noexcept;

void set_global_timing_paused(bool paused) noexcept;
[[nodiscard]] bool global_timing_paused() noexcept;

void set_current_profile_frame_index(u64 frame_index) noexcept;
[[nodiscard]] u64 current_profile_frame_index() noexcept;

void record_snapshot_created() noexcept;
void record_snapshot_reused() noexcept;
void record_snapshot_copy(usize bytes) noexcept;
[[nodiscard]] SnapshotCostSnapshot global_snapshot_cost_snapshot() noexcept;

void record_lighting_job_submitted() noexcept;
void record_lighting_job_completed(const LightingStatsUpdate& update) noexcept;
void record_lighting_sections_published(u32 count) noexcept;
void record_lighting_remeshes_triggered(u32 count) noexcept;
void record_lighting_boundary_deferrals(u32 count) noexcept;
[[nodiscard]] LightingStatsSnapshot global_lighting_stats_snapshot() noexcept;

void reset_global_timing_stats() noexcept;

class ScopedThreadTiming {
public:
  explicit ScopedThreadTiming(PerfMetric metric) noexcept;
  ScopedThreadTiming(
      PerfMetric metric,
      const char* thread_category,
      const char* context = nullptr) noexcept;

  ScopedThreadTiming(const ScopedThreadTiming&) = delete;
  ScopedThreadTiming& operator=(const ScopedThreadTiming&) = delete;

  ~ScopedThreadTiming();

private:
  PerfMetric metric_{};
  const char* thread_category_{};
  const char* context_{};
  std::chrono::steady_clock::time_point start_{};
};

} // namespace arv::core
