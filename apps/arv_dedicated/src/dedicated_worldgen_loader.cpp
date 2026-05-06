#include "dedicated_worldgen_loader.hpp"

#include "arv/core/log.hpp"

#include <utility>

namespace arv::dedicated {

Expected<DedicatedWorldgen, DedicatedError> load_dedicated_worldgen(
    const io::EngineConfig& config) {
  auto worldgen_text = io::read_text_file(
      config.paths.config_path() / "worldgen/overworld.yaml",
      "dedicated overworld generator definition");
  if (!worldgen_text) {
    return Expected<DedicatedWorldgen, DedicatedError>::failure(
        dedicated_error("worldgen", worldgen_text.error().message));
  }

  auto worldgen_config = world::parse_world_generation_yaml(worldgen_text.value());
  if (!worldgen_config) {
    return Expected<DedicatedWorldgen, DedicatedError>::failure(
        dedicated_error("worldgen", worldgen_config.error().message));
  }

  DedicatedWorldgen worldgen;
  worldgen.default_config = std::move(worldgen_config.value());
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
  auto worldgen_palette = world::resolve_world_generation_palette(config, block_registry);
  if (!worldgen_palette) {
    return Expected<world::WorldGenerationPalette, DedicatedError>::failure(
        dedicated_error("worldgen", worldgen_palette.error().message));
  }
  return Expected<world::WorldGenerationPalette, DedicatedError>::success(
      std::move(worldgen_palette.value()));
}

} // namespace arv::dedicated
