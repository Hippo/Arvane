#include "arv/protocol/chunk_streaming.hpp"
#include "arv/server/chunk_streamer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace {

void confirm_chunk_messages(
    arv::server::ServerChunkStreamer& streamer,
    const std::vector<arv::server::QueuedChunkStreamMessage>& messages) {
  for (const arv::server::QueuedChunkStreamMessage& message : messages) {
    if (!message.tracks_sent_chunk) {
      continue;
    }
    ASSERT_TRUE(streamer.confirm_chunk_sent(
        message.session_id,
        message.world_id,
        message.coord,
        message.block_version,
        message.light_version,
        message.lighting_valid));
  }
}

} // namespace

TEST(ServerChunkStreamerTests, UnloadMarginRetainsRecentlySentChunks) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  (void)world.ensure_chunk({{1, 0, 0}});
  (void)world.ensure_chunk({{2, 0, 0}});

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({.max_view_distance = 0,
                      .unload_margin = 1,
                      .max_chunks_sent_per_player_tick = 8,
                      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ServerboundChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 0;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message_type, arv::protocol::MessageType::ClientboundChunkSectionData);
  confirm_chunk_messages(streamer, messages);
  messages.clear();

  streamer.update_player_interest(session, {{1, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message_type, arv::protocol::MessageType::ClientboundChunkSectionData);
  const auto retained_step_data =
      arv::protocol::deserialize_clientbound_chunk_section_data(messages.front().payload);
  ASSERT_TRUE(retained_step_data);
  EXPECT_EQ(retained_step_data.value().coord, (arv::ChunkSectionCoord{{1, 0, 0}}));
  confirm_chunk_messages(streamer, messages);
  messages.clear();

  streamer.update_player_interest(session, {{2, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 2U);
  EXPECT_EQ(messages[0].message_type, arv::protocol::MessageType::ClientboundChunkSectionUnload);
  const auto unload = arv::protocol::deserialize_clientbound_chunk_section_unload(messages[0].payload);
  ASSERT_TRUE(unload);
  EXPECT_EQ(unload.value().coord, (arv::ChunkSectionCoord{{0, 0, 0}}));
  EXPECT_EQ(messages[1].message_type, arv::protocol::MessageType::ClientboundChunkSectionData);
}

TEST(ServerChunkStreamerTests, LightDependencyVersionDoesNotResendUnchangedNeighborChunk) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& target = world.ensure_chunk({{0, 0, 0}});
  arv::voxel::ChunkSection& lit_chunk = world.ensure_chunk({{1, 0, 0}});

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({.max_view_distance = 1,
                      .unload_margin = 1,
                      .max_chunks_sent_per_player_tick = 32,
                      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ServerboundChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 1;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 2U);
  confirm_chunk_messages(streamer, messages);
  messages.clear();

  std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume> lights = lit_chunk.lights();
  lights[arv::voxel::local_index({{0, 4, 5}})] = arv::voxel::pack_light(7, 6, 5, 4);
  const arv::u8 negative_x_boundary =
      static_cast<arv::u8>(1U << arv::voxel::face_index(arv::voxel::BlockFace::NegativeX));
  arv::world::LightingResult result;
  result.world_id = world.id();
  result.chunks.push_back({{{1, 0, 0}}, lit_chunk.block_version(), lights, 0U, negative_x_boundary});
  const arv::world::LightingPublishStats stats = world.publish_lighting_result(std::move(result));
  ASSERT_EQ(stats.changed_chunks, 1U);
  ASSERT_TRUE(target.is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));

  streamer.tick_streaming_budget(world, messages);

  ASSERT_EQ(messages.size(), 1U);
  const auto data = arv::protocol::deserialize_clientbound_chunk_section_data(messages.front().payload);
  ASSERT_TRUE(data);
  EXPECT_EQ(data.value().coord, (arv::ChunkSectionCoord{{1, 0, 0}}));
}

TEST(ServerChunkStreamerTests, SendsLightingWhenPreviouslyDeferredLightingBecomesValid) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(chunk.set_light({{4, 5, 6}}, arv::voxel::pack_light(1, 2, 3, 4)));
  world.mark_chunk_lighting_dirty({{0, 0, 0}});

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({.max_view_distance = 0,
                      .unload_margin = 1,
                      .max_chunks_sent_per_player_tick = 8,
                      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ServerboundChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 0;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  confirm_chunk_messages(streamer, messages);
  auto data = arv::protocol::deserialize_clientbound_chunk_section_data(messages.front().payload);
  ASSERT_TRUE(data);
  EXPECT_TRUE(data.value().has_lighting);
  EXPECT_FALSE(data.value().lighting_valid);
  EXPECT_EQ(
      data.value().lights[arv::voxel::local_index({{4, 5, 6}})],
      arv::voxel::pack_light(1, 2, 3, 4));
  messages.clear();

  arv::world::LightingResult result;
  result.world_id = world.id();
  result.chunks.push_back(
      {{{0, 0, 0}}, chunk.block_version(), chunk.lights(), chunk.content_version(), 0U});
  const arv::world::LightingPublishStats stats =
      world.publish_lighting_result(std::move(result));
  ASSERT_EQ(stats.accepted_chunks, 1U);
  ASSERT_FALSE(chunk.is_dirty(arv::voxel::ChunkDirtyFlags::Lighting));

  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  data = arv::protocol::deserialize_clientbound_chunk_section_data(messages.front().payload);
  ASSERT_TRUE(data);
  EXPECT_TRUE(data.value().has_lighting);
  EXPECT_TRUE(data.value().lighting_valid);
}

TEST(ServerChunkStreamerTests, BlockUpdateAcknowledgementStillRequiresValidLightingConfirmation) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({.max_view_distance = 0,
                      .unload_margin = 1,
                      .max_chunks_sent_per_player_tick = 8,
                      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ServerboundChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 0;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  confirm_chunk_messages(streamer, messages);
  messages.clear();

  ASSERT_TRUE(world.set_block({{1, 2, 3}}, arv::voxel::BlockVariantID{7}));
  ASSERT_TRUE(streamer.mark_sent_block_version(
      session.session_id,
      session.world_id,
      {{0, 0, 0}},
      chunk.block_version()));

  arv::world::LightingResult result;
  result.world_id = world.id();
  result.chunks.push_back(
      {{{0, 0, 0}}, chunk.block_version(), chunk.lights(), chunk.content_version(), 0U});
  const arv::world::LightingPublishStats stats =
      world.publish_lighting_result(std::move(result));
  ASSERT_EQ(stats.accepted_chunks, 1U);

  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  const auto data = arv::protocol::deserialize_clientbound_chunk_section_data(messages.front().payload);
  ASSERT_TRUE(data);
  EXPECT_TRUE(data.value().has_lighting);
  EXPECT_TRUE(data.value().lighting_valid);
}

TEST(ServerChunkStreamerTests, PendingChunkIsRetriedUntilSendConfirmed) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({.max_view_distance = 0,
                      .unload_margin = 1,
                      .max_chunks_sent_per_player_tick = 8,
                      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ServerboundChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 0;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, interest);
  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_TRUE(messages.front().tracks_sent_chunk);
  messages.clear();

  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  const auto data = arv::protocol::deserialize_clientbound_chunk_section_data(messages.front().payload);
  ASSERT_TRUE(data);
  EXPECT_EQ(data.value().coord, (arv::ChunkSectionCoord{{0, 0, 0}}));
  confirm_chunk_messages(streamer, messages);
  messages.clear();

  streamer.tick_streaming_budget(world, messages);
  EXPECT_TRUE(messages.empty());
}

TEST(ServerChunkStreamerTests, MissingChunkQueuesGenerationRequestWithoutSynchronousSend) {
  arv::world::WorldRuntime world{arv::WorldID{1}};

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({.max_view_distance = 0,
                      .unload_margin = 1,
                      .max_chunks_sent_per_player_tick = 8,
                      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ServerboundChunkInterestUpdate interest;
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
  EXPECT_EQ(generation_requests.front(), (arv::ChunkCoord{0, 0}));

  arv::world::GeneratedChunk generated;
  generated.world_id = world.id();
  generated.coord = {0, 0};
  arv::world::GeneratedChunkSection section;
  section.coord = {{0, 0, 0}};
  section.blocks.fill(arv::voxel::AirBlockVariantID);
  generated.sections.emplace_back(std::move(section));
  ASSERT_TRUE(world.publish_generated_chunk(std::move(generated)));

  streamer.tick_streaming_budget(world, messages);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message_type, arv::protocol::MessageType::ClientboundChunkSectionData);
}

TEST(ServerChunkStreamerTests, StreamingRequestsFullConfiguredColumnHeightButOneGenerationColumn) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({-1, 1});

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({.max_view_distance = 0,
                      .unload_margin = 1,
                      .max_chunks_sent_per_player_tick = 8,
                      .max_unloads_sent_per_player_tick = 8});

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = world.id();

  arv::protocol::ServerboundChunkInterestUpdate interest;
  interest.world_id = world.id();
  interest.client_view_distance = 0;

  std::vector<arv::server::QueuedChunkStreamMessage> messages;
  streamer.update_player_interest(session, {{0, 0, 0}}, world.height_settings(), interest);
  streamer.tick_streaming_budget(world, messages);

  EXPECT_TRUE(messages.empty());
  for (arv::i32 y = -1; y <= 1; ++y) {
    const arv::voxel::ChunkSection* requested = world.find_chunk({{0, y, 0}});
    ASSERT_NE(requested, nullptr);
    EXPECT_EQ(requested->lifecycle_state(), arv::voxel::ChunkLifecycleState::Requested);
  }
  const std::vector<arv::ChunkCoord> generation_requests =
      streamer.pending_generation_requests(world);
  ASSERT_EQ(generation_requests.size(), 1U);
  EXPECT_EQ(generation_requests.front(), (arv::ChunkCoord{0, 0}));
}
