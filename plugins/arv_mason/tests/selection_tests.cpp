#include "arv_mason/selection/selection.hpp"

#include <gtest/gtest.h>

namespace {

TEST(MasonSelectionTests, NormalizesInclusiveBoundsAndCountsBlocks) {
  arv::PlayerSelection selection;
  selection.has_world = true;
  selection.world = 7;
  selection.a = ArvBlockPos{4, 9, -2};
  selection.b = ArvBlockPos{2, 6, 1};

  const arv_mason::SelectionBoundsResult result =
      arv_mason::normalized_bounds(selection);

  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.bounds.world, 7U);
  EXPECT_EQ(result.bounds.min.x, 2);
  EXPECT_EQ(result.bounds.min.y, 6);
  EXPECT_EQ(result.bounds.min.z, -2);
  EXPECT_EQ(result.bounds.max.x, 4);
  EXPECT_EQ(result.bounds.max.y, 9);
  EXPECT_EQ(result.bounds.max.z, 1);
  EXPECT_EQ(result.bounds.size_x, 3U);
  EXPECT_EQ(result.bounds.size_y, 4U);
  EXPECT_EQ(result.bounds.size_z, 4U);
  EXPECT_EQ(result.bounds.block_count, 48U);
}

TEST(MasonSelectionTests, RejectsIncompleteSelection) {
  arv::PlayerSelection selection;
  selection.has_world = true;
  selection.world = 1;
  selection.a = ArvBlockPos{0, 0, 0};

  const arv_mason::SelectionBoundsResult result =
      arv_mason::normalized_bounds(selection);

  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.message.find("incomplete"), std::string::npos);
}

TEST(MasonSelectionTests, SingleBlockSelectionCountsAsOne) {
  arv::PlayerSelection selection;
  selection.has_world = true;
  selection.world = 1;
  selection.a = ArvBlockPos{-3, 8, 11};
  selection.b = ArvBlockPos{-3, 8, 11};

  const arv_mason::SelectionBoundsResult result =
      arv_mason::normalized_bounds(selection);

  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.bounds.block_count, 1U);
  EXPECT_EQ(arv_mason::format_pos(result.bounds.min), "-3, 8, 11");
}

} // namespace
