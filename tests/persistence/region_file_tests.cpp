#include "arv/persistence/region_file.hpp"

#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

arv::persistence::SavedChunkSection make_saved_chunk(
    arv::ChunkCoord coord,
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
    arv::ChunkCoord coord) {
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
