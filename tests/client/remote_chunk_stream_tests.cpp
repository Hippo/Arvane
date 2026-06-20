#include "arv/client/remote_chunk_stream.hpp"

#include "arv/voxel/storage.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

[[nodiscard]] arv::protocol::ClientboundChunkSectionData make_section(
    arv::WorldID world_id,
    arv::ChunkSectionCoord coord,
    arv::voxel::BlockVariantID marker_block =
        arv::voxel::BlockVariantID{1}) {
  arv::protocol::ClientboundChunkSectionData section;
  section.world_id = world_id;
  section.coord = coord;
  section.chunk_version = 10;
  section.chunk_content_version = 11;
  section.chunk_light_version = 12;
  section.has_lighting = true;
  section.lighting_valid = true;
  section.blocks.fill(arv::voxel::AirBlockVariantID);
  section.lights.fill(arv::voxel::pack_light(1, 2, 3, 4));
  section.fluids.fill(arv::voxel::empty_fluid_cell());
  section.blocks.front() = marker_block;
  return section;
}

[[nodiscard]] arv::protocol::BlockUpdate make_update(
    arv::i32 x,
    arv::u64 server_tick = 1U) {
  return {
      .pos = arv::BlockPos{{x, 0, 0}},
      .new_state = arv::voxel::BlockVariantID{3},
      .update_flags = 0U,
      .server_tick = server_tick,
      .has_fluid = false,
      .new_fluid = arv::voxel::empty_fluid_cell(),
  };
}

} // namespace

TEST(RemoteChunkStreamTests, CountsChunkStreamPacketBundleContents) {
  const arv::client::ClientChunkStreamPacketCounts empty_counts =
      arv::client::count_client_chunk_stream_packets({});
  EXPECT_EQ(empty_counts.unloads, 0U);
  EXPECT_EQ(empty_counts.sections, 0U);
  EXPECT_EQ(empty_counts.block_update_batches, 0U);
  EXPECT_EQ(empty_counts.block_updates, 0U);

  arv::client::ClientChunkStreamPackets packets;
  packets.unloads.push_back({.world_id = arv::WorldID{1}, .coord = {{0, 0, 0}}});
  packets.unloads.push_back({.world_id = arv::WorldID{1}, .coord = {{1, 0, 0}}});
  packets.sections.push_back(
      make_section(arv::WorldID{1}, {{2, 0, 0}}, arv::voxel::BlockVariantID{4}));

  arv::protocol::ClientboundBlockUpdateBatch first_batch;
  first_batch.world_id = arv::WorldID{1};
  first_batch.updates.push_back(make_update(0));
  first_batch.updates.push_back(make_update(1));
  packets.block_updates.push_back(first_batch);

  arv::protocol::ClientboundBlockUpdateBatch second_batch;
  second_batch.world_id = arv::WorldID{1};
  second_batch.updates.push_back(make_update(2));
  packets.block_updates.push_back(second_batch);

  const arv::client::ClientChunkStreamPacketCounts counts =
      arv::client::count_client_chunk_stream_packets(packets);

  EXPECT_EQ(counts.unloads, 2U);
  EXPECT_EQ(counts.sections, 1U);
  EXPECT_EQ(counts.block_update_batches, 2U);
  EXPECT_EQ(counts.block_updates, 3U);
}

