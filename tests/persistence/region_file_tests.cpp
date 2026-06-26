#include "arv/persistence/region_file.hpp"

#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

namespace {

arv::persistence::SavedChunkSection make_saved_chunk(
    arv::ChunkSectionCoord coord,
    arv::voxel::BlockVariantID fill_variant,
    bool lighting_valid) {
  arv::persistence::SavedChunkSection chunk;
  chunk.coord = coord;
  chunk.blocks.fill(fill_variant);
  chunk.lights.fill(arv::voxel::NoLight);
  chunk.fluids.fill(arv::voxel::empty_fluid_cell());
  chunk.lighting_valid = lighting_valid;
  chunk.lights[0] = arv::voxel::pack_light(1, 2, 3, 4);
  chunk.fluids[0] = arv::voxel::make_fluid_source(arv::voxel::make_water_fluid_definition());
  return chunk;
}

const arv::persistence::SavedChunkSection* find_chunk(
    const std::vector<arv::persistence::SavedChunkSection>& chunks,
    arv::ChunkSectionCoord coord) {
  const auto found = std::find_if(
      chunks.begin(),
      chunks.end(),
      [&](const arv::persistence::SavedChunkSection& chunk) {
        return chunk.coord == coord;
      });
  return found == chunks.end() ? nullptr : &*found;
}

} // namespace

TEST(RegionFileTests, ChunkSerializationRoundTripsThroughRegionEncoding) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID dirt = registry.find_variant("arvane:dirt").value();
  const std::vector<arv::persistence::SavedChunkSection> chunks{
      make_saved_chunk({{0, 0, 0}}, stone, true),
      make_saved_chunk({{1, 0, 0}}, dirt, false)};

  auto encoded = arv::persistence::encode_region_file({0, 0}, chunks, registry, 0);
  ASSERT_TRUE(encoded);
  auto decoded = arv::persistence::decode_region_file(encoded.value(), registry, 0);

  ASSERT_TRUE(decoded);
  ASSERT_EQ(decoded.value().chunks.size(), 2U);
  const arv::persistence::SavedChunkSection* lower =
      find_chunk(decoded.value().chunks, {{0, 0, 0}});
  const arv::persistence::SavedChunkSection* neighbor =
      find_chunk(decoded.value().chunks, {{1, 0, 0}});
  ASSERT_NE(lower, nullptr);
  ASSERT_NE(neighbor, nullptr);
  EXPECT_EQ(lower->blocks[0], stone);
  EXPECT_EQ(neighbor->blocks[0], dirt);
  EXPECT_EQ(lower->lights[0], arv::voxel::pack_light(1, 2, 3, 4));
  EXPECT_TRUE(lower->fluids[0].source());
  EXPECT_TRUE(lower->lighting_valid);
  EXPECT_FALSE(neighbor->lighting_valid);
}

TEST(RegionFileTests, ChunkSerializationAllowsSectionsOutsideLegacyMetadataWindow) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID dirt = registry.find_variant("arvane:dirt").value();
  const std::vector<arv::persistence::SavedChunkSection> chunks{
      make_saved_chunk({{0, -48, 0}}, stone, true),
      make_saved_chunk({{0, 300, 0}}, dirt, true)};

  auto encoded = arv::persistence::encode_region_file({0, 0}, chunks, registry, 0);
  ASSERT_TRUE(encoded) << encoded.error().message;
  auto decoded = arv::persistence::decode_region_file(encoded.value(), registry, 123);

  ASSERT_TRUE(decoded);
  ASSERT_EQ(decoded.value().chunks.size(), 2U);
  const arv::persistence::SavedChunkSection* lower =
      find_chunk(decoded.value().chunks, {{0, -48, 0}});
  const arv::persistence::SavedChunkSection* upper =
      find_chunk(decoded.value().chunks, {{0, 300, 0}});
  ASSERT_NE(lower, nullptr);
  ASSERT_NE(upper, nullptr);
  EXPECT_EQ(lower->blocks[0], stone);
  EXPECT_EQ(upper->blocks[0], dirt);
}

