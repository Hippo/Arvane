#pragma once

#include "arv/world/worldgen/registry.hpp"
#include "arv/world/worldgen/stats.hpp"

namespace arv::world {
struct ChunkGenerationRequest;
struct GeneratedChunk;
struct WorldGenerationError;
using GeneratedChunkResult = Expected<GeneratedChunk, WorldGenerationError>;
}

namespace arv::world::worldgen {

[[nodiscard]] TerrainProfile generate_terrain_profile(
    const ResolvedDimension& dimension,
    u64 seed,
    ChunkCoord coord,
    WorldGenerationStats* stats = nullptr);

[[nodiscard]] GeneratedChunkResult generate_resolved_chunk(
    const ChunkGenerationRequest& request,
    const ResolvedDimension& dimension);

} // namespace arv::world::worldgen
