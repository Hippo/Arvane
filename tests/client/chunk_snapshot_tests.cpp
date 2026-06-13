#include "arv/client/chunk_snapshot.hpp"

#include <gtest/gtest.h>

TEST(ClientChunkSnapshotTests, ConvertsSectionPacketToVoxelSnapshot) {
  arv::protocol::ClientboundChunkSectionData section;
  section.world_id = arv::WorldID{2};
  section.coord = {{3, -1, 4}};
  section.chunk_version = 11;
  section.chunk_content_version = 12;
  section.chunk_light_version = 13;
  section.has_lighting = true;
  section.lighting_valid = true;
  section.has_fluids = true;
  section.blocks.fill(arv::voxel::AirBlockVariantID);
  section.lights.fill(arv::voxel::NoLight);
  section.fluids.fill(arv::voxel::empty_fluid_cell());

  const arv::SectionLocalPos local{{1, 2, 3}};
  const arv::usize index = arv::voxel::local_index(local);
  section.blocks[index] = arv::voxel::BlockVariantID{7};
  section.lights[index] = arv::voxel::pack_light(1, 2, 3, 4);
  section.fluids[index] = arv::voxel::make_fluid_source(
      arv::voxel::make_water_fluid_definition());

  const arv::voxel::ChunkSnapshot snapshot =
      arv::client::chunk_snapshot_from_section_data(section);

  EXPECT_EQ(snapshot.coord, section.coord);
  EXPECT_EQ(snapshot.source_version, 11U);
  EXPECT_EQ(snapshot.source_content_version, 12U);
  EXPECT_EQ(snapshot.source_light_version, 13U);
  EXPECT_EQ(arv::voxel::snapshot_block(snapshot, local), arv::voxel::BlockVariantID{7});
  EXPECT_EQ(arv::voxel::snapshot_light(snapshot, local), arv::voxel::pack_light(1, 2, 3, 4));
  EXPECT_EQ(
      arv::voxel::snapshot_fluid(snapshot, local),
      arv::voxel::make_fluid_source(arv::voxel::make_water_fluid_definition()));
}

TEST(ClientChunkSnapshotTests, MissingLightingUsesVisibleFallbackLight) {
  arv::protocol::ClientboundChunkSectionData section;
  section.coord = {{0, 0, 0}};
  section.chunk_version = 1;
  section.chunk_content_version = 2;
  section.chunk_light_version = 3;
  section.blocks.fill(arv::voxel::AirBlockVariantID);

  const arv::voxel::ChunkSnapshot snapshot =
      arv::client::chunk_snapshot_from_section_data(section);

  const arv::voxel::LightWord light =
      arv::voxel::snapshot_light(snapshot, arv::SectionLocalPos{{0, 0, 0}});
  EXPECT_EQ(arv::voxel::light_r(light), arv::voxel::MaxLightLevel);
  EXPECT_EQ(arv::voxel::light_g(light), arv::voxel::MaxLightLevel);
  EXPECT_EQ(arv::voxel::light_b(light), arv::voxel::MaxLightLevel);
  EXPECT_EQ(arv::voxel::light_sky(light), arv::voxel::MaxLightLevel);
}
