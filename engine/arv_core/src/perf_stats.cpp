#include "arv/core/perf_stats.hpp"

#include "arv/core/atomic.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>

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

template<usize Capacity>
void copy_c_string(std::array<char, Capacity>& target, const char* source) noexcept {
  target.fill('\0');
  if (source == nullptr || Capacity == 0U) {
    return;
  }

  const usize length = std::min<usize>(std::strlen(source), Capacity - 1U);
  if (length > 0U) {
    std::memcpy(target.data(), source, length);
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
    update_atomic_maximum(max_us_, sample_us);
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

class AtomicEventRing {
public:
  void record(
      PerfMetric metric,
      u32 duration_us,
      u64 frame_index,
      u64 timestamp_us,
      const char* thread_category,
      const char* context) noexcept {
    if (duration_us == 0U) {
      return;
    }

    const u64 write = write_index_.fetch_add(1U, std::memory_order_relaxed);
    Slot& slot = samples_[write % WorstEventRingCapacity];
    const u64 sequence = (write + 1U) * 2U;
    slot.sequence.store(sequence - 1U, std::memory_order_release);
    slot.metric.store(static_cast<u8>(metric), std::memory_order_relaxed);
    slot.duration_us.store(duration_us, std::memory_order_relaxed);
    slot.frame_index.store(frame_index, std::memory_order_relaxed);
    slot.timestamp_us.store(timestamp_us, std::memory_order_relaxed);
    copy_c_string(slot.thread, thread_category);
    copy_c_string(slot.context, context);
    slot.sequence.store(sequence, std::memory_order_release);

    u32 sample_count = sample_count_.load(std::memory_order_relaxed);
    while (sample_count < WorstEventRingCapacity &&
           !sample_count_.compare_exchange_weak(
               sample_count,
               sample_count + 1U,
               std::memory_order_release,
               std::memory_order_relaxed)) {
    }
  }

  [[nodiscard]] u32 sample_count() const noexcept {
    return std::min<u32>(
        sample_count_.load(std::memory_order_acquire),
        static_cast<u32>(WorstEventRingCapacity));
  }

  [[nodiscard]] bool sample(usize index, PerfEventSample& out) const noexcept {
    if (index >= WorstEventRingCapacity) {
      return false;
    }

    const Slot& slot = samples_[index];
    const u64 before = slot.sequence.load(std::memory_order_acquire);
    if (before == 0U || (before & 1U) != 0U) {
      return false;
    }

    PerfEventSample sample;
    sample.metric = static_cast<PerfMetric>(slot.metric.load(std::memory_order_relaxed));
    sample.duration_us = slot.duration_us.load(std::memory_order_relaxed);
    sample.frame_index = slot.frame_index.load(std::memory_order_relaxed);
    sample.timestamp_us = slot.timestamp_us.load(std::memory_order_relaxed);
    sample.thread = slot.thread;
    sample.context = slot.context;

    const u64 after = slot.sequence.load(std::memory_order_acquire);
    if (before != after || (after & 1U) != 0U || sample.duration_us == 0U) {
      return false;
    }

    out = sample;
    return true;
  }

  void clear() noexcept {
    for (Slot& slot : samples_) {
      slot.sequence.store(0U, std::memory_order_release);
      slot.metric.store(0U, std::memory_order_relaxed);
      slot.duration_us.store(0U, std::memory_order_relaxed);
      slot.frame_index.store(0U, std::memory_order_relaxed);
      slot.timestamp_us.store(0U, std::memory_order_relaxed);
      slot.thread.fill('\0');
      slot.context.fill('\0');
    }
    write_index_.store(0U, std::memory_order_relaxed);
    sample_count_.store(0U, std::memory_order_release);
  }

private:
  struct Slot {
    std::atomic<u64> sequence{};
    std::atomic<u8> metric{};
    std::atomic<u32> duration_us{};
    std::atomic<u64> frame_index{};
    std::atomic<u64> timestamp_us{};
    std::array<char, PerfEventThreadCapacity> thread{};
    std::array<char, PerfEventContextCapacity> context{};
  };

  std::array<Slot, WorstEventRingCapacity> samples_{};
  std::atomic<u64> write_index_{};
  std::atomic<u32> sample_count_{};
};

using GlobalTimingHistograms =
    std::array<AtomicTimingHistogram, static_cast<usize>(PerfMetric::Count)>;

using GlobalWorstEventRings =
    std::array<AtomicEventRing, static_cast<usize>(PerfMetric::Count)>;

[[nodiscard]] GlobalTimingHistograms& global_timing_histograms() noexcept {
  static GlobalTimingHistograms histograms;
  return histograms;
}

[[nodiscard]] GlobalWorstEventRings& global_worst_event_rings() noexcept {
  static GlobalWorstEventRings rings;
  return rings;
}

[[nodiscard]] std::atomic_bool& global_pause_flag() noexcept {
  static std::atomic_bool paused{false};
  return paused;
}

[[nodiscard]] std::atomic<u64>& global_frame_index() noexcept {
  static std::atomic<u64> frame_index{0U};
  return frame_index;
}

template <typename Tag>
[[nodiscard]] std::atomic<u64>& global_u64_counter() noexcept {
  static std::atomic<u64> value{0U};
  return value;
}

struct SnapshotsCreatedCounter {};
struct SnapshotsReusedCounter {};
struct SnapshotCopiesCounter {};
struct SnapshotBytesCopiedCounter {};
struct LightingJobsSubmittedCounter {};
struct LightingJobsCompletedCounter {};
struct LightingJobsContinuedCounter {};
struct LightingNodesProcessedCounter {};
struct LightingAddNodesProcessedCounter {};
struct LightingRemoveNodesProcessedCounter {};
struct LightingBoundaryDeferralsCounter {};
struct LightingSectionsTouchedCounter {};
struct LightingSectionsPublishedCounter {};
struct LightingRemeshesTriggeredCounter {};

[[nodiscard]] std::atomic<u64>& snapshots_created_total() noexcept {
  return global_u64_counter<SnapshotsCreatedCounter>();
}

[[nodiscard]] std::atomic<u64>& snapshots_reused_total() noexcept {
  return global_u64_counter<SnapshotsReusedCounter>();
}

[[nodiscard]] std::atomic<u64>& snapshot_copies_total() noexcept {
  return global_u64_counter<SnapshotCopiesCounter>();
}

[[nodiscard]] std::atomic<u64>& snapshot_bytes_copied_total() noexcept {
  return global_u64_counter<SnapshotBytesCopiedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_jobs_submitted_total() noexcept {
  return global_u64_counter<LightingJobsSubmittedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_jobs_completed_total() noexcept {
  return global_u64_counter<LightingJobsCompletedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_jobs_continued_total() noexcept {
  return global_u64_counter<LightingJobsContinuedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_nodes_processed_total() noexcept {
  return global_u64_counter<LightingNodesProcessedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_add_nodes_processed_total() noexcept {
  return global_u64_counter<LightingAddNodesProcessedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_remove_nodes_processed_total() noexcept {
  return global_u64_counter<LightingRemoveNodesProcessedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_boundary_deferrals_total() noexcept {
  return global_u64_counter<LightingBoundaryDeferralsCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_sections_touched_total() noexcept {
  return global_u64_counter<LightingSectionsTouchedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_sections_published_total() noexcept {
  return global_u64_counter<LightingSectionsPublishedCounter>();
}

[[nodiscard]] std::atomic<u64>& lighting_remeshes_triggered_total() noexcept {
  return global_u64_counter<LightingRemeshesTriggeredCounter>();
}

[[nodiscard]] std::atomic<u32>& lighting_max_queue_depth() noexcept {
  static std::atomic<u32> value{0U};
  return value;
}

[[nodiscard]] bool valid_metric(PerfMetric metric) noexcept {
  return static_cast<usize>(metric) < static_cast<usize>(PerfMetric::Count);
}

[[nodiscard]] u64 timestamp_microseconds() noexcept {
  return static_cast<u64>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void insert_worst_sample(std::span<PerfEventSample> out, usize& count, PerfEventSample sample) noexcept {
  if (out.empty()) {
    return;
  }

  if (count < out.size()) {
    usize position = count;
    while (position > 0U && out[position - 1U].duration_us < sample.duration_us) {
      out[position] = out[position - 1U];
      --position;
    }
    out[position] = sample;
    ++count;
    return;
  }

  if (out.back().duration_us >= sample.duration_us) {
    return;
  }

  usize position = out.size() - 1U;
  while (position > 0U && out[position - 1U].duration_us < sample.duration_us) {
    out[position] = out[position - 1U];
    --position;
  }
  out[position] = sample;
}

} // namespace

void format_chunk_section_perf_context(
    std::array<char, PerfEventContextCapacity>& context,
    ChunkSectionCoord coord) noexcept {
  (void)std::snprintf(
      context.data(),
      context.size(),
      "section=(%d,%d,%d)",
      coord.value.x,
      coord.value.y,
      coord.value.z);
}

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
  record_thread_timing(metric, milliseconds, perf_metric_thread_category(metric), nullptr);
}

void record_thread_timing(
    PerfMetric metric,
    double milliseconds,
    const char* thread_category,
    const char* context) noexcept {
#if !ARVANE_ENABLE_PROFILING
  (void)metric;
  (void)milliseconds;
  (void)thread_category;
  (void)context;
  return;
#else
  if (!valid_metric(metric) || global_timing_paused()) {
    return;
  }
  const u32 sample_us = clamp_to_microseconds(milliseconds);
  thread_timing_histograms().metrics[static_cast<usize>(metric)].record_ms(milliseconds);
  global_timing_histograms()[static_cast<usize>(metric)].record_ms(milliseconds);
  global_worst_event_rings()[static_cast<usize>(metric)].record(
      metric,
      sample_us,
      current_profile_frame_index(),
      timestamp_microseconds(),
      thread_category == nullptr ? perf_metric_thread_category(metric) : thread_category,
      context);
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
    case PerfMetric::FrameDelta:
      return "Frame delta";
    case PerfMetric::CpuFrameWork:
      return "CPU frame work";
    case PerfMetric::SimulationTick:
      return "Simulation tick";
    case PerfMetric::RenderSubmit:
      return "Render submit";
    case PerfMetric::GpuWaitTime:
      return "GPU wait time";
    case PerfMetric::GpuQueueTiming:
      return "GPU queue timing";
    case PerfMetric::SnapshotCreation:
      return "Snapshot creation";
    case PerfMetric::ChunkLookupAccess:
      return "Chunk lookup / access";
    case PerfMetric::MeshingTask:
      return "Meshing tasks";
    case PerfMetric::LightingTask:
      return "Lighting tasks";
    case PerfMetric::GenerationTask:
      return "Chunk generation";
    case PerfMetric::FluidTask:
      return "Fluid simulation";
    case PerfMetric::JobQueueLatency:
      return "Job queue latency";
    case PerfMetric::Count:
      break;
  }
  return "Unknown";
}

const char* perf_metric_thread_category(PerfMetric metric) noexcept {
  switch (metric) {
    case PerfMetric::FrameDelta:
    case PerfMetric::CpuFrameWork:
      return "main/render";
    case PerfMetric::SimulationTick:
      return "main/world";
    case PerfMetric::RenderSubmit:
    case PerfMetric::GpuWaitTime:
      return "main/render";
    case PerfMetric::GpuQueueTiming:
      return "GPU";
    case PerfMetric::SnapshotCreation:
    case PerfMetric::ChunkLookupAccess:
      return "world";
    case PerfMetric::MeshingTask:
      return "worker/client";
    case PerfMetric::LightingTask:
    case PerfMetric::GenerationTask:
    case PerfMetric::FluidTask:
      return "worker/server";
    case PerfMetric::JobQueueLatency:
      return "worker";
    case PerfMetric::Count:
      break;
  }
  return "-";
}

PerfEventGroup perf_metric_group(PerfMetric metric) noexcept {
  switch (metric) {
    case PerfMetric::FrameDelta:
    case PerfMetric::CpuFrameWork:
    case PerfMetric::SimulationTick:
    case PerfMetric::RenderSubmit:
    case PerfMetric::GpuWaitTime:
    case PerfMetric::GpuQueueTiming:
      return PerfEventGroup::Frame;
    case PerfMetric::MeshingTask:
    case PerfMetric::LightingTask:
    case PerfMetric::GenerationTask:
    case PerfMetric::FluidTask:
    case PerfMetric::JobQueueLatency:
      return PerfEventGroup::Worker;
    case PerfMetric::SnapshotCreation:
    case PerfMetric::ChunkLookupAccess:
      return PerfEventGroup::Zone;
    case PerfMetric::Count:
      break;
  }
  return PerfEventGroup::All;
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

usize global_worst_event_samples(std::span<PerfEventSample> out, PerfEventGroup group) noexcept {
#if !ARVANE_ENABLE_PROFILING
  (void)out;
  (void)group;
  return 0U;
#else
  usize count = 0U;
  for (u8 metric_index = 0; metric_index < static_cast<u8>(PerfMetric::Count); ++metric_index) {
    const auto metric = static_cast<PerfMetric>(metric_index);
    if (group != PerfEventGroup::All && perf_metric_group(metric) != group) {
      continue;
    }

    const AtomicEventRing& ring = global_worst_event_rings()[metric_index];
    const u32 sample_count = ring.sample_count();
    for (u32 sample_index = 0; sample_index < sample_count; ++sample_index) {
      PerfEventSample sample;
      if (ring.sample(sample_index, sample)) {
        insert_worst_sample(out, count, sample);
      }
    }
  }
  return count;
#endif
}

void set_global_timing_paused(bool paused) noexcept {
#if ARVANE_ENABLE_PROFILING
  global_pause_flag().store(paused, std::memory_order_release);
#else
  (void)paused;
#endif
}

bool global_timing_paused() noexcept {
#if ARVANE_ENABLE_PROFILING
  return global_pause_flag().load(std::memory_order_acquire);
#else
  return false;
#endif
}

void set_current_profile_frame_index(u64 frame_index) noexcept {
#if ARVANE_ENABLE_PROFILING
  global_frame_index().store(frame_index, std::memory_order_release);
#else
  (void)frame_index;
#endif
}

u64 current_profile_frame_index() noexcept {
#if ARVANE_ENABLE_PROFILING
  return global_frame_index().load(std::memory_order_acquire);
#else
  return 0U;
#endif
}

void record_snapshot_created() noexcept {
#if ARVANE_ENABLE_PROFILING
  if (!global_timing_paused()) {
    snapshots_created_total().fetch_add(1U, std::memory_order_relaxed);
  }
#endif
}

void record_snapshot_reused() noexcept {
#if ARVANE_ENABLE_PROFILING
  if (!global_timing_paused()) {
    snapshots_reused_total().fetch_add(1U, std::memory_order_relaxed);
  }
#endif
}

void record_snapshot_copy(usize bytes) noexcept {
#if ARVANE_ENABLE_PROFILING
  if (!global_timing_paused()) {
    snapshot_copies_total().fetch_add(1U, std::memory_order_relaxed);
    snapshot_bytes_copied_total().fetch_add(static_cast<u64>(bytes), std::memory_order_relaxed);
  }
#else
  (void)bytes;
#endif
}

SnapshotCostSnapshot global_snapshot_cost_snapshot() noexcept {
  SnapshotCostSnapshot snapshot;
#if ARVANE_ENABLE_PROFILING
  snapshot.snapshots_created_total =
      snapshots_created_total().load(std::memory_order_relaxed);
  snapshot.snapshots_reused_total =
      snapshots_reused_total().load(std::memory_order_relaxed);
  snapshot.snapshot_copies_total =
      snapshot_copies_total().load(std::memory_order_relaxed);
  snapshot.snapshot_bytes_copied_total =
      snapshot_bytes_copied_total().load(std::memory_order_relaxed);
#endif
  return snapshot;
}

void record_lighting_job_submitted() noexcept {
#if ARVANE_ENABLE_PROFILING
  if (!global_timing_paused()) {
    lighting_jobs_submitted_total().fetch_add(1U, std::memory_order_relaxed);
  }
#endif
}

void record_lighting_job_completed(const LightingStatsUpdate& update) noexcept {
#if ARVANE_ENABLE_PROFILING
  if (global_timing_paused()) {
    return;
  }
  lighting_jobs_completed_total().fetch_add(1U, std::memory_order_relaxed);
  if (update.continued) {
    lighting_jobs_continued_total().fetch_add(1U, std::memory_order_relaxed);
  }
  lighting_nodes_processed_total().fetch_add(update.nodes_processed, std::memory_order_relaxed);
  lighting_add_nodes_processed_total().fetch_add(update.add_nodes_processed, std::memory_order_relaxed);
  lighting_remove_nodes_processed_total().fetch_add(update.remove_nodes_processed, std::memory_order_relaxed);
  lighting_boundary_deferrals_total().fetch_add(update.boundary_deferrals, std::memory_order_relaxed);
  lighting_sections_touched_total().fetch_add(update.sections_touched, std::memory_order_relaxed);
  lighting_sections_published_total().fetch_add(update.sections_published, std::memory_order_relaxed);
  lighting_remeshes_triggered_total().fetch_add(update.remeshes_triggered, std::memory_order_relaxed);
  update_atomic_maximum(lighting_max_queue_depth(), update.max_queue_depth);
#else
  (void)update;
#endif
}

void record_lighting_sections_published(u32 count) noexcept {
#if ARVANE_ENABLE_PROFILING
  if (!global_timing_paused()) {
    lighting_sections_published_total().fetch_add(count, std::memory_order_relaxed);
  }
#else
  (void)count;
#endif
}

void record_lighting_remeshes_triggered(u32 count) noexcept {
#if ARVANE_ENABLE_PROFILING
  if (!global_timing_paused()) {
    lighting_remeshes_triggered_total().fetch_add(count, std::memory_order_relaxed);
  }
#else
  (void)count;
#endif
}

void record_lighting_boundary_deferrals(u32 count) noexcept {
#if ARVANE_ENABLE_PROFILING
  if (!global_timing_paused()) {
    lighting_boundary_deferrals_total().fetch_add(count, std::memory_order_relaxed);
  }
#else
  (void)count;
#endif
}

LightingStatsSnapshot global_lighting_stats_snapshot() noexcept {
  LightingStatsSnapshot snapshot;
#if ARVANE_ENABLE_PROFILING
  snapshot.jobs_submitted_total =
      lighting_jobs_submitted_total().load(std::memory_order_relaxed);
  snapshot.jobs_completed_total =
      lighting_jobs_completed_total().load(std::memory_order_relaxed);
  snapshot.jobs_continued_total =
      lighting_jobs_continued_total().load(std::memory_order_relaxed);
  snapshot.nodes_processed_total =
      lighting_nodes_processed_total().load(std::memory_order_relaxed);
  snapshot.add_nodes_processed_total =
      lighting_add_nodes_processed_total().load(std::memory_order_relaxed);
  snapshot.remove_nodes_processed_total =
      lighting_remove_nodes_processed_total().load(std::memory_order_relaxed);
  snapshot.boundary_deferrals_total =
      lighting_boundary_deferrals_total().load(std::memory_order_relaxed);
  snapshot.sections_touched_total =
      lighting_sections_touched_total().load(std::memory_order_relaxed);
  snapshot.sections_published_total =
      lighting_sections_published_total().load(std::memory_order_relaxed);
  snapshot.remeshes_triggered_total =
      lighting_remeshes_triggered_total().load(std::memory_order_relaxed);
  snapshot.max_queue_depth =
      lighting_max_queue_depth().load(std::memory_order_relaxed);
#endif
  return snapshot;
}

void reset_global_timing_stats() noexcept {
#if ARVANE_ENABLE_PROFILING
  for (AtomicTimingHistogram& histogram : global_timing_histograms()) {
    histogram.clear();
  }
  for (AtomicEventRing& ring : global_worst_event_rings()) {
    ring.clear();
  }
  snapshots_created_total().store(0U, std::memory_order_relaxed);
  snapshots_reused_total().store(0U, std::memory_order_relaxed);
  snapshot_copies_total().store(0U, std::memory_order_relaxed);
  snapshot_bytes_copied_total().store(0U, std::memory_order_relaxed);
  lighting_jobs_submitted_total().store(0U, std::memory_order_relaxed);
  lighting_jobs_completed_total().store(0U, std::memory_order_relaxed);
  lighting_jobs_continued_total().store(0U, std::memory_order_relaxed);
  lighting_nodes_processed_total().store(0U, std::memory_order_relaxed);
  lighting_add_nodes_processed_total().store(0U, std::memory_order_relaxed);
  lighting_remove_nodes_processed_total().store(0U, std::memory_order_relaxed);
  lighting_boundary_deferrals_total().store(0U, std::memory_order_relaxed);
  lighting_sections_touched_total().store(0U, std::memory_order_relaxed);
  lighting_sections_published_total().store(0U, std::memory_order_relaxed);
  lighting_remeshes_triggered_total().store(0U, std::memory_order_relaxed);
  lighting_max_queue_depth().store(0U, std::memory_order_relaxed);
#endif
}

ScopedThreadTiming::ScopedThreadTiming(PerfMetric metric) noexcept
    : ScopedThreadTiming(metric, perf_metric_thread_category(metric), nullptr) {
}

ScopedThreadTiming::ScopedThreadTiming(
    PerfMetric metric,
    const char* thread_category,
    const char* context) noexcept
    : metric_(metric),
      thread_category_(thread_category),
      context_(context) {
#if ARVANE_ENABLE_PROFILING
  start_ = std::chrono::steady_clock::now();
#endif
}

ScopedThreadTiming::~ScopedThreadTiming() {
#if ARVANE_ENABLE_PROFILING
  const auto end = std::chrono::steady_clock::now();
  record_thread_timing(
      metric_,
      std::chrono::duration<double, std::milli>(end - start_).count(),
      thread_category_,
      context_);
#endif
}

} // namespace arv::core
