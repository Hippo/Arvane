#include "arv/player/selection.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(PlayerSelectionTests, NormalizesBoundsAndCountsInclusiveBlocks) {
  arv::player::PlayerSelection selection;
  selection.world = arv::WorldID{3};
  selection.a = arv::BlockPos{{4, -2, 10}};
  selection.b = arv::BlockPos{{2, 1, 8}};

  const arv::player::SelectionBoundsResult result =
      arv::player::normalized_bounds(selection);

  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.bounds.world, arv::WorldID{3});
  EXPECT_EQ(result.bounds.min, (arv::BlockPos{{2, -2, 8}}));
  EXPECT_EQ(result.bounds.max, (arv::BlockPos{{4, 1, 10}}));
  EXPECT_EQ(result.bounds.size_x, 3U);
  EXPECT_EQ(result.bounds.size_y, 4U);
  EXPECT_EQ(result.bounds.size_z, 3U);
  EXPECT_EQ(result.bounds.block_count, 36U);
  EXPECT_EQ(arv::player::format_pos(result.bounds.min), "2, -2, 8");
}

TEST(PlayerSelectionTests, RejectsIncompleteSelection) {
  arv::player::PlayerSelection selection;
  selection.world = arv::WorldID{1};
  selection.a = arv::BlockPos{{0, 0, 0}};

  const arv::player::SelectionBoundsResult result =
      arv::player::normalized_bounds(selection);

  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.message.find("/select b"), std::string::npos);
}

TEST(PlayerSelectionTests, SingleBlockSelectionCountsAsOne) {
  arv::player::PlayerSelection selection;
  selection.world = arv::WorldID{2};
  selection.a = arv::BlockPos{{-3, 8, 11}};
  selection.b = arv::BlockPos{{-3, 8, 11}};

  const arv::player::SelectionBoundsResult result =
      arv::player::normalized_bounds(selection);

  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.bounds.block_count, 1U);
  EXPECT_EQ(
      arv::player::format_bounds(result.bounds),
      "world 2 | AABB min -3, 8, 11 max -3, 8, 11 | 1 x 1 x 1 = 1 blocks");
}
