#include "dedicated_worldgen_loader.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/core/log.hpp"
#include "arv/world/worldgen/loader.hpp"

#include <memory>
#include <utility>

namespace arv::dedicated {

Expected<DedicatedWorldgen, DedicatedError> load_dedicated_worldgen(
    const io::EngineConfig& config,
    const asset::PackManager& packs,
    const voxel::BlockRegistry& block_registry) {
  (void)config;
  auto registry = world::worldgen::load_worldgen_registry_from_packs(packs);
  if (!registry) {
    return Expected<DedicatedWorldgen, DedicatedError>::failure(
        dedicated_error("worldgen", registry.error().describe()));
  }

  auto registry_ptr =
      std::make_shared<world::worldgen::WorldGenRegistry>(std::move(registry.value()));
  auto resolved = world::worldgen::resolve_dimension(
      *registry_ptr,
      base::ids::OverworldWorldgenDataID,
      block_registry);
  if (!resolved) {
    return Expected<DedicatedWorldgen, DedicatedError>::failure(
        dedicated_error("worldgen", resolved.error().describe()));
  }

  DedicatedWorldgen worldgen;
  worldgen.registry = std::move(registry_ptr);
  worldgen.default_config = world::config_from_resolved_dimension(std::move(resolved.value()));
  core::info(
      "dedicated",
      "loaded worldgen preset: id={} y=[{}, {}] sea_level={}",
      worldgen.default_config.id,
      worldgen.default_config.min_y,
      worldgen.default_config.max_y,
      worldgen.default_config.sea_level);

  return Expected<DedicatedWorldgen, DedicatedError>::success(std::move(worldgen));
}

Expected<world::WorldGenerationPalette, DedicatedError> resolve_dedicated_worldgen_palette(
    const world::WorldGenerationConfig& config,
    const voxel::BlockRegistry& block_registry) {
  if (config.resolved_dimension != nullptr) {
    world::WorldGenerationPalette palette;
    palette.resolved_dimension = config.resolved_dimension;
    return Expected<world::WorldGenerationPalette, DedicatedError>::success(std::move(palette));
  }
  auto worldgen_palette = world::resolve_world_generation_palette(config, block_registry);
  if (!worldgen_palette) {
    return Expected<world::WorldGenerationPalette, DedicatedError>::failure(
        dedicated_error("worldgen", worldgen_palette.error().message));
  }
  return Expected<world::WorldGenerationPalette, DedicatedError>::success(
      std::move(worldgen_palette.value()));
}

} // namespace arv::dedicated
