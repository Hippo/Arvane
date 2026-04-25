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
