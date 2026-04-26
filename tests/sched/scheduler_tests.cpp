#include "arv/sched/completion_queue.hpp"
#include "arv/sched/scheduler.hpp"

#include <gtest/gtest.h>

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
