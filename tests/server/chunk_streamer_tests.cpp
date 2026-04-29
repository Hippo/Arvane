#include "arv/protocol/chunk_streaming.hpp"
#include "arv/server/chunk_streamer.hpp"

#include <gtest/gtest.h>

#include <array>

TEST(ServerChunkStreamerTests, UnloadMarginRetainsRecentlySentChunks) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  (void)world.ensure_chunk({{1, 0, 0}});
  (void)world.ensure_chunk({{2, 0, 0}});

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({
      .max_view_distance = 0,
      .unload_margin = 1,
      .max_chunks_sent_per_player_tick = 8,
      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 0;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message_type, arv::protocol::MessageType::ChunkData);
  messages.clear();

  streamer.update_player_interest(session, {{1, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message_type, arv::protocol::MessageType::ChunkData);
  const auto retained_step_data =
      arv::protocol::deserialize_chunk_data(messages.front().payload);
  ASSERT_TRUE(retained_step_data);
  EXPECT_EQ(retained_step_data.value().coord, (arv::ChunkCoord{{1, 0, 0}}));
  messages.clear();

  streamer.update_player_interest(session, {{2, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 2U);
  EXPECT_EQ(messages[0].message_type, arv::protocol::MessageType::ChunkUnload);
  const auto unload = arv::protocol::deserialize_chunk_unload(messages[0].payload);
  ASSERT_TRUE(unload);
  EXPECT_EQ(unload.value().coord, (arv::ChunkCoord{{0, 0, 0}}));
  EXPECT_EQ(messages[1].message_type, arv::protocol::MessageType::ChunkData);
}

TEST(ServerChunkStreamerTests, LightDependencyVersionDoesNotResendUnchangedNeighborChunk) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& lit_chunk = world.ensure_chunk({{1, 0, 0}});

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({
      .max_view_distance = 1,
      .unload_margin = 1,
      .max_chunks_sent_per_player_tick = 32,
      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 1;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 2U);
  messages.clear();

  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = lit_chunk.lights();
  lights[arv::voxel::local_index({{0, 4, 5}})] = arv::voxel::pack_light(7, 6, 5, 4);
  arv::world::LightingResult result;
  result.world_id = world.id();
  result.chunks.push_back({{{1, 0, 0}}, lit_chunk.block_version(), lights});
  const arv::world::LightingPublishStats stats = world.publish_lighting_result(std::move(result));
  ASSERT_EQ(stats.changed_chunks, 1U);
  ASSERT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));

  streamer.tick_streaming_budget(world, messages);

  ASSERT_EQ(messages.size(), 1U);
  const auto data = arv::protocol::deserialize_chunk_data(messages.front().payload);
  ASSERT_TRUE(data);
  EXPECT_EQ(data.value().coord, (arv::ChunkCoord{{1, 0, 0}}));
}

TEST(ServerChunkStreamerTests, MissingChunkQueuesGenerationRequestWithoutSynchronousSend) {
  arv::world::WorldRuntime world{arv::WorldID{1}};

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({
      .max_view_distance = 0,
      .unload_margin = 1,
      .max_chunks_sent_per_player_tick = 8,
      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 0;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);

  EXPECT_TRUE(messages.empty());
  const arv::voxel::ChunkSection* requested = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(requested, nullptr);
  EXPECT_EQ(
      requested->lifecycle_state(),
      arv::voxel::ChunkLifecycleState::Requested);
  const std::vector<arv::ChunkCoord> generation_requests =
      streamer.pending_generation_requests(world);
  ASSERT_EQ(generation_requests.size(), 1U);
  EXPECT_EQ(generation_requests.front(), (arv::ChunkCoord{{0, 0, 0}}));

  arv::world::GeneratedChunk generated;
  generated.world_id = world.id();
  generated.coord = {{0, 0, 0}};
  generated.blocks.fill(arv::voxel::AirBlockVariantID);
  ASSERT_TRUE(world.publish_generated_chunk(std::move(generated)));

  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message_type, arv::protocol::MessageType::ChunkData);
}
