#include "arv/client/chunk_snapshot.hpp"

#include <memory>
#include <utility>

namespace arv::client {

voxel::ChunkSnapshot chunk_snapshot_from_section_data(
    const protocol::ClientboundChunkSectionData& section) {
  auto storage = std::make_shared<voxel::ChunkStorage>();
  storage->blocks = section.blocks;
  storage->lights = section.has_lighting && section.lighting_valid
                        ? section.lights
                        : std::array<voxel::LightWord, voxel::ChunkVolume>{};
  if (!section.has_lighting || !section.lighting_valid) {
    storage->lights.fill(voxel::pack_light(
        voxel::MaxLightLevel,
        voxel::MaxLightLevel,
        voxel::MaxLightLevel,
        voxel::MaxLightLevel));
  }
  storage->fluids = section.has_fluids
                        ? section.fluids
                        : std::array<voxel::FluidCell, voxel::ChunkVolume>{};
  if (!section.has_fluids) {
    storage->fluids.fill(voxel::empty_fluid_cell());
  }

  voxel::ChunkSnapshot snapshot;
  snapshot.coord = section.coord;
  snapshot.source_version = section.chunk_version;
  snapshot.source_content_version = section.chunk_content_version;
  snapshot.source_light_version = section.chunk_light_version;
  snapshot.storages[voxel::ChunkSnapshotCenterStorageIndex] = std::move(storage);
  return snapshot;
}

} // namespace arv::client
