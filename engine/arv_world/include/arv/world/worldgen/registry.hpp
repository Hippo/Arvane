#pragma once

#include "arv/blueprint/blueprint.hpp"
#include "arv/core/expected.hpp"
#include "arv/world/worldgen/biome.hpp"
#include "arv/world/worldgen/biome_source.hpp"
#include "arv/world/worldgen/carver.hpp"
#include "arv/world/worldgen/dimension.hpp"
#include "arv/world/worldgen/feature.hpp"
#include "arv/world/worldgen/structure_set.hpp"
#include "arv/world/worldgen/surface_rules.hpp"
#include "arv/world/worldgen/terrain.hpp"

#include <memory>
#include <string>
#include <vector>

namespace arv::world::worldgen {

struct WorldGenRegistry {
  HashMap<std::string, DimensionConfig> dimensions{};
  HashMap<std::string, NoiseFileConfig> noises{};
  HashMap<std::string, TerrainConfig> terrains{};
  HashMap<std::string, BiomeSourceConfig> biome_sources{};
  HashMap<std::string, BiomeConfig> biomes{};
  HashMap<std::string, BiomeSpawnConditionConfig> spawn_conditions{};
  HashMap<std::string, SpawnTableConfig> spawn_tables{};
  HashMap<std::string, SpawnBudgetProfileConfig> spawn_budget_profiles{};
  HashMap<std::string, SurfaceRuleSetConfig> surface_rule_sets{};
  HashMap<std::string, CarverConfig> carvers{};
  HashMap<std::string, ConfiguredFeatureConfig> configured_features{};
  HashMap<std::string, PlacedFeatureConfig> placed_features{};
  HashMap<std::string, StructureSetConfig> structure_sets{};
  HashMap<std::string, blueprint::Blueprint> blueprints{};
};

struct ResolvedBiome {
  BiomeConfig raw{};
  std::optional<usize> surface_rule_set_index{};
  std::vector<usize> tag_indices{};
  std::vector<usize> features_by_stage[StageOrder.size()];
};

struct ResolvedClimateField {
  std::string name{};
  NoiseFieldConfig noise{};
  f32 scale{1.0F};
  f32 offset{};
};

struct ResolvedClimateRange {
  usize field_index{};
  FloatRange range{};
};

struct ResolvedBiomeSourceEntry {
  usize biome_index{};
  std::vector<ClimateRange> climate{};
  std::vector<ResolvedClimateRange> resolved_climate{};
};

struct ResolvedBiomeSource {
  BiomeSourceConfig raw{};
  NoiseFileConfig noise{};
  std::vector<ResolvedClimateField> fields{};
  std::vector<ResolvedBiomeSourceEntry> entries{};
};

struct ResolvedDimension {
  DimensionConfig raw{};
  TerrainConfig terrain{};
  ResolvedBiomeSource biome_source{};
  std::vector<ResolvedBiome> biomes{};
  std::vector<std::string> biome_tags{};
  std::vector<ResolvedSurfaceRuleSet> surface_rule_sets{};
  std::optional<usize> dimension_surface_rule_set{};
  std::vector<CarverConfig> carvers{};
  std::vector<ResolvedConfiguredFeature> configured_features{};
  std::vector<ResolvedPlacedFeature> placed_features{};
  std::vector<ResolvedStructureSet> structure_sets{};
  std::vector<usize> dimension_features_by_stage[StageOrder.size()];
  std::vector<usize> biome_features_by_stage[StageOrder.size()];
  std::vector<usize> features_by_stage[StageOrder.size()];
  std::vector<voxel::BlockVariantID> replaceable_blocks{};
  voxel::BlockVariantID water_block{voxel::AirBlockVariantID};
};

using RegistryResult = Expected<WorldGenRegistry, WorldGenDiagnostic>;
using ResolvedDimensionResult = Expected<std::shared_ptr<const ResolvedDimension>, WorldGenDiagnostic>;

[[nodiscard]] const DimensionConfig* find_dimension(
    const WorldGenRegistry& registry,
    std::string_view id) noexcept;

[[nodiscard]] ResolvedDimensionResult resolve_dimension(
    const WorldGenRegistry& registry,
    std::string_view dimension_id,
    const voxel::BlockRegistry& block_registry);

} // namespace arv::world::worldgen
