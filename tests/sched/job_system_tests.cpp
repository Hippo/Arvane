#include "arv/sched/completion_queue.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/sched/owner_thread.hpp"
#include "arv/sched/task_executor.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

[[nodiscard]] arv::sched::JobSystem make_test_job_system() {
  arv::sched::JobSystemConfig config;
  config.critical_cpu_workers = 1;
  config.background_cpu_workers = 1;
  config.blocking_io_workers = 1;
  arv::sched::JobSystem jobs{config};
  jobs.start();
  return jobs;
}

[[nodiscard]] arv::sched::JobDesc desc(
    const char* name,
    arv::sched::JobLane lane,
    arv::sched::JobDomain domain = arv::sched::JobDomain::Generic) {
  arv::sched::JobDesc result;
  result.name = name;
  result.lane = lane;
  result.domain = domain;
  return result;
}

} // namespace

TEST(JobSystemTests, JobSystemRunsCriticalCpuJob) {
  auto jobs = make_test_job_system();

  auto job = jobs.submit_result(desc("critical", arv::sched::JobLane::CriticalCpu), [] {
    return 11;
  });

  EXPECT_EQ(job.get(), 11);
}

TEST(JobSystemTests, JobSystemRunsBackgroundCpuJob) {
  auto jobs = make_test_job_system();

  auto job = jobs.submit_result(desc("background", arv::sched::JobLane::BackgroundCpu), [] {
    return 17;
  });

  EXPECT_EQ(job.get(), 17);
}

TEST(JobSystemTests, JobSystemRunsBlockingIoJob) {
  auto jobs = make_test_job_system();

  auto job = jobs.submit_result(desc("blocking_io", arv::sched::JobLane::BlockingIo), [] {
    return 23;
  });

  EXPECT_EQ(job.get(), 23);
}

TEST(JobSystemTests, JobSystemPumpsCompletionOnRequestedTarget) {
  auto jobs = make_test_job_system();
  std::atomic<int> value{0};

  arv::sched::JobDesc job_desc = desc("completion", arv::sched::JobLane::BackgroundCpu);
  job_desc.completion_target = arv::sched::JobCompletionTarget::World;
  const arv::sched::JobHandle handle = jobs.submit_with_completion(
      std::move(job_desc),
      [&value](const arv::sched::CancellationToken&) {
        value.store(1, std::memory_order_release);
      },
      [&value] {
        value.store(2, std::memory_order_release);
      });

  ASSERT_TRUE(handle.valid());
  jobs.join();
  EXPECT_EQ(value.load(std::memory_order_acquire), 1);
  jobs.pump_completions(arv::sched::JobCompletionTarget::Render, 8);
  EXPECT_EQ(value.load(std::memory_order_acquire), 1);
  jobs.pump_completions(arv::sched::JobCompletionTarget::World, 8);
  EXPECT_EQ(value.load(std::memory_order_acquire), 2);
}

TEST(JobSystemTests, JobSystemCancellationTokenIsObserved) {
  auto jobs = make_test_job_system();
  auto started = std::make_shared<std::promise<void>>();
  std::future<void> started_future = started->get_future();
  auto release = std::make_shared<std::promise<void>>();
  std::shared_future<void> release_future = release->get_future().share();
  std::atomic<bool> observed{false};

  auto job = jobs.submit_result(
      desc("cancel_me", arv::sched::JobLane::CriticalCpu),
      [started, release_future, &observed](const arv::sched::CancellationToken& token) {
        started->set_value();
        release_future.wait();
        observed.store(token.is_canceled(), std::memory_order_release);
      });

  ASSERT_EQ(started_future.wait_for(2s), std::future_status::ready);
  jobs.cancel(job.handle());
  release->set_value();
  job.get();

  EXPECT_TRUE(observed.load(std::memory_order_acquire));
  EXPECT_EQ(jobs.stats().canceled, 1U);
}