TEST(RemoteChunkStreamTests, VisitsChunkStreamPacketsReadOnlyInSinkOrder) {
  arv::client::ClientChunkStreamPackets packets;
  packets.unloads.push_back({.world_id = arv::WorldID{1}, .coord = {{0, 0, 0}}});
  packets.sections.push_back(
      make_section(arv::WorldID{1}, {{2, 0, 0}}, arv::voxel::BlockVariantID{4}));
  arv::protocol::ClientboundBlockUpdateBatch batch;
  batch.world_id = arv::WorldID{1};
  batch.updates.push_back(make_update(2, 9U));
  packets.block_updates.push_back(batch);

  std::vector<int> order;
  struct ReadOnlySink {
    std::vector<int>& order;

    void operator()(const arv::protocol::ClientboundChunkSectionUnload& unload)
        const {
      order.push_back(0);
      EXPECT_EQ(unload.coord, (arv::ChunkSectionCoord{{0, 0, 0}}));
    }

    void operator()(const arv::protocol::ClientboundChunkSectionData& section)
        const {
      order.push_back(1);
      EXPECT_EQ(section.coord, (arv::ChunkSectionCoord{{2, 0, 0}}));
    }

    void operator()(const arv::protocol::ClientboundBlockUpdateBatch& batch)
        const {
      order.push_back(2);
      ASSERT_EQ(batch.updates.size(), 1U);
      EXPECT_EQ(batch.updates.front().server_tick, 9U);
    }
  };

  const arv::client::ClientChunkStreamPackets& const_packets = packets;
  arv::client::visit_client_chunk_stream_packets(
      const_packets,
      ReadOnlySink{.order = order});

  ASSERT_EQ(order.size(), 3U);
  EXPECT_EQ(order[0], 0);
  EXPECT_EQ(order[1], 1);
  EXPECT_EQ(order[2], 2);
  EXPECT_EQ(packets.sections.size(), 1U);
  EXPECT_EQ(packets.block_updates.size(), 1U);
}

TEST(RemoteChunkStreamTests, ConsumesChunkStreamPacketsInSinkOrder) {
  arv::client::ClientChunkStreamPackets packets;
  packets.unloads.push_back({.world_id = arv::WorldID{1}, .coord = {{0, 0, 0}}});
  packets.sections.push_back(
      make_section(arv::WorldID{1}, {{2, 0, 0}}, arv::voxel::BlockVariantID{4}));
  arv::protocol::ClientboundBlockUpdateBatch batch;
  batch.world_id = arv::WorldID{1};
  batch.updates.push_back(make_update(2, 9U));
  packets.block_updates.push_back(batch);

  std::vector<int> order;
  std::vector<arv::protocol::ClientboundChunkSectionUnload> unloads;
  std::vector<arv::protocol::ClientboundChunkSectionData> sections;
  std::vector<arv::protocol::ClientboundBlockUpdateBatch> batches;

  struct RecordingSink {
    std::vector<int>& order;
    std::vector<arv::protocol::ClientboundChunkSectionUnload>& unloads;
    std::vector<arv::protocol::ClientboundChunkSectionData>& sections;
    std::vector<arv::protocol::ClientboundBlockUpdateBatch>& batches;

    void operator()(const arv::protocol::ClientboundChunkSectionUnload& unload)
        const {
      order.push_back(0);
      unloads.push_back(unload);
    }

    void operator()(arv::protocol::ClientboundChunkSectionData&& section)
        const {
      order.push_back(1);
      sections.push_back(std::move(section));
    }

    void operator()(arv::protocol::ClientboundBlockUpdateBatch&& batch) const {
      order.push_back(2);
      batches.push_back(std::move(batch));
    }
  };

  arv::client::consume_client_chunk_stream_packets(
      packets,
      RecordingSink{
          .order = order,
          .unloads = unloads,
          .sections = sections,
          .batches = batches,
      });

  ASSERT_EQ(order.size(), 3U);
  EXPECT_EQ(order[0], 0);
  EXPECT_EQ(order[1], 1);
  EXPECT_EQ(order[2], 2);
  ASSERT_EQ(unloads.size(), 1U);
  EXPECT_EQ(unloads.front().coord, (arv::ChunkSectionCoord{{0, 0, 0}}));
  ASSERT_EQ(sections.size(), 1U);
  EXPECT_EQ(sections.front().coord, (arv::ChunkSectionCoord{{2, 0, 0}}));
  ASSERT_EQ(batches.size(), 1U);
  ASSERT_EQ(batches.front().updates.size(), 1U);
  EXPECT_EQ(batches.front().updates.front().server_tick, 9U);
}

TEST(RemoteChunkStreamTests, SectionDataUpsertsActiveWorldSections) {
  arv::client::RemoteChunkSectionStore store;
  constexpr arv::WorldID active_world{1};
  const arv::ChunkSectionCoord coord{{2, 0, -3}};

  EXPECT_FALSE(store.apply_section_data(
      active_world,
      make_section(arv::WorldID{2}, coord, arv::voxel::BlockVariantID{7})));
  EXPECT_TRUE(store.sections().empty());

  EXPECT_TRUE(store.apply_section_data(
      active_world,
      make_section(active_world, coord, arv::voxel::BlockVariantID{7})));
  ASSERT_EQ(store.sections().size(), 1U);
  EXPECT_EQ(store.sections().front().blocks.front(), arv::voxel::BlockVariantID{7});

  EXPECT_TRUE(store.apply_section_data(
      active_world,
      make_section(active_world, coord, arv::voxel::BlockVariantID{9})));
  ASSERT_EQ(store.sections().size(), 1U);
  EXPECT_EQ(store.sections().front().blocks.front(), arv::voxel::BlockVariantID{9});
}

