#include "arv_mason/history/history.hpp"
#include "arv_mason/operations/operation.hpp"

#include <gtest/gtest.h>

namespace {

TEST(MasonOperationTests, BuildsSetPlanOnlyForChangedBlocks) {
  const std::vector<arv_mason::SnapshotBlock> snapshot{
      {{0, 0, 0}, 1},
      {{1, 0, 0}, 2},
      {{2, 0, 0}, 1},
  };

  const arv_mason::OperationPlan plan =
      arv_mason::build_set_plan_from_snapshot(3, snapshot, 1);

  ASSERT_EQ(plan.changes.size(), 1U);
  EXPECT_EQ(plan.changes.front().pos.x, 1);
  EXPECT_EQ(plan.changes.front().before, 2U);
  EXPECT_EQ(plan.changes.front().after, 1U);
}

TEST(MasonOperationTests, BuildsReplacePlanForExactMatches) {
  const std::vector<arv_mason::SnapshotBlock> snapshot{
      {{0, 0, 0}, 4},
      {{1, 0, 0}, 5},
      {{2, 0, 0}, 4},
  };

  const arv_mason::OperationPlan plan =
      arv_mason::build_replace_plan_from_snapshot(3, snapshot, 4, 9);

  ASSERT_EQ(plan.changes.size(), 2U);
  EXPECT_EQ(plan.changes[0].pos.x, 0);
  EXPECT_EQ(plan.changes[1].pos.x, 2);
  EXPECT_EQ(plan.changes[0].after, 9U);
}

TEST(MasonOperationTests, PastePlanCanSkipAir) {
  arv_mason::MasonClipboard clipboard;
  clipboard.size = {2, 1, 1};
  clipboard.source_min = {10, 20, 30};
  clipboard.origin = {10, 20, 30};
  clipboard.blocks = {
      {0, "arvane:air[]"},
      {7, "arvane:stone[]"},
  };
  const std::vector<arv_mason::SnapshotBlock> destination{
      {{5, 6, 7}, 2},
      {{6, 6, 7}, 3},
  };

  const arv_mason::OperationPlan plan =
      arv_mason::build_paste_plan_from_snapshot(1, destination, clipboard, {5, 6, 7}, 0, false);

  ASSERT_EQ(plan.changes.size(), 1U);
  EXPECT_EQ(plan.changes.front().pos.x, 6);
  EXPECT_EQ(plan.changes.front().before, 3U);
  EXPECT_EQ(plan.changes.front().after, 7U);
}

TEST(MasonHistoryTests, PushUndoRedoAndLimit) {
  arv_mason::MasonHistory history;
  history.set_limit(2);
  history.push({1, {{{0, 0, 0}, 1, 2}}, "first"});
  history.push({1, {{{1, 0, 0}, 2, 3}}, "second"});
  history.push({1, {{{2, 0, 0}, 3, 4}}, "third"});

  EXPECT_EQ(history.undo_size(), 2U);
  auto undo = history.take_undo();
  ASSERT_TRUE(undo.has_value());
  EXPECT_EQ(undo->summary, "third");
  history.push_redo(*undo);
  EXPECT_TRUE(history.can_redo());
  history.push({1, {{{3, 0, 0}, 4, 5}}, "fourth"});
  EXPECT_FALSE(history.can_redo());
}

} // namespace
