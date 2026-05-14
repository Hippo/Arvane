#pragma once

#include "arv/world/worldgen/feature.hpp"

#include <string>
#include <vector>

namespace arv::world::worldgen {

struct StructureEntryConfig {
  ResourceRef feature{};
  i32 weight{1};
};

struct StructurePlacementConfig {
  std::string type{"grid"};
  i32 average_distance_chunks{32};
  i32 separation_chunks{8};
};

struct StructureSetConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  u64 salt{};
  std::vector<StructureEntryConfig> structures{};
  StructurePlacementConfig placement{};
  std::vector<PlacementFilterConfig> filters{};
};

struct ResolvedStructureEntry {
  usize feature_index{};
  i32 weight{1};
};

struct ResolvedStructureSet {
  StructureSetConfig raw{};
  std::vector<ResolvedStructureEntry> structures{};
  std::vector<ResolvedPlacementFilter> filters{};
  i32 radius_blocks{};
};

} // namespace arv::world::worldgen
