#include "arv/client/chunk_interest.hpp"

#include <gtest/gtest.h>

TEST(ClientChunkInterestTests, UpdatesOnlyWhenChunkOrSettingsChange) {
  arv::client::ClientChunkInterest interest;
  interest.set_world_id(arv::WorldID{1});
  interest.set_view_distance(3);
  interest.update_player_position({0.5F, 1.0F, -0.5F});

  EXPECT_TRUE(interest.consume_interest_changed());
  arv::protocol::ChunkInterestUpdate update = interest.build_interest_update();
  EXPECT_EQ(update.world_id, (arv::WorldID{1}));
  EXPECT_EQ(update.center_chunk, (arv::ChunkSectionCoord{{0, 0, -1}}));
  EXPECT_EQ(update.client_view_distance, 3);
  EXPECT_FALSE(update.previous_center_chunk.has_value());
  EXPECT_FALSE(interest.consume_interest_changed());

  interest.update_player_position({15.9F, 1.0F, -0.5F});
  EXPECT_FALSE(interest.consume_interest_changed());

  interest.update_player_position({16.0F, 1.0F, -0.5F});
  EXPECT_TRUE(interest.consume_interest_changed());
  update = interest.build_interest_update();
  EXPECT_EQ(update.center_chunk, (arv::ChunkSectionCoord{{1, 0, -1}}));
  ASSERT_TRUE(update.previous_center_chunk.has_value());
  EXPECT_EQ(update.previous_center_chunk.value(), (arv::ChunkSectionCoord{{0, 0, -1}}));
}

TEST(ClientChunkInterestTests, ViewDistanceChangeMarksInterestDirty) {
  arv::client::ClientChunkInterest interest;
  interest.set_world_id(arv::WorldID{1});
  interest.update_player_position({0.0F, 0.0F, 0.0F});
  ASSERT_TRUE(interest.consume_interest_changed());

  interest.set_view_distance(4);
  EXPECT_TRUE(interest.consume_interest_changed());
  EXPECT_EQ(interest.build_interest_update().client_view_distance, 4);
}
