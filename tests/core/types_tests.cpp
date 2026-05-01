#include "arv/core/id.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace {

arv::i32 add_one(arv::i32 value) {
  return value + 1;
}

} // namespace

TEST(CoreTypesTests, PrimitiveAliasesUseExpectedStorage) {
  static_assert(std::is_same_v<arv::i8, std::int8_t>);
  static_assert(std::is_same_v<arv::i16, std::int16_t>);
  static_assert(std::is_same_v<arv::i32, std::int32_t>);
  static_assert(std::is_same_v<arv::i64, std::int64_t>);
  static_assert(std::is_same_v<arv::u8, std::uint8_t>);
  static_assert(std::is_same_v<arv::u16, std::uint16_t>);
  static_assert(std::is_same_v<arv::u32, std::uint32_t>);
  static_assert(std::is_same_v<arv::u64, std::uint64_t>);
  static_assert(std::is_same_v<arv::f32, float>);
  static_assert(std::is_same_v<arv::f64, double>);
  static_assert(std::is_same_v<arv::usize, std::size_t>);
  static_assert(std::is_same_v<arv::isize, std::ptrdiff_t>);
  SUCCEED();
}

TEST(CoreTypesTests, FunctionRefIsCheapToCopy) {
  static_assert(std::is_trivially_copyable_v<arv::FunctionRef<void()>>);
  EXPECT_EQ(sizeof(arv::FunctionRef<void()>), sizeof(void*) * 2U);
}

TEST(CoreTypesTests, HashMapSupportsStrongIdKeys) {
  struct TestTag {};
  using TestID = arv::ID<TestTag>;

  arv::HashMap<TestID, std::string> values;
  values.emplace(TestID{7}, "seven");

  ASSERT_NE(values.find(TestID{7}), values.end());
  EXPECT_EQ(values.at(TestID{7}), "seven");
  EXPECT_EQ(values.find(TestID{8}), values.end());
}

TEST(CoreTypesTests, HashSetSupportsStrongIdKeys) {
  struct TestTag {};
  using TestID = arv::ID<TestTag>;

  arv::HashSet<TestID> values;
  values.insert(TestID{3});

  EXPECT_NE(values.find(TestID{3}), values.end());
  EXPECT_EQ(values.find(TestID{4}), values.end());
}

TEST(CoreTypesTests, StableHashMapKeepsElementAddressesAcrossGrowth) {
  arv::StableHashMap<arv::u32, std::string> values;
  values.reserve(1);
  values.emplace(1U, "one");
  const std::string* stored = &values.at(1U);

  for (arv::u32 value = 2; value < 256; ++value) {
    values.emplace(value, std::to_string(value));
  }

  EXPECT_EQ(stored, &values.at(1U));
  EXPECT_EQ(*stored, "one");
}

TEST(CoreTypesTests, StableHashSetKeepsElementAddressesAcrossGrowth) {
  arv::StableHashSet<std::string> values;
  values.reserve(1);
  auto [iter, inserted] = values.emplace("root");
  ASSERT_TRUE(inserted);
  const std::string* stored = &(*iter);

  for (arv::u32 value = 0; value < 256; ++value) {
    values.emplace("value_" + std::to_string(value));
  }

  const auto found = values.find("root");
  ASSERT_NE(found, values.end());
  EXPECT_EQ(stored, &(*found));
  EXPECT_EQ(*stored, "root");
}

TEST(CoreTypesTests, FunctionRefInvokesLambdaWithoutOwningIt) {
  arv::i32 total = 0;
  auto add_to_total = [&total](arv::i32 value) {
    total += value;
  };

  arv::FunctionRef<void(arv::i32)> callback{add_to_total};
  ASSERT_TRUE(callback);

  callback(3);
  callback(4);

  EXPECT_EQ(total, 7);
}

TEST(CoreTypesTests, FunctionRefInvokesFunctionPointer) {
  arv::FunctionRef<arv::i32(arv::i32)> callback{add_one};

  ASSERT_TRUE(callback);
  EXPECT_EQ(callback(41), 42);
}

TEST(CoreTypesTests, EmptyFunctionRefIsFalse) {
  const arv::FunctionRef<void()> callback{};

  EXPECT_FALSE(callback);
}

TEST(CoreTypesTests, NullFunctionPointerRefIsFalse) {
  using Callback = void (*)();
  const arv::FunctionRef<void()> callback{static_cast<Callback>(nullptr)};

  EXPECT_FALSE(callback);
}

