#include "arv/sched/completion_queue.hpp"
#include "arv/sched/scheduler.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

TEST(SchedulerTests, JobCompletesAndReturnsValue) {
  arv::sched::Scheduler scheduler(1);

  auto job = scheduler.submit("test_job", [] {
    return 42;
  });

  EXPECT_EQ(job.get(), 42);
}

TEST(SchedulerTests, CompletionQueuePublishesWorkerResult) {
  arv::sched::Scheduler scheduler(1);
  arv::sched::CompletionQueue<int> completions;

  auto job = scheduler.submit("publish_result", [&completions] {
    completions.push(7);
  });

  job.get();
  const std::vector<int> drained = completions.drain();

  ASSERT_EQ(drained.size(), 1U);
  EXPECT_EQ(drained.front(), 7);
  EXPECT_TRUE(completions.empty());
}

TEST(SchedulerTests, CompletionQueueDrainsConcurrentWorkerResults) {
  arv::sched::Scheduler scheduler(4);
  arv::sched::CompletionQueue<int> completions;
  std::vector<arv::sched::JobHandle<void>> jobs;

  for (int value = 0; value < 32; ++value) {
    jobs.push_back(scheduler.submit("publish_result", [&completions, value] {
      completions.push(value);
    }));
  }

  for (auto& job : jobs) {
    job.get();
  }

  std::vector<int> drained = completions.drain();
  std::sort(drained.begin(), drained.end());

  ASSERT_EQ(drained.size(), 32U);
  for (int value = 0; value < 32; ++value) {
    EXPECT_EQ(drained[static_cast<arv::usize>(value)], value);
  }
  EXPECT_TRUE(completions.empty());
}

TEST(SchedulerTests, WorkerThreadPolicyClampsConfiguredCount) {
  arv::sched::WorkerThreadConfig config;
  config.worker_thread_count = 99;
  config.min_worker_thread_count = 2;
  config.max_worker_thread_count = 8;

  EXPECT_EQ(arv::sched::resolve_worker_thread_count(config), 8U);
}

TEST(SchedulerTests, WorkerThreadPolicyReservesEngineThreadsForAutomaticCount) {
  arv::sched::WorkerThreadConfig config;
  config.worker_thread_count = 0;
  config.reserved_thread_count = arv::sched::hardware_thread_count_or_one() + 10U;
  config.min_worker_thread_count = 1;
  config.max_worker_thread_count = 64;

  EXPECT_EQ(arv::sched::resolve_worker_thread_count(config), 1U);
}

TEST(SchedulerTests, RuntimeThreadBudgetReservesNamedRolesAndSplitsPools) {
  arv::sched::RuntimeThreadBudgetConfig config;
  config.workers.worker_thread_count = 6;
  config.workers.reserved_thread_count = 1;
  config.workers.min_worker_thread_count = 1;
  config.workers.max_worker_thread_count = 16;
  config.split_primary_secondary_pools = true;

  const arv::sched::RuntimeThreadBudgetPlan plan =
      arv::sched::plan_runtime_thread_budget(config);

  EXPECT_GE(plan.reserved_thread_count, 4U);
  EXPECT_EQ(plan.total_worker_thread_count, 6U);
  EXPECT_EQ(
      plan.primary_worker_thread_count + plan.secondary_worker_thread_count,
      plan.total_worker_thread_count);
  EXPECT_EQ(plan.primary_workers.worker_thread_count, plan.primary_worker_thread_count);
  EXPECT_EQ(plan.secondary_workers.worker_thread_count, plan.secondary_worker_thread_count);
}

TEST(SchedulerTests, RuntimeThreadBudgetKeepsSplitPoolsRunnableWhenAutoResolvesLow) {
  arv::sched::RuntimeThreadBudgetConfig config;
  config.workers.worker_thread_count = 1;
  config.workers.min_worker_thread_count = 1;
  config.workers.max_worker_thread_count = 1;
  config.split_primary_secondary_pools = true;

  const arv::sched::RuntimeThreadBudgetPlan plan =
      arv::sched::plan_runtime_thread_budget(config);

  EXPECT_EQ(plan.primary_worker_thread_count, 1U);
  EXPECT_EQ(plan.secondary_worker_thread_count, 1U);
}
