#pragma once

#include "arv/protocol/chunk_streaming.hpp"
#include "arv/voxel/storage.hpp"

#include <span>

namespace arv::client {

using ChunkSectionDataLookup =
    FunctionRef<const protocol::ClientboundChunkSectionData*(ChunkSectionCoord)>;

[[nodiscard]] voxel::ChunkSnapshot chunk_snapshot_from_section_data(
    const protocol::ClientboundChunkSectionData& section);

[[nodiscard]] voxel::ChunkSnapshot chunk_meshing_snapshot_from_section_data(
    const protocol::ClientboundChunkSectionData& center,
    ChunkSectionDataLookup lookup);

[[nodiscard]] voxel::ChunkSnapshot chunk_meshing_snapshot_from_section_data(
    const protocol::ClientboundChunkSectionData& center,
    std::span<const protocol::ClientboundChunkSectionData> loaded_sections);

[[nodiscard]] u64 chunk_meshing_dependency_signature(
    const protocol::ClientboundChunkSectionData& center,
    ChunkSectionDataLookup lookup) noexcept;

[[nodiscard]] u64 chunk_meshing_dependency_signature(
    const protocol::ClientboundChunkSectionData& center,
    std::span<const protocol::ClientboundChunkSectionData> loaded_sections) noexcept;

} // namespace arv::client
