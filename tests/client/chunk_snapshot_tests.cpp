#include "arv/client/chunk_snapshot.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>

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
  section.lights.emplace();
  section.lights->fill(arv::voxel::NoLight);
  section.fluids.emplace();
  section.fluids->fill(arv::voxel::empty_fluid_cell());

  const arv::SectionLocalPos local{{1, 2, 3}};
  const arv::usize index = arv::voxel::local_index(local);
  section.blocks[index] = arv::voxel::BlockVariantID{7};
  section.lights.value()[index] = arv::voxel::pack_light(1, 2, 3, 4);
  section.fluids.value()[index] = arv::voxel::make_fluid_source(
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

TEST(ClientChunkSnapshotTests, MeshingSnapshotIncludesLoadedBoundaryNeighborLights) {
  arv::protocol::ClientboundChunkSectionData center;
  center.world_id = arv::WorldID{2};
  center.coord = {{3, 0, 4}};
  center.chunk_version = 1;
  center.chunk_content_version = 2;
  center.chunk_light_version = 3;
  center.has_lighting = true;
  center.lighting_valid = true;
  center.blocks.fill(arv::voxel::AirBlockVariantID);
  center.lights.emplace();
  center.lights->fill(arv::voxel::NoLight);

  arv::protocol::ClientboundChunkSectionData neighbor = center;
  neighbor.coord = {{4, 0, 4}};
  neighbor.chunk_content_version = 4;
  neighbor.chunk_light_version = 5;
  neighbor.lights->fill(arv::voxel::NoLight);
  neighbor.lights.value()[arv::voxel::local_index({{0, 1, 1}})] =
      arv::voxel::pack_light(6, 7, 8, 9);

  const std::array sections{center, neighbor};
  const arv::voxel::ChunkSnapshot snapshot =
      arv::client::chunk_meshing_snapshot_from_section_data(
          center,
          std::span<const arv::protocol::ClientboundChunkSectionData>{sections});

  const arv::voxel::LightWord boundary_light =
      arv::voxel::snapshot_light(snapshot, arv::SectionLocalPos{{arv::ChunkSize, 1, 1}});
  EXPECT_EQ(boundary_light, arv::voxel::pack_light(6, 7, 8, 9));
}

TEST(ClientChunkSnapshotTests, MeshingDependencySignatureTracksNeighborLighting) {
  arv::protocol::ClientboundChunkSectionData center;
  center.world_id = arv::WorldID{2};
  center.coord = {{0, 0, 0}};
  center.chunk_content_version = 10;
  center.chunk_light_version = 20;

  arv::protocol::ClientboundChunkSectionData neighbor = center;
  neighbor.coord = {{1, 0, 0}};
  neighbor.chunk_content_version = 11;
  neighbor.chunk_light_version = 21;

  const std::array center_only{center};
  const std::array with_neighbor{center, neighbor};
  const arv::u64 center_signature =
      arv::client::chunk_meshing_dependency_signature(
          center,
          std::span<const arv::protocol::ClientboundChunkSectionData>{center_only});
  const arv::u64 with_neighbor_signature =
      arv::client::chunk_meshing_dependency_signature(
          center,
          std::span<const arv::protocol::ClientboundChunkSectionData>{with_neighbor});
  EXPECT_NE(center_signature, with_neighbor_signature);

  neighbor.chunk_light_version = 22;
  const std::array with_updated_neighbor{center, neighbor};
  const arv::u64 updated_neighbor_signature =
      arv::client::chunk_meshing_dependency_signature(
          center,
          std::span<const arv::protocol::ClientboundChunkSectionData>{with_updated_neighbor});
  EXPECT_NE(with_neighbor_signature, updated_neighbor_signature);
}
