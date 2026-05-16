#pragma once

#include "arv/world/worldgen/common.hpp"

#include <array>
#include <string>
#include <vector>

namespace arv::world::worldgen {

inline constexpr usize BiomeBlendWeightCount = 4;

struct BiomeTerrainModifiers {
  f32 base_height_offset{};
  f32 height_scale_multiplier{1.0F};
  f32 hill_multiplier{1.0F};
  f32 ridge_multiplier{1.0F};
  f32 roughness_multiplier{1.0F};
  f32 terrace_step{};
  f32 terrace_strength{};
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
  std::string display_name{};
  std::vector<ResourceRef> tags{};
  BiomeTerrainModifiers terrain{};
  ResourceRef surface_rule_set{};
  std::vector<StageFeatureList> features{};
  BiomeColors colors{};
};

struct BiomeWeight {
  usize biome_index{};
  f32 weight{};
};

struct BiomeSample {
  usize dominant_biome{};
  usize weight_count{};
  std::array<BiomeWeight, BiomeBlendWeightCount> weights{};
};

} // namespace arv::world::worldgen
