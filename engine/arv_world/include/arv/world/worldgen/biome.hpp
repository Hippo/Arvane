#pragma once

#include "arv/world/worldgen/common.hpp"

#include <string>
#include <vector>

namespace arv::world::worldgen {

struct BiomeTerrainModifiers {
  f32 base_height_offset{};
  f32 height_scale_multiplier{1.0F};
  f32 roughness_multiplier{1.0F};
};

struct BiomeColors {
  std::optional<ColorValue> grass{};
  std::optional<ColorValue> foliage{};
  std::optional<ColorValue> water{};
  std::optional<ColorValue> fog{};
};

struct BiomeConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  std::vector<ResourceRef> tags{};
  BiomeTerrainModifiers terrain{};
  ResourceRef surface_rule_set{};
  std::vector<StageFeatureList> features{};
  BiomeColors colors{};
};

struct BiomeWeight {
  usize biome_index{};
  f32 weight{1.0F};
};

struct BiomeSample {
  usize dominant_biome{};
  std::array<BiomeWeight, 1> weights{};
};

} // namespace arv::world::worldgen
