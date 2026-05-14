#pragma once

#include "arv/world/worldgen/common.hpp"

#include <string>
#include <vector>

namespace arv::world::worldgen {

struct GeneratorConfig {
  std::string type{"arvane:generator/layered_heightmap"};
};

struct DimensionConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  std::string seed_salt{"overworld"};
  VerticalConfig vertical{};
  GeneratorConfig generator{};
  ResourceRef biome_source{};
  ResourceRef terrain{};
  ResourceRef surface_rule_set{};
  std::vector<ResourceRef> carvers{};
  std::vector<StageFeatureList> features{};
  std::vector<ResourceRef> structure_sets{};
};

} // namespace arv::world::worldgen