TEST(RemoteChunkStreamTests, FindsAndUnloadsActiveWorldSections) {
  arv::client::RemoteChunkSectionStore store;
  constexpr arv::WorldID active_world{1};
  const arv::ChunkSectionCoord coord{{0, 0, 0}};
  const arv::ChunkSectionCoord neighbor{{1, 0, 0}};

  EXPECT_TRUE(store.apply_section_data(active_world, make_section(active_world, coord)));
  EXPECT_TRUE(store.apply_section_data(active_world, make_section(active_world, neighbor)));

  ASSERT_NE(store.find_section(active_world, coord), nullptr);
  EXPECT_EQ(store.find_section(arv::WorldID{2}, coord), nullptr);
  const arv::client::RemoteChunkSectionIndex index =
      store.build_section_index(active_world);
  ASSERT_EQ(index.size(), 2U);
  ASSERT_NE(index.find(coord), index.end());
  EXPECT_EQ(index.find(coord)->second->coord, coord);
  EXPECT_TRUE(store.build_section_index(arv::WorldID{2}).empty());

  EXPECT_FALSE(store.apply_section_unload(
      active_world,
      {.world_id = arv::WorldID{2}, .coord = coord}));
  EXPECT_EQ(store.sections().size(), 2U);

  EXPECT_TRUE(store.apply_section_unload(
      active_world,
      {.world_id = active_world, .coord = coord}));
  EXPECT_EQ(store.find_section(active_world, coord), nullptr);
  ASSERT_EQ(store.sections().size(), 1U);
  EXPECT_EQ(store.sections().front().coord, neighbor);
}

TEST(RemoteChunkStreamTests, BlockUpdatesMutateLoadedSectionsAndBumpVersions) {
  arv::client::RemoteChunkSectionStore store;
  constexpr arv::WorldID active_world{1};
  const arv::ChunkSectionCoord coord{{0, 0, 0}};
  arv::protocol::ClientboundChunkSectionData section =
      make_section(active_world, coord);
  section.chunk_version = 10;
  section.chunk_content_version = 11;
  ASSERT_TRUE(store.apply_section_data(active_world, section));

  const arv::SectionLocalPos local{{1, 2, 3}};
  arv::protocol::ClientboundBlockUpdateBatch batch;
  batch.world_id = active_world;
  batch.server_tick = 200;
  batch.updates.push_back({
      .pos = arv::BlockPos{{1, 2, 3}},
      .new_state = arv::voxel::BlockVariantID{8},
      .update_flags = 0U,
      .server_tick = 200U,
      .has_fluid = true,
      .new_fluid = arv::voxel::make_fluid_source(
          arv::voxel::make_water_fluid_definition()),
  });

  ASSERT_TRUE(store.apply_block_update_batch(active_world, batch));
  const arv::protocol::ClientboundChunkSectionData* updated =
      store.find_section(active_world, coord);
  ASSERT_NE(updated, nullptr);
  const arv::usize index = arv::voxel::local_index(local);
  EXPECT_EQ(updated->blocks[index], arv::voxel::BlockVariantID{8});
  EXPECT_TRUE(updated->has_fluids);
  EXPECT_TRUE(updated->fluids[index].source());
  EXPECT_EQ(updated->chunk_content_version, 200U);
  EXPECT_EQ(updated->chunk_version, 200U);
}

