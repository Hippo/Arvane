#pragma once

#include "dedicated_error.hpp"

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/io/config.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_generation.hpp"

#include <utility>

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

[[nodiscard]] std::pair<i32, i32> visible_worldgen_chunk_y_range(
    const world::WorldGenerationConfig& config);

} // namespace arv::dedicated
