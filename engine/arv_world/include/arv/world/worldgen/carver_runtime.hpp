#pragma once

#include "arv/world/worldgen/blueprint_placement.hpp"
#include "arv/world/worldgen/chunk_generator.hpp"

namespace arv::world::worldgen {

void apply_carvers(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    GeneratedChunkEditView& view,
    u64 seed,
    WorldGenerationStats* stats = nullptr);

} // namespace arv::world::worldgen
