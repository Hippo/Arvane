#include "arv/core/coordinates.hpp"

#include <gtest/gtest.h>

TEST(CoordinateTests, FloorDivAndModHandleNegativeValues) {
  EXPECT_EQ(arv::floor_div(-1, arv::ChunkSize), -1);
  EXPECT_EQ(arv::floor_mod(-1, arv::ChunkSize), 15);
  EXPECT_EQ(arv::floor_div(-16, arv::ChunkSize), -1);
  EXPECT_EQ(arv::floor_mod(-16, arv::ChunkSize), 0);
  EXPECT_EQ(arv::floor_div(-17, arv::ChunkSize), -2);
  EXPECT_EQ(arv::floor_mod(-17, arv::ChunkSize), 15);
}

TEST(CoordinateTests, BlockToChunkAndLocalRoundTrip) {
  const arv::BlockPos block{{-17, 0, 31}};
  const arv::ChunkCoord chunk = arv::block_to_chunk(block);
  const arv::SectionLocalPos local = arv::block_to_local(block);

  EXPECT_EQ(chunk, (arv::ChunkCoord{{-2, 0, 1}}));
  EXPECT_EQ(local, (arv::SectionLocalPos{{15, 0, 15}}));
  EXPECT_EQ(arv::chunk_local_to_block(chunk, local), block);
}

TEST(CoordinateTests, ChunkColumnsConvertToRegionsWithNegativeCoordinates) {
  EXPECT_EQ(arv::chunk_column_to_region({-1, 31}), (arv::RegionCoord{-1, 0}));
  EXPECT_EQ(arv::chunk_column_to_region_local({-1, 31}), (arv::RegionLocalChunkCoord{31, 31}));
  EXPECT_EQ(arv::chunk_column_to_region({-33, -32}), (arv::RegionCoord{-2, -1}));
  EXPECT_EQ(arv::chunk_column_to_region_local({-33, -32}), (arv::RegionLocalChunkCoord{31, 0}));
}

TEST(CoordinateTests, NeighborChunkMovesAlongRequestedAxis) {
  EXPECT_EQ(
      arv::neighbor_chunk({{1, 2, 3}}, arv::AxisDirection::PositiveX),
      (arv::ChunkCoord{{2, 2, 3}}));
  EXPECT_EQ(
      arv::neighbor_chunk({{1, 2, 3}}, arv::AxisDirection::NegativeZ),
      (arv::ChunkCoord{{1, 2, 2}}));
}