TEST(RemoteChunkStreamTests, BlockUpdatesIgnoreWrongWorldAndMissingSections) {
  arv::client::RemoteChunkSectionStore store;
  constexpr arv::WorldID active_world{1};
  ASSERT_TRUE(store.apply_section_data(
      active_world,
      make_section(active_world, {{0, 0, 0}}, arv::voxel::BlockVariantID{3})));

  arv::protocol::ClientboundBlockUpdateBatch wrong_world;
  wrong_world.world_id = arv::WorldID{2};
  wrong_world.server_tick = 99;
  wrong_world.updates.push_back({
      .pos = arv::BlockPos{{0, 0, 0}},
      .new_state = arv::voxel::BlockVariantID{8},
      .update_flags = 0U,
      .server_tick = 99U,
      .has_fluid = false,
      .new_fluid = arv::voxel::empty_fluid_cell(),
  });
  EXPECT_FALSE(store.apply_block_update_batch(active_world, wrong_world));

  arv::protocol::ClientboundBlockUpdateBatch missing_section = wrong_world;
  missing_section.world_id = active_world;
  missing_section.updates.front().pos = arv::BlockPos{{16, 0, 0}};
  EXPECT_FALSE(store.apply_block_update_batch(active_world, missing_section));

  const arv::protocol::ClientboundChunkSectionData* section =
      store.find_section(active_world, {{0, 0, 0}});
  ASSERT_NE(section, nullptr);
  EXPECT_EQ(section->blocks.front(), arv::voxel::BlockVariantID{3});
  EXPECT_EQ(section->chunk_content_version, 11U);
}

TEST(RemoteChunkStreamTests, AppliesChunkStreamPacketBundleToRemoteStore) {
  arv::client::RemoteChunkSectionStore store;
  constexpr arv::WorldID active_world{1};
  const arv::ChunkSectionCoord unloaded_coord{{-1, 0, 0}};
  const arv::ChunkSectionCoord loaded_coord{{0, 0, 0}};
  ASSERT_TRUE(store.apply_section_data(
      active_world,
      make_section(active_world, unloaded_coord, arv::voxel::BlockVariantID{4})));

  arv::client::ClientChunkStreamPackets packets;
  packets.unloads.push_back({.world_id = arv::WorldID{2}, .coord = unloaded_coord});
  packets.unloads.push_back({.world_id = active_world, .coord = unloaded_coord});
  packets.sections.push_back(
      make_section(active_world, loaded_coord, arv::voxel::BlockVariantID{7}));
  packets.sections.push_back(
      make_section(arv::WorldID{2}, {{4, 0, 0}}, arv::voxel::BlockVariantID{8}));

  arv::protocol::ClientboundBlockUpdateBatch wrong_world;
  wrong_world.world_id = arv::WorldID{2};
  wrong_world.updates.push_back({
      .pos = arv::BlockPos{{1, 0, 0}},
      .new_state = arv::voxel::BlockVariantID{9},
      .update_flags = 0U,
      .server_tick = 50U,
      .has_fluid = false,
      .new_fluid = arv::voxel::empty_fluid_cell(),
  });
  packets.block_updates.push_back(wrong_world);

  arv::protocol::ClientboundBlockUpdateBatch active_update;
  active_update.world_id = active_world;
  active_update.updates.push_back({
      .pos = arv::BlockPos{{1, 0, 0}},
      .new_state = arv::voxel::BlockVariantID{9},
      .update_flags = 0U,
      .server_tick = 50U,
      .has_fluid = false,
      .new_fluid = arv::voxel::empty_fluid_cell(),
  });
  packets.block_updates.push_back(active_update);

  const arv::client::RemoteChunkStreamPacketApplyResult result =
      arv::client::apply_remote_chunk_stream_packets(
          store,
          active_world,
          std::move(packets));

  EXPECT_TRUE(result.changed);
  EXPECT_EQ(result.unloaded_sections, 1U);
  EXPECT_EQ(result.loaded_sections, 1U);
  EXPECT_EQ(result.block_update_batches, 1U);
  EXPECT_EQ(store.find_section(active_world, unloaded_coord), nullptr);
  const arv::protocol::ClientboundChunkSectionData* section =
      store.find_section(active_world, loaded_coord);
  ASSERT_NE(section, nullptr);
  EXPECT_EQ(
      section->blocks[arv::voxel::local_index(arv::SectionLocalPos{{1, 0, 0}})],
      arv::voxel::BlockVariantID{9});
  EXPECT_EQ(section->chunk_content_version, 50U);
  EXPECT_EQ(section->chunk_version, 50U);
  EXPECT_EQ(store.find_section(arv::WorldID{2}, {{4, 0, 0}}), nullptr);
}
