#pragma once

#include "arv/world/worldgen/common.hpp"

#include <string>
#include <vector>

namespace arv::world::worldgen {

struct ClimateRange {
  std::string field{};
  FloatRange range{};
};

struct BiomeSourceEntryConfig {
  ResourceRef biome{};
  std::vector<ClimateRange> climate{};
};

struct BiomeSourceConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  std::string type{"arvane:biome_source/climate_map"};
  ResourceRef noise{};
  i32 sample_scale_xz{4};
  std::vector<BiomeSourceEntryConfig> biomes{};
};

} // namespace arv::world::worldgen
