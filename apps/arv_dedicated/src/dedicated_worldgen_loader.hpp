#pragma once

#include "dedicated_error.hpp"

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/asset/pack.hpp"
#include "arv/io/config.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/worldgen/registry.hpp"

#include <memory>

namespace arv::dedicated {

struct DedicatedWorldgen {
  world::WorldGenerationConfig default_config;
  std::shared_ptr<const world::worldgen::WorldGenRegistry> registry{};
};

[[nodiscard]] Expected<DedicatedWorldgen, DedicatedError> load_dedicated_worldgen(
    const io::EngineConfig& config,
    const asset::PackManager& packs,
    const voxel::BlockRegistry& block_registry);

[[nodiscard]] Expected<world::WorldGenerationPalette, DedicatedError>
resolve_dedicated_worldgen_palette(
    const world::WorldGenerationConfig& config,
    const voxel::BlockRegistry& block_registry);

} // namespace arv::dedicated