TEST(CoreTypesTests, GlobalTimingStatsExposeRecordedPercentiles) {
  arv::core::reset_global_timing_stats();
  if constexpr (!arv::core::perf_stats_enabled()) {
    const arv::core::TimingMetricSnapshot snapshot =
        arv::core::global_timing_snapshot(arv::core::PerfMetric::MeshingTask);
    EXPECT_EQ(snapshot.percentiles.sample_count, 0U);
    return;
  }

  arv::core::record_thread_timing(arv::core::PerfMetric::MeshingTask, 1.0);
  arv::core::record_thread_timing(arv::core::PerfMetric::MeshingTask, 2.0);
  arv::core::record_thread_timing(arv::core::PerfMetric::MeshingTask, 3.0);

  const arv::core::TimingMetricSnapshot snapshot =
      arv::core::global_timing_snapshot(arv::core::PerfMetric::MeshingTask);

  EXPECT_STREQ(arv::core::perf_metric_name(arv::core::PerfMetric::MeshingTask), "Meshing tasks");
  EXPECT_EQ(snapshot.percentiles.sample_count, 3U);
  EXPECT_DOUBLE_EQ(snapshot.latest_ms, 3.0);
  EXPECT_DOUBLE_EQ(snapshot.max_ms, 3.0);
  EXPECT_DOUBLE_EQ(snapshot.percentiles.p50_ms, 2.0);
  EXPECT_DOUBLE_EQ(snapshot.percentiles.p95_ms, 3.0);
  EXPECT_DOUBLE_EQ(snapshot.percentiles.p99_ms, 3.0);

  arv::core::reset_global_timing_stats();
  const arv::core::TimingMetricSnapshot cleared =
      arv::core::global_timing_snapshot(arv::core::PerfMetric::MeshingTask);
  EXPECT_EQ(cleared.percentiles.sample_count, 0U);
}

TEST(CoreTypesTests, WorstEventSamplesKeepBoundedSlowestRecentEvents) {
  arv::core::reset_global_timing_stats();
  if constexpr (!arv::core::perf_stats_enabled()) {
    std::array<arv::core::PerfEventSample, 4> samples{};
    EXPECT_EQ(
        arv::core::global_worst_event_samples(
            samples,
            arv::core::PerfEventGroup::Worker),
        0U);
    return;
  }

  arv::core::set_current_profile_frame_index(42);
  arv::core::record_thread_timing(
      arv::core::PerfMetric::MeshingTask,
      1.0,
      "worker/client",
      "section=(1,2,3)");
  arv::core::record_thread_timing(
      arv::core::PerfMetric::LightingTask,
      9.0,
      "worker/server",
      "section=(4,5,6)");

  std::array<arv::core::PerfEventSample, 4> samples{};
  const arv::usize count = arv::core::global_worst_event_samples(
      samples,
      arv::core::PerfEventGroup::Worker);

  ASSERT_GE(count, 2U);
  EXPECT_EQ(samples[0].metric, arv::core::PerfMetric::LightingTask);
  EXPECT_DOUBLE_EQ(samples[0].duration_ms(), 9.0);
  EXPECT_EQ(samples[0].frame_index, 42U);
  EXPECT_STREQ(samples[0].thread.data(), "worker/server");
  EXPECT_STREQ(samples[0].context.data(), "section=(4,5,6)");
  arv::core::reset_global_timing_stats();
}

TEST(CoreTypesTests, SnapshotCostCountersExposeTotals) {
  arv::core::reset_global_timing_stats();
  if constexpr (!arv::core::perf_stats_enabled()) {
    const arv::core::SnapshotCostSnapshot snapshot =
        arv::core::global_snapshot_cost_snapshot();
    EXPECT_EQ(snapshot.snapshots_created_total, 0U);
    return;
  }

  arv::core::record_snapshot_created();
  arv::core::record_snapshot_reused();
  arv::core::record_snapshot_copy(128);

  const arv::core::SnapshotCostSnapshot snapshot =
      arv::core::global_snapshot_cost_snapshot();
  EXPECT_EQ(snapshot.snapshots_created_total, 1U);
  EXPECT_EQ(snapshot.snapshots_reused_total, 1U);
  EXPECT_EQ(snapshot.snapshot_copies_total, 1U);
  EXPECT_EQ(snapshot.snapshot_bytes_copied_total, 128U);
  arv::core::reset_global_timing_stats();
}
