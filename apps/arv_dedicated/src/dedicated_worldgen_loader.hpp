#pragma once

#include "dedicated_error.hpp"

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/io/config.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_generation.hpp"

namespace arv::dedicated {

struct DedicatedWorldgen {
  world::WorldGenerationConfig default_config;
};

[[nodiscard]] Expected<DedicatedWorldgen, DedicatedError> load_dedicated_worldgen(
    const io::EngineConfig& config);

[[nodiscard]] Expected<world::WorldGenerationPalette, DedicatedError>
resolve_dedicated_worldgen_palette(
    const world::WorldGenerationConfig& config,
    const voxel::BlockRegistry& block_registry);

} // namespace arv::dedicated