TEST(JobSystemTests, JobSystemRecordsFailureForThrowingJob) {
  auto jobs = make_test_job_system();

  auto job = jobs.submit_result(desc("throws", arv::sched::JobLane::BackgroundCpu), []() -> int {
    throw std::runtime_error{"boom"};
  });

  EXPECT_THROW((void)job.get(), std::runtime_error);
  jobs.join();
  EXPECT_EQ(jobs.stats().failed, 1U);
}

TEST(JobSystemTests, CriticalCpuJobCompletesWhileBackgroundCpuIsSaturated) {
  auto jobs = make_test_job_system();
  auto background_started = std::make_shared<std::promise<void>>();
  std::future<void> background_started_future = background_started->get_future();
  auto release_background = std::make_shared<std::promise<void>>();
  std::shared_future<void> release_future = release_background->get_future().share();
  auto critical_started = std::make_shared<std::promise<void>>();
  std::future<void> critical_started_future = critical_started->get_future();

  auto background = jobs.submit_result(
      desc("blocked_background", arv::sched::JobLane::BackgroundCpu),
      [background_started, release_future] {
        background_started->set_value();
        release_future.wait();
      });
  ASSERT_EQ(background_started_future.wait_for(2s), std::future_status::ready);

  auto critical = jobs.submit_result(
      desc("critical_during_background", arv::sched::JobLane::CriticalCpu),
      [critical_started] {
        critical_started->set_value();
        return 31;
      });

  EXPECT_EQ(critical_started_future.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(critical.get(), 31);
  release_background->set_value();
  background.get();
}

TEST(JobSystemTests, DomainBudgetRejectsExcessInFlightWork) {
  arv::sched::JobSystemConfig config;
  config.critical_cpu_workers = 1;
  config.background_cpu_workers = 1;
  config.blocking_io_workers = 1;
  config.max_mesh_jobs_in_flight = 1;
  arv::sched::JobSystem jobs{config};
  jobs.start();

  auto started = std::make_shared<std::promise<void>>();
  std::future<void> started_future = started->get_future();
  auto release = std::make_shared<std::promise<void>>();
  std::shared_future<void> release_future = release->get_future().share();

  auto first = jobs.submit_result(
      desc("mesh_1", arv::sched::JobLane::BackgroundCpu, arv::sched::JobDomain::Meshing),
      [started, release_future] {
        started->set_value();
        release_future.wait();
      });
  ASSERT_TRUE(first.valid());
  ASSERT_EQ(started_future.wait_for(2s), std::future_status::ready);

  auto second = jobs.submit_result(
      desc("mesh_2", arv::sched::JobLane::BackgroundCpu, arv::sched::JobDomain::Meshing),
      [] {});
  EXPECT_FALSE(second.valid());

  release->set_value();
  first.get();
}

TEST(JobSystemTests, CompletionQueuePublishesWorkerResult) {
  auto jobs = make_test_job_system();
  arv::sched::CompletionQueue<int> completions;

  auto job = jobs.submit_result(desc("publish_result", arv::sched::JobLane::BackgroundCpu), [&completions] {
    completions.push(7);
  });

  job.get();
  const std::vector<int> drained = completions.drain();

  ASSERT_EQ(drained.size(), 1U);
  EXPECT_EQ(drained.front(), 7);
  EXPECT_TRUE(completions.empty());
}

TEST(JobSystemTests, CompletionQueueDrainsConcurrentWorkerResults) {
  arv::sched::JobSystemConfig config;
  config.critical_cpu_workers = 1;
  config.background_cpu_workers = 4;
  config.blocking_io_workers = 1;
  arv::sched::JobSystem jobs{config};
  jobs.start();
  arv::sched::CompletionQueue<int> completions;
  std::vector<arv::sched::JobFuture<void>> submitted_jobs;

  for (int value = 0; value < 32; ++value) {
    submitted_jobs.push_back(jobs.submit_result(
        desc("publish_result", arv::sched::JobLane::BackgroundCpu),
        [&completions, value] {
          completions.push(value);
        }));
  }

  for (auto& job : submitted_jobs) {
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

TEST(TaskExecutorTests, SingleThreadExecutorRunsOnlyWhenPumped) {
  arv::sched::SingleThreadTaskExecutor executor;
  int value = 0;

  const arv::sched::JobHandle handle = executor.submit(
      desc("single", arv::sched::JobLane::BackgroundCpu),
      [&value](const arv::sched::CancellationToken&) {
        value = 7;
      });

  ASSERT_TRUE(handle.valid());
  EXPECT_EQ(value, 0);
  executor.pump({1ms, 1});
  EXPECT_EQ(value, 7);
  EXPECT_EQ(executor.stats().completed, 1U);
}

TEST(TaskExecutorTests, SingleThreadExecutorRespectsMaxTasksPerPump) {
  arv::sched::SingleThreadTaskExecutor executor;
  std::vector<int> values;

  for (int value = 0; value < 3; ++value) {
    ASSERT_TRUE(executor.submit(
        desc("single", arv::sched::JobLane::BackgroundCpu),
        [&values, value](const arv::sched::CancellationToken&) {
          values.push_back(value);
        }).valid());
  }

  executor.pump({1ms, 2});
  ASSERT_EQ(values.size(), 2U);
  EXPECT_EQ(executor.queued_count(), 1U);
  executor.pump({1ms, 2});
  ASSERT_EQ(values.size(), 3U);
  EXPECT_EQ(values[2], 2);
}

TEST(TaskExecutorTests, SingleThreadExecutorMaintainsQueuedStatsWhenPumped) {
  arv::sched::SingleThreadTaskExecutor executor;

  ASSERT_TRUE(executor.submit(
      desc("queued_1", arv::sched::JobLane::BackgroundCpu),
      [](const arv::sched::CancellationToken&) {}).valid());
  ASSERT_TRUE(executor.submit(
      desc("queued_2", arv::sched::JobLane::BackgroundCpu),
      [](const arv::sched::CancellationToken&) {}).valid());

  EXPECT_EQ(executor.stats().queued_background_cpu, 2U);
  executor.pump({1ms, 1});
  EXPECT_EQ(executor.stats().completed, 1U);
  EXPECT_EQ(executor.stats().queued_background_cpu, 1U);
  EXPECT_EQ(executor.stats().running_background_cpu, 0U);
  executor.pump({1ms, 1});
  EXPECT_EQ(executor.stats().queued_background_cpu, 0U);
}

TEST(TaskExecutorTests, SingleThreadExecutorRespectsTimeBudgetBetweenTasks) {
  std::chrono::steady_clock::time_point now{};
  arv::sched::SingleThreadTaskExecutor executor{[&now] {
    return now;
  }};
  std::vector<int> values;

  ASSERT_TRUE(executor.submit(
      desc("first", arv::sched::JobLane::BackgroundCpu),
      [&values, &now](const arv::sched::CancellationToken&) {
        values.push_back(1);
        now += 2ms;
      }).valid());
  ASSERT_TRUE(executor.submit(
      desc("second", arv::sched::JobLane::BackgroundCpu),
      [&values](const arv::sched::CancellationToken&) {
        values.push_back(2);
      }).valid());

  executor.pump({1ms, 8});
  ASSERT_EQ(values.size(), 1U);
  EXPECT_EQ(executor.queued_count(), 1U);
}

TEST(TaskExecutorTests, SingleThreadExecutorCanceledQueuedTaskDoesNotRun) {
  arv::sched::SingleThreadTaskExecutor executor;
  bool ran = false;

  const arv::sched::JobHandle handle = executor.submit(
      desc("cancel", arv::sched::JobLane::BackgroundCpu),
      [&ran](const arv::sched::CancellationToken&) {
        ran = true;
      });

  executor.cancel(handle);
  executor.pump({1ms, 8});
  EXPECT_FALSE(ran);
  EXPECT_EQ(executor.stats().canceled, 1U);
  EXPECT_EQ(executor.stats().started, 0U);
}

TEST(TaskExecutorTests, SingleThreadExecutorPumpsCompletionsOnTarget) {
  arv::sched::SingleThreadTaskExecutor executor;
  int value = 0;
  arv::sched::JobDesc job_desc = desc("completion", arv::sched::JobLane::BackgroundCpu);
  job_desc.completion_target = arv::sched::JobCompletionTarget::Main;

  ASSERT_TRUE(executor.submit_with_completion(
      std::move(job_desc),
      [](const arv::sched::CancellationToken&) {},
      [&value] {
        value = 5;
      }).valid());

  executor.pump({1ms, 1});
  EXPECT_EQ(value, 0);
  executor.pump_completions(arv::sched::JobCompletionTarget::World, 8);
  EXPECT_EQ(value, 0);
  executor.pump_completions(arv::sched::JobCompletionTarget::Main, 8);
  EXPECT_EQ(value, 5);
}

TEST(TaskExecutorTests, JobSystemTaskExecutorDelegatesToNativeJobSystem) {
  auto jobs = make_test_job_system();
  arv::sched::JobSystemTaskExecutor executor{jobs};
  std::atomic<int> value{0};

  const arv::sched::JobHandle handle = executor.submit(
      desc("native_executor", arv::sched::JobLane::BackgroundCpu),
      [&value](const arv::sched::CancellationToken&) {
        value.store(9, std::memory_order_release);
      });

  ASSERT_TRUE(handle.valid());
  jobs.join();
  EXPECT_TRUE(executor.supports_parallelism());
  EXPECT_EQ(value.load(std::memory_order_acquire), 9);
}

TEST(JobSystemTests, SpscQueueMovesValuesWithoutBlocking) {
  arv::sched::SpscQueue<int> queue(4);

  EXPECT_TRUE(queue.try_push(11));
  EXPECT_TRUE(queue.try_push(17));
  EXPECT_GE(queue.size_approx(), 1U);

  int value = 0;
  ASSERT_TRUE(queue.try_pop(value));
  EXPECT_EQ(value, 11);
  ASSERT_TRUE(queue.try_pop(value));
  EXPECT_EQ(value, 17);
  EXPECT_FALSE(queue.try_pop(value));
}

TEST(JobSystemTests, WorkerThreadPolicyClampsConfiguredCount) {
  arv::sched::WorkerThreadConfig config;
  config.worker_thread_count = 99;
  config.min_worker_thread_count = 2;
  config.max_worker_thread_count = 8;

  EXPECT_EQ(arv::sched::resolve_worker_thread_count(config), 8U);
}

TEST(JobSystemTests, WorkerThreadPolicyReservesEngineThreadsForAutomaticCount) {
  arv::sched::WorkerThreadConfig config;
  config.worker_thread_count = 0;
  config.reserved_thread_count = arv::sched::hardware_thread_count_or_one() + 10U;
  config.min_worker_thread_count = 1;
  config.max_worker_thread_count = 64;

  EXPECT_EQ(arv::sched::resolve_worker_thread_count(config), 1U);
}

TEST(JobSystemTests, RuntimeThreadBudgetUsesGenericReservationAndSplitsPools) {
  arv::sched::RuntimeThreadBudgetConfig config;
  config.workers.worker_thread_count = 6;
  config.workers.reserved_thread_count = 1;
  config.workers.min_worker_thread_count = 1;
  config.workers.max_worker_thread_count = 16;
  config.split_primary_secondary_pools = true;

  const arv::sched::RuntimeThreadBudgetPlan plan =
      arv::sched::plan_runtime_thread_budget(config);

  EXPECT_EQ(plan.reserved_thread_count, 1U);
  EXPECT_EQ(plan.total_worker_thread_count, 6U);
  EXPECT_EQ(
      plan.primary_worker_thread_count + plan.secondary_worker_thread_count,
      plan.total_worker_thread_count);
  EXPECT_EQ(plan.primary_workers.worker_thread_count, plan.primary_worker_thread_count);
  EXPECT_EQ(plan.secondary_workers.worker_thread_count, plan.secondary_worker_thread_count);
}

TEST(JobSystemTests, RuntimeThreadBudgetKeepsSplitPoolsRunnableWhenAutoResolvesLow) {
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

TEST(OwnerThreadTests, RenderNetworkAndWorldThreadsAreDistinctFromMain) {
  const std::thread::id main_thread = std::this_thread::get_id();
  arv::sched::RenderThread render;
  arv::sched::NetworkThread network;
  arv::sched::WorldThread world;

  ASSERT_TRUE(render.start());
  ASSERT_TRUE(network.start());
  ASSERT_TRUE(world.start());

  const std::thread::id render_thread = render.call([] {
    return std::this_thread::get_id();
  });
  const std::thread::id network_thread = network.call([] {
    return std::this_thread::get_id();
  });
  const std::thread::id world_thread = world.call([] {
    return std::this_thread::get_id();
  });

  EXPECT_NE(render_thread, main_thread);
  EXPECT_NE(network_thread, main_thread);
  EXPECT_NE(world_thread, main_thread);
  EXPECT_NE(render_thread, network_thread);
  EXPECT_NE(render_thread, world_thread);
  EXPECT_NE(network_thread, world_thread);
}

TEST(OwnerThreadTests, CommandsRunInSubmissionOrderOnOwner) {
  arv::sched::WorldThread world;
  ASSERT_TRUE(world.start());
  std::vector<int> values;

  ASSERT_TRUE(world.post([&values] {
    values.push_back(1);
  }));
  ASSERT_TRUE(world.post([&values] {
    values.push_back(2);
  }));
  world.call([&values] {
    values.push_back(3);
  });

  ASSERT_EQ(values.size(), 3U);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
}

TEST(OwnerThreadTests, ConcurrentProducersPostHighVolumeCommands) {
  arv::sched::WorldThread world;
  ASSERT_TRUE(world.start());

  constexpr int ProducerCount = 4;
  constexpr int CommandsPerProducer = 1000;
  constexpr int ExpectedCommands = ProducerCount * CommandsPerProducer;
  std::atomic<int> ran{0};
  std::atomic<int> rejected{0};
  std::vector<std::thread> producers;

  for (int producer = 0; producer < ProducerCount; ++producer) {
    producers.emplace_back([&world, &ran, &rejected] {
      for (int command = 0; command < CommandsPerProducer; ++command) {
        if (!world.post([&ran] {
              ran.fetch_add(1, std::memory_order_acq_rel);
            })) {
          rejected.fetch_add(1, std::memory_order_acq_rel);
        }
      }
    });
  }

  for (std::thread& producer : producers) {
    producer.join();
  }

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (ran.load(std::memory_order_acquire) != ExpectedCommands &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }

  EXPECT_EQ(rejected.load(std::memory_order_acquire), 0);
  EXPECT_EQ(ran.load(std::memory_order_acquire), ExpectedCommands);
}

TEST(OwnerThreadTests, RequestStopRejectsNewCommandsAndDrainsAcceptedCommands) {
  arv::sched::WorldThread world;
  ASSERT_TRUE(world.start());

  auto release = std::make_shared<std::promise<void>>();
  std::shared_future<void> release_future = release->get_future().share();
  std::atomic<int> ran{0};

  ASSERT_TRUE(world.post([release_future, &ran] {
    release_future.wait();
    ran.fetch_add(1, std::memory_order_acq_rel);
  }));
  for (int command = 0; command < 8; ++command) {
    ASSERT_TRUE(world.post([&ran] {
      ran.fetch_add(1, std::memory_order_acq_rel);
    }));
  }

  world.request_stop();
  EXPECT_FALSE(world.post([] {}));

  release->set_value();
  world.join();

  EXPECT_EQ(ran.load(std::memory_order_acquire), 9);
  EXPECT_FALSE(world.running());
  EXPECT_TRUE(world.stop_requested());
  EXPECT_EQ(world.owner_thread_hash(), 0U);
}
