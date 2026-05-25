#pragma once

#include "arv/world/worldgen/common.hpp"
#include "arv/world/natural_spawn_types.hpp"

#include <array>
#include <optional>
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

struct BiomeSpawnConditionConfig {
  std::optional<i32> min_y{};
  std::optional<i32> max_y{};
  std::optional<u8> min_light{};
  std::optional<u8> max_light{};
  bool requires_surface{};
  bool requires_sky_access{};
  bool requires_water{};
  bool requires_air_body{true};
  bool requires_air_head{true};
  std::vector<ResourceRef> block_tags_below{};
  std::vector<ResourceRef> block_tags_inside{};
  std::vector<ResourceRef> block_tags_head{};
  u32 min_distance_from_player_blocks{24};
  u32 max_distance_from_player_blocks{96};
  std::optional<TimePredicate> time{};
};

struct BiomeSpawnEntryConfig {
  ResourceRef entity{};
  SpawnCategory category{SpawnCategory::Passive};
  u32 weight{};
  u8 min_group{1};
  u8 max_group{1};
  BiomeSpawnConditionConfig conditions{};
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
  std::vector<BiomeSpawnEntryConfig> spawns{};
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
