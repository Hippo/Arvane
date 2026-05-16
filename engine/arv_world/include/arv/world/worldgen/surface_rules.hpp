#pragma once

#include "arv/world/worldgen/common.hpp"
#include "arv/voxel/block.hpp"

#include <string>
#include <vector>

namespace arv::world::worldgen {

struct SurfaceRulePredicate {
  std::optional<bool> above_sea_level{};
  std::optional<bool> below_sea_level{};
  std::optional<bool> underwater{};
  std::optional<ResourceRef> biome{};
  std::optional<ResourceRef> biome_tag{};
  std::optional<ResourceRef> biome_tag_not{};
  std::optional<f32> slope_less_than{};
  std::optional<f32> slope_greater_than{};
  std::optional<i32> height_above{};
  std::optional<i32> height_below{};
};

struct SurfaceLayerConfig {
  std::string block{};
  i32 depth{};
  bool remaining{};
};

struct SurfaceRuleConfig {
  std::optional<SurfaceRulePredicate> when{};
  std::vector<SurfaceLayerConfig> layers{};
};

struct SurfaceRuleSetConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  std::vector<SurfaceRuleConfig> rules{};
};

struct ResolvedSurfaceLayer {
  voxel::BlockVariantID block{};
  i32 depth{};
  bool remaining{};
};

struct ResolvedSurfaceRule {
  SurfaceRulePredicate when{};
  bool has_predicate{};
  std::optional<usize> biome_index{};
  std::optional<usize> biome_tag_index{};
  std::optional<usize> biome_tag_not_index{};
  std::vector<ResolvedSurfaceLayer> layers{};
};

struct ResolvedSurfaceRuleSet {
  std::string id{};
  std::vector<ResolvedSurfaceRule> rules{};
};

} // namespace arv::world::worldgen
