#include "arv/client/chunk_snapshot.hpp"

#include <array>
#include <memory>
#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] std::shared_ptr<voxel::ChunkStorage> storage_from_section_data(
    const protocol::ClientboundChunkSectionData& section) {
  auto storage = std::make_shared<voxel::ChunkStorage>();
  storage->blocks = section.blocks;
  storage->lights =
      section.has_lighting && section.lighting_valid && section.lights.has_value()
          ? section.lights.value()
          : std::array<voxel::LightWord, voxel::ChunkVolume>{};
  if (!section.has_lighting || !section.lighting_valid) {
    storage->lights.fill(voxel::pack_light(
        voxel::MaxLightLevel,
        voxel::MaxLightLevel,
        voxel::MaxLightLevel,
        voxel::MaxLightLevel));
  }
  storage->fluids =
      section.has_fluids && section.fluids.has_value()
          ? section.fluids.value()
          : std::array<voxel::FluidCell, voxel::ChunkVolume>{};
  if (!section.has_fluids) {
    storage->fluids.fill(voxel::empty_fluid_cell());
  }
  return storage;
}

[[nodiscard]] const protocol::ClientboundChunkSectionData* find_section_in_span(
    WorldID world_id,
    ChunkSectionCoord coord,
    std::span<const protocol::ClientboundChunkSectionData> loaded_sections) noexcept {
  for (const protocol::ClientboundChunkSectionData& section : loaded_sections) {
    if (section.world_id == world_id && section.coord == coord) {
      return &section;
    }
  }
  return nullptr;
}

[[nodiscard]] u64 mix_dependency_signature(u64 hash, u64 value) noexcept {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
  return hash;
}

} // namespace

voxel::ChunkSnapshot chunk_snapshot_from_section_data(
    const protocol::ClientboundChunkSectionData& section) {
  voxel::ChunkSnapshot snapshot;
  snapshot.coord = section.coord;
  snapshot.source_version = section.chunk_version;
  snapshot.source_content_version = section.chunk_content_version;
  snapshot.source_light_version = section.chunk_light_version;
  snapshot.storages[voxel::ChunkSnapshotCenterStorageIndex] =
      storage_from_section_data(section);
  return snapshot;
}

voxel::ChunkSnapshot chunk_meshing_snapshot_from_section_data(
    const protocol::ClientboundChunkSectionData& center,
    ChunkSectionDataLookup lookup) {
  voxel::ChunkSnapshot snapshot = chunk_snapshot_from_section_data(center);
  for (usize storage_index = 0U;
       storage_index < voxel::ChunkSnapshotNeighborhoodVolume;
       ++storage_index) {
    if (storage_index == voxel::ChunkSnapshotCenterStorageIndex) {
      continue;
    }
    const protocol::ClientboundChunkSectionData* section =
        lookup(offset_chunk_section(
            center.coord,
            voxel::snapshot_storage_offset(storage_index)));
    if (section != nullptr && section->world_id == center.world_id) {
      snapshot.storages[storage_index] = storage_from_section_data(*section);
    }
  }
  return snapshot;
}

voxel::ChunkSnapshot chunk_meshing_snapshot_from_section_data(
    const protocol::ClientboundChunkSectionData& center,
    std::span<const protocol::ClientboundChunkSectionData> loaded_sections) {
  auto lookup =
      [&](ChunkSectionCoord coord) -> const protocol::ClientboundChunkSectionData* {
    return find_section_in_span(center.world_id, coord, loaded_sections);
  };
  return chunk_meshing_snapshot_from_section_data(
      center,
      ChunkSectionDataLookup{lookup});
}

u64 chunk_meshing_dependency_signature(
    const protocol::ClientboundChunkSectionData& center,
    ChunkSectionDataLookup lookup) noexcept {
  u64 hash = 0xcbf29ce484222325ULL;
  for (usize storage_index = 0U;
       storage_index < voxel::ChunkSnapshotNeighborhoodVolume;
       ++storage_index) {
    const protocol::ClientboundChunkSectionData* section =
        storage_index == voxel::ChunkSnapshotCenterStorageIndex
            ? &center
            : lookup(offset_chunk_section(
                  center.coord,
                  voxel::snapshot_storage_offset(storage_index)));
    if (section != nullptr && section->world_id != center.world_id) {
      section = nullptr;
    }
    hash = mix_dependency_signature(hash, static_cast<u64>(storage_index));
    if (section == nullptr) {
      hash = mix_dependency_signature(hash, 0U);
      continue;
    }
    hash = mix_dependency_signature(hash, 1U);
    hash = mix_dependency_signature(hash, section->chunk_content_version);
    hash = mix_dependency_signature(hash, section->chunk_light_version);
    hash = mix_dependency_signature(hash, section->has_lighting ? 1U : 0U);
    hash = mix_dependency_signature(hash, section->lighting_valid ? 1U : 0U);
    hash = mix_dependency_signature(hash, section->has_fluids ? 1U : 0U);
  }
  return hash;
}

u64 chunk_meshing_dependency_signature(
    const protocol::ClientboundChunkSectionData& center,
    std::span<const protocol::ClientboundChunkSectionData> loaded_sections) noexcept {
  auto lookup =
      [&](ChunkSectionCoord coord) -> const protocol::ClientboundChunkSectionData* {
    return find_section_in_span(center.world_id, coord, loaded_sections);
  };
  return chunk_meshing_dependency_signature(
      center,
      ChunkSectionDataLookup{lookup});
}

} // namespace arv::client