TEST(RegionFileTests, BlockContainerSerializationRoundTripsOccupiedSlots) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  const arv::player::ItemID dirt_item =
      item_registry.find_item("arvane:dirt").value();

  arv::persistence::SavedChunkSection chunk =
      make_saved_chunk({{0, 0, 0}}, stone, true);
  arv::world::SavedBlockContainer container;
  container.local = {{2, 3, 4}};
  container.container_type = "arvane:furnace";
  container.slot_count = 3;
  container.slots.push_back({2, arv::player::ItemStack{dirt_item, 12}});
  container.furnace.active_cycle = arv::world::FurnaceCycleState{
      stone_item,
      3U,
      dirt_item,
      1U,
      20.0F,
      42.5F};
  chunk.block_containers.push_back(std::move(container));

  const std::vector<arv::persistence::SavedChunkSection> chunks{std::move(chunk)};
  auto encoded = arv::persistence::encode_region_file({0, 0}, chunks, block_registry, 0);
  ASSERT_TRUE(encoded) << encoded.error().message;
  auto decoded = arv::persistence::decode_region_file(encoded.value(), block_registry, 0);
  ASSERT_TRUE(decoded) << decoded.error().message;

  ASSERT_EQ(decoded.value().chunks.size(), 1U);
  ASSERT_EQ(decoded.value().chunks[0].block_containers.size(), 1U);
  const arv::world::SavedBlockContainer& loaded =
      decoded.value().chunks[0].block_containers[0];
  EXPECT_EQ(loaded.local, (arv::SectionLocalPos{{2, 3, 4}}));
  EXPECT_EQ(loaded.container_type, "arvane:furnace");
  EXPECT_EQ(loaded.slot_count, 3U);
  ASSERT_EQ(loaded.slots.size(), 1U);
  EXPECT_EQ(loaded.slots[0].slot, 2U);
  EXPECT_EQ(loaded.slots[0].stack.item, dirt_item);
  EXPECT_EQ(loaded.slots[0].stack.count, 12U);
  ASSERT_TRUE(loaded.furnace.active_cycle.has_value());
  EXPECT_EQ(loaded.furnace.active_cycle->input, stone_item);
  EXPECT_EQ(loaded.furnace.active_cycle->input_count, 3U);
  EXPECT_EQ(loaded.furnace.active_cycle->output, dirt_item);
  EXPECT_EQ(loaded.furnace.active_cycle->output_count, 1U);
  EXPECT_FLOAT_EQ(loaded.furnace.active_cycle->duration_secs, 20.0F);
  EXPECT_FLOAT_EQ(loaded.furnace.active_cycle->start_at, 42.5F);
}

TEST(RegionFileTests, ChunkSerializationAllowsMoreThanU8SectionsPerColumn) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  std::vector<arv::persistence::SavedChunkSection> chunks;
  chunks.reserve(260U);
  for (arv::i32 y = 0; y < 260; ++y) {
    chunks.push_back(make_saved_chunk({{0, y, 0}}, stone, true));
  }

  auto encoded = arv::persistence::encode_region_file({0, 0}, chunks, registry, 0);
  ASSERT_TRUE(encoded) << encoded.error().message;
  auto decoded = arv::persistence::decode_region_file(encoded.value(), registry, 0);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().chunks.size(), chunks.size());
  EXPECT_NE(find_chunk(decoded.value().chunks, {{0, 259, 0}}), nullptr);
}

TEST(RegionFileTests, ParsesEncodedRegionHeaderMetadata) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const std::vector<arv::persistence::SavedChunkSection> chunks{
      make_saved_chunk({{64, 0, -96}}, stone, true)};

  auto encoded = arv::persistence::encode_region_file({2, -3}, chunks, registry, 0);
  ASSERT_TRUE(encoded);
  auto header = arv::persistence::parse_region_header(encoded.value());

  ASSERT_TRUE(header);
  EXPECT_EQ(header.value().format_version, arv::persistence::CurrentRegionFormatVersion);
  EXPECT_EQ(header.value().region, (arv::RegionCoord{2, -3}));
  EXPECT_EQ(header.value().chunk_count, arv::persistence::RegionChunkSlotCount);
  EXPECT_GT(header.value().file_size, arv::persistence::RegionHeaderSize);
}
