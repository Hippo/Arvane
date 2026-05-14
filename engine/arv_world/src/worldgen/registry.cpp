#include "arv/world/worldgen/registry.hpp"

#include <algorithm>

namespace arv::world::worldgen {
namespace {

[[nodiscard]] WorldGenDiagnostic error(
    std::string resource,
    std::string field,
    std::string message) {
  return {std::move(resource), std::move(field), std::move(message)};
}

template<typename T>
[[nodiscard]] const T* find_by_id(
    const HashMap<std::string, T>& values,
    const ResourceRef& ref) noexcept {
  const auto iter = values.find(ref.id);
  return iter == values.end() ? nullptr : &iter->second;
}

[[nodiscard]] std::optional<usize> index_of_biome(
    const ResolvedDimension& dimension,
    std::string_view id) noexcept {
  for (usize index = 0; index < dimension.biomes.size(); ++index) {
    if (dimension.biomes[index].raw.id == id) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<usize> index_of_surface_rule_set(
    const ResolvedDimension& dimension,
    std::string_view id) noexcept {
  for (usize index = 0; index < dimension.surface_rule_sets.size(); ++index) {
    if (dimension.surface_rule_sets[index].id == id) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<usize> index_of_configured_feature(
    const ResolvedDimension& dimension,
    std::string_view id) noexcept {
  for (usize index = 0; index < dimension.configured_features.size(); ++index) {
    if (dimension.configured_features[index].id == id) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<usize> index_of_placed_feature(
    const ResolvedDimension& dimension,
    std::string_view id) noexcept {
  for (usize index = 0; index < dimension.placed_features.size(); ++index) {
    if (dimension.placed_features[index].raw.id == id) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool biome_has_tag(const BiomeConfig& biome, std::string_view tag) noexcept {
  return std::any_of(biome.tags.begin(), biome.tags.end(), [tag](const ResourceRef& ref) {
    return ref.id == tag;
  });
}

[[nodiscard]] std::optional<voxel::BlockVariantID> resolve_block_variant(
    const voxel::BlockRegistry& registry,
    std::string_view name) {
  if (const auto variant = registry.find_variant(name); variant) {
    return variant;
  }
  if (name.ends_with("[]")) {
    const std::string normalized{name.substr(0, name.size() - 2U)};
    if (const auto variant = registry.find_variant(normalized); variant) {
      return variant;
    }
  }
  if (const auto block = registry.find_block(name); block) {
    const voxel::BlockRecord* record = registry.block(block.value());
    if (record != nullptr) {
      return record->default_variant;
    }
  }
  return std::nullopt;
}

[[nodiscard]] Expected<voxel::BlockVariantID, WorldGenDiagnostic> require_block(
    const voxel::BlockRegistry& registry,
    std::string_view resource,
    std::string_view field,
    std::string_view name) {
  const auto block = resolve_block_variant(registry, name);
  if (!block) {
    return Expected<voxel::BlockVariantID, WorldGenDiagnostic>::failure(
        error(std::string{resource}, std::string{field}, "unknown block: " + std::string{name}));
  }
  return Expected<voxel::BlockVariantID, WorldGenDiagnostic>::success(block.value());
}

[[nodiscard]] Expected<std::vector<voxel::BlockVariantID>, WorldGenDiagnostic> resolve_blocks(
    const voxel::BlockRegistry& registry,
    std::string_view resource,
    std::string_view field,
    const std::vector<std::string>& names) {
  std::vector<voxel::BlockVariantID> blocks;
  blocks.reserve(names.size());
  for (const std::string& name : names) {
    auto block = require_block(registry, resource, field, name);
    if (!block) {
      return Expected<std::vector<voxel::BlockVariantID>, WorldGenDiagnostic>::failure(
          block.error());
    }
    blocks.push_back(block.value());
  }
  return Expected<std::vector<voxel::BlockVariantID>, WorldGenDiagnostic>::success(
      std::move(blocks));
}

[[nodiscard]] Expected<ResolvedSurfaceRuleSet, WorldGenDiagnostic> resolve_surface_rule_set(
    const SurfaceRuleSetConfig& config,
    const voxel::BlockRegistry& block_registry) {
  ResolvedSurfaceRuleSet resolved;
  resolved.id = config.id;
  for (const SurfaceRuleConfig& rule : config.rules) {
    ResolvedSurfaceRule resolved_rule;
    resolved_rule.has_predicate = rule.when.has_value();
    if (rule.when.has_value()) {
      resolved_rule.when = rule.when.value();
    }
    bool has_remaining = false;
    for (const SurfaceLayerConfig& layer : rule.layers) {
      auto block = require_block(block_registry, config.id, "rules[].layers[].block", layer.block);
      if (!block) {
        return Expected<ResolvedSurfaceRuleSet, WorldGenDiagnostic>::failure(block.error());
      }
      has_remaining = has_remaining || layer.remaining;
      resolved_rule.layers.push_back({block.value(), layer.depth, layer.remaining});
    }
    if (!has_remaining) {
      return Expected<ResolvedSurfaceRuleSet, WorldGenDiagnostic>::failure(
          error(config.id, "rules[].layers", "must include a remaining layer"));
    }
    resolved.rules.push_back(std::move(resolved_rule));
  }
  return Expected<ResolvedSurfaceRuleSet, WorldGenDiagnostic>::success(std::move(resolved));
}

[[nodiscard]] i32 configured_feature_radius(const ConfiguredFeatureConfig& feature) noexcept {
  if (const auto* tree = std::get_if<SimpleTreeFeatureConfig>(&feature.config)) {
    return std::max(tree->leaves_radius.max + 2, 4);
  }
  if (const auto* patch = std::get_if<PatchFeatureConfig>(&feature.config)) {
    return patch->radius_xz;
  }
  if (const auto* disk = std::get_if<DiskFeatureConfig>(&feature.config)) {
    return disk->radius.max;
  }
  if (const auto* ore = std::get_if<OreVeinFeatureConfig>(&feature.config)) {
    return ore->vein_size.max;
  }
  if (std::holds_alternative<SingleBlockFeatureConfig>(feature.config)) {
    return 1;
  }
  if (std::holds_alternative<BlueprintFeatureConfig>(feature.config)) {
    return ChunkSize;
  }
  return 0;
}

[[nodiscard]] Expected<ResolvedConfiguredFeature, WorldGenDiagnostic> resolve_configured_feature(
    const ConfiguredFeatureConfig& config,
    const WorldGenRegistry& registry,
    const voxel::BlockRegistry& block_registry) {
  ResolvedConfiguredFeature resolved;
  resolved.id = config.id;
  resolved.type = config.type;
  resolved.raw = config;
  resolved.radius_blocks = configured_feature_radius(config);

  auto resolve_replace = [&](const ReplaceRuleConfig& replace) -> Expected<void, WorldGenDiagnostic> {
    resolved.replace.only_air = replace.only_air;
    auto blocks = resolve_blocks(block_registry, config.id, "replace.blocks", replace.blocks);
    if (!blocks) {
      return Expected<void, WorldGenDiagnostic>::failure(blocks.error());
    }
    resolved.replace.blocks = std::move(blocks.value());
    return Expected<void, WorldGenDiagnostic>::success();
  };

  if (const auto* tree = std::get_if<SimpleTreeFeatureConfig>(&config.config)) {
    auto trunk = require_block(block_registry, config.id, "trunk.block", tree->trunk_block);
    auto leaves = require_block(block_registry, config.id, "leaves.block", tree->leaves_block);
    if (!trunk) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(trunk.error());
    }
    if (!leaves) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(leaves.error());
    }
    resolved.blocks = {trunk.value(), leaves.value()};
  } else if (const auto* patch = std::get_if<PatchFeatureConfig>(&config.config)) {
    auto block = require_block(block_registry, config.id, "block", patch->block);
    if (!block) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(block.error());
    }
    resolved.blocks = {block.value()};
    auto replace = resolve_replace(patch->replace);
    if (!replace) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(replace.error());
    }
    auto support = resolve_blocks(block_registry, config.id, "place_on", patch->place_on.blocks);
    if (!support) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(support.error());
    }
    resolved.support_blocks = std::move(support.value());
  } else if (const auto* disk = std::get_if<DiskFeatureConfig>(&config.config)) {
    auto block = require_block(block_registry, config.id, "block", disk->block);
    if (!block) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(block.error());
    }
    resolved.blocks = {block.value()};
    auto replace = resolve_replace(disk->replace);
    if (!replace) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(replace.error());
    }
  } else if (const auto* ore = std::get_if<OreVeinFeatureConfig>(&config.config)) {
    auto block = require_block(block_registry, config.id, "block", ore->block);
    if (!block) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(block.error());
    }
    resolved.blocks = {block.value()};
    auto replace = resolve_replace(ore->replace);
    if (!replace) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(replace.error());
    }
  } else if (const auto* single = std::get_if<SingleBlockFeatureConfig>(&config.config)) {
    auto block = require_block(block_registry, config.id, "block", single->block);
    if (!block) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(block.error());
    }
    resolved.blocks = {block.value()};
    auto replace = resolve_replace(single->replace);
    if (!replace) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(replace.error());
    }
    auto support = resolve_blocks(block_registry, config.id, "place_on", single->place_on.blocks);
    if (!support) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(support.error());
    }
    resolved.support_blocks = std::move(support.value());
  } else if (const auto* blueprint_feature = std::get_if<BlueprintFeatureConfig>(&config.config)) {
    const auto blueprint_iter = registry.blueprints.find(blueprint_feature->blueprint.id);
    if (blueprint_iter == registry.blueprints.end()) {
      return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(
          error(config.id, "blueprint", "unknown blueprint: " + blueprint_feature->blueprint.id));
    }
    resolved.blueprint = blueprint_iter->second;
    for (const std::string& entry : resolved.blueprint.palette) {
      auto block = require_block(block_registry, config.id, "blueprint.palette", entry);
      if (!block) {
        return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::failure(block.error());
      }
      resolved.blueprint_palette.push_back(block.value());
    }
    resolved.radius_blocks = std::max({
        resolved.radius_blocks,
        resolved.blueprint.size.value.x,
        resolved.blueprint.size.value.z});
  }

  return Expected<ResolvedConfiguredFeature, WorldGenDiagnostic>::success(std::move(resolved));
}

[[nodiscard]] Expected<ResolvedPlacementFilter, WorldGenDiagnostic> resolve_filter(
    const PlacementFilterConfig& filter,
    const ResolvedDimension& dimension,
    const voxel::BlockRegistry& block_registry,
    std::string_view resource) {
  ResolvedPlacementFilter resolved;
  resolved.raw = filter;
  if (filter.type == PlacementFilterConfig::Type::BlockBelow) {
    auto blocks = resolve_blocks(block_registry, resource, "filters[].blocks", filter.blocks);
    if (!blocks) {
      return Expected<ResolvedPlacementFilter, WorldGenDiagnostic>::failure(blocks.error());
    }
    resolved.blocks = std::move(blocks.value());
  }
  if (filter.type == PlacementFilterConfig::Type::Biome) {
    auto index = index_of_biome(dimension, filter.biome.id);
    if (!index) {
      return Expected<ResolvedPlacementFilter, WorldGenDiagnostic>::failure(
          error(std::string{resource}, "filters[].biome", "unknown biome: " + filter.biome.id));
    }
    resolved.biome_index = index;
  }
  return Expected<ResolvedPlacementFilter, WorldGenDiagnostic>::success(std::move(resolved));
}

} // namespace

const DimensionConfig* find_dimension(
    const WorldGenRegistry& registry,
    std::string_view id) noexcept {
  const auto iter = registry.dimensions.find(std::string{id});
  return iter == registry.dimensions.end() ? nullptr : &iter->second;
}

ResolvedDimensionResult resolve_dimension(
    const WorldGenRegistry& registry,
    std::string_view dimension_id,
    const voxel::BlockRegistry& block_registry) {
  const auto dimension_iter = registry.dimensions.find(std::string{dimension_id});
  if (dimension_iter == registry.dimensions.end()) {
    return ResolvedDimensionResult::failure(
        error(std::string{dimension_id}, "dimension", "unknown dimension"));
  }

  auto resolved = std::make_shared<ResolvedDimension>();
  resolved->raw = dimension_iter->second;

  const TerrainConfig* terrain = find_by_id(registry.terrains, resolved->raw.terrain);
  if (terrain == nullptr) {
    return ResolvedDimensionResult::failure(
        error(resolved->raw.id, "terrain", "unknown terrain: " + resolved->raw.terrain.id));
  }
  resolved->terrain = *terrain;

  const BiomeSourceConfig* biome_source =
      find_by_id(registry.biome_sources, resolved->raw.biome_source);
  if (biome_source == nullptr) {
    return ResolvedDimensionResult::failure(error(
        resolved->raw.id,
        "biome_source",
        "unknown biome source: " + resolved->raw.biome_source.id));
  }
  resolved->biome_source.raw = *biome_source;
  const NoiseFileConfig* noise = find_by_id(registry.noises, biome_source->noise);
  if (noise == nullptr) {
    return ResolvedDimensionResult::failure(
        error(biome_source->id, "noise", "unknown noise file: " + biome_source->noise.id));
  }
  resolved->biome_source.noise = *noise;

  for (const BiomeSourceEntryConfig& entry : biome_source->biomes) {
    const BiomeConfig* biome = find_by_id(registry.biomes, entry.biome);
    if (biome == nullptr) {
      return ResolvedDimensionResult::failure(
          error(biome_source->id, "biomes[].biome", "unknown biome: " + entry.biome.id));
    }
    if (!index_of_biome(*resolved, biome->id)) {
      resolved->biomes.push_back({*biome, std::nullopt});
    }
    const auto biome_index = index_of_biome(*resolved, biome->id);
    ResolvedBiomeSourceEntry resolved_entry;
    resolved_entry.biome_index = biome_index.value();
    resolved_entry.climate = entry.climate;
    for (const ClimateRange& range : resolved_entry.climate) {
      if (!noise->fields.contains(range.field)) {
        return ResolvedDimensionResult::failure(error(
            biome_source->id,
            "biomes[].climate." + range.field,
            "climate field is not defined in noise file"));
      }
    }
    resolved->biome_source.entries.push_back(std::move(resolved_entry));
  }

  auto add_surface_rule_set = [&](const ResourceRef& ref) -> Expected<usize, WorldGenDiagnostic> {
    if (ref.empty()) {
      return Expected<usize, WorldGenDiagnostic>::failure(
          error(resolved->raw.id, "surface_rule_set", "empty surface rule set reference"));
    }
    if (const auto existing = index_of_surface_rule_set(*resolved, ref.id); existing) {
      return Expected<usize, WorldGenDiagnostic>::success(existing.value());
    }
    const SurfaceRuleSetConfig* rule_set = find_by_id(registry.surface_rule_sets, ref);
    if (rule_set == nullptr) {
      return Expected<usize, WorldGenDiagnostic>::failure(
          error(resolved->raw.id, "surface_rule_set", "unknown surface rule set: " + ref.id));
    }
    auto resolved_rule_set = resolve_surface_rule_set(*rule_set, block_registry);
    if (!resolved_rule_set) {
      return Expected<usize, WorldGenDiagnostic>::failure(resolved_rule_set.error());
    }
    const usize index = resolved->surface_rule_sets.size();
    resolved->surface_rule_sets.push_back(std::move(resolved_rule_set.value()));
    return Expected<usize, WorldGenDiagnostic>::success(index);
  };

  if (!resolved->raw.surface_rule_set.empty()) {
    auto rule_set = add_surface_rule_set(resolved->raw.surface_rule_set);
    if (!rule_set) {
      return ResolvedDimensionResult::failure(rule_set.error());
    }
    resolved->dimension_surface_rule_set = rule_set.value();
  }

  for (ResolvedBiome& biome : resolved->biomes) {
    if (!biome.raw.surface_rule_set.empty()) {
      auto rule_set = add_surface_rule_set(biome.raw.surface_rule_set);
      if (!rule_set) {
        return ResolvedDimensionResult::failure(rule_set.error());
      }
      biome.surface_rule_set_index = rule_set.value();
    }
  }

  for (const ResourceRef& ref : resolved->raw.carvers) {
    const CarverConfig* carver = find_by_id(registry.carvers, ref);
    if (carver == nullptr) {
      return ResolvedDimensionResult::failure(
          error(resolved->raw.id, "carvers", "unknown carver: " + ref.id));
    }
    resolved->carvers.push_back(*carver);
  }

  for (const auto& [id, feature] : registry.configured_features) {
    (void)id;
    auto resolved_feature = resolve_configured_feature(feature, registry, block_registry);
    if (!resolved_feature) {
      return ResolvedDimensionResult::failure(resolved_feature.error());
    }
    resolved->configured_features.push_back(std::move(resolved_feature.value()));
  }
  std::sort(
      resolved->configured_features.begin(),
      resolved->configured_features.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
      });

  for (const auto& [id, placed] : registry.placed_features) {
    (void)id;
    const auto feature_index = index_of_configured_feature(*resolved, placed.feature.id);
    if (!feature_index) {
      return ResolvedDimensionResult::failure(
          error(placed.id, "feature", "unknown configured feature: " + placed.feature.id));
    }
    ResolvedPlacedFeature resolved_placed;
    resolved_placed.raw = placed;
    resolved_placed.feature_index = feature_index.value();
    resolved_placed.radius_blocks = resolved->configured_features[feature_index.value()].radius_blocks;
    for (const PlacementFilterConfig& filter : placed.filters) {
      auto resolved_filter = resolve_filter(filter, *resolved, block_registry, placed.id);
      if (!resolved_filter) {
        return ResolvedDimensionResult::failure(resolved_filter.error());
      }
      resolved_placed.filters.push_back(std::move(resolved_filter.value()));
    }
    resolved->placed_features.push_back(std::move(resolved_placed));
  }
  std::sort(
      resolved->placed_features.begin(),
      resolved->placed_features.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.raw.id < rhs.raw.id;
      });

  auto add_stage_refs = [&](const std::vector<StageFeatureList>& lists, bool biome_scoped) -> Expected<void, WorldGenDiagnostic> {
    for (const StageFeatureList& list : lists) {
      const usize stage_index = static_cast<usize>(list.stage);
      for (const ResourceRef& ref : list.features) {
        const auto placed_index = index_of_placed_feature(*resolved, ref.id);
        if (!placed_index) {
          return Expected<void, WorldGenDiagnostic>::failure(
              error(resolved->raw.id, "features.stages", "unknown placed feature: " + ref.id));
        }
        if (biome_scoped) {
          resolved->biome_features_by_stage[stage_index].push_back(placed_index.value());
        } else {
          resolved->dimension_features_by_stage[stage_index].push_back(placed_index.value());
        }
      }
    }
    return Expected<void, WorldGenDiagnostic>::success();
  };
  auto dimension_features = add_stage_refs(resolved->raw.features, false);
  if (!dimension_features) {
    return ResolvedDimensionResult::failure(dimension_features.error());
  }
  for (const ResolvedBiome& biome : resolved->biomes) {
    auto biome_features = add_stage_refs(biome.raw.features, true);
    if (!biome_features) {
      return ResolvedDimensionResult::failure(biome_features.error());
    }
  }

  for (const ResourceRef& ref : resolved->raw.structure_sets) {
    const StructureSetConfig* structure = find_by_id(registry.structure_sets, ref);
    if (structure == nullptr) {
      return ResolvedDimensionResult::failure(
          error(resolved->raw.id, "structure_sets", "unknown structure set: " + ref.id));
    }
    ResolvedStructureSet resolved_structure;
    resolved_structure.raw = *structure;
    for (const StructureEntryConfig& entry : structure->structures) {
      const auto feature_index = index_of_configured_feature(*resolved, entry.feature.id);
      if (!feature_index) {
        return ResolvedDimensionResult::failure(error(
            structure->id,
            "structures[].feature",
            "unknown configured feature: " + entry.feature.id));
      }
      if (!std::holds_alternative<BlueprintFeatureConfig>(
              resolved->configured_features[feature_index.value()].raw.config)) {
        return ResolvedDimensionResult::failure(error(
            structure->id,
            "structures[].feature",
            "structure sets must reference blueprint configured features"));
      }
      resolved_structure.radius_blocks = std::max(
          resolved_structure.radius_blocks,
          resolved->configured_features[feature_index.value()].radius_blocks);
      resolved_structure.structures.push_back({feature_index.value(), entry.weight});
    }
    for (const PlacementFilterConfig& filter : structure->filters) {
      auto resolved_filter = resolve_filter(filter, *resolved, block_registry, structure->id);
      if (!resolved_filter) {
        return ResolvedDimensionResult::failure(resolved_filter.error());
      }
      resolved_structure.filters.push_back(std::move(resolved_filter.value()));
    }
    resolved->structure_sets.push_back(std::move(resolved_structure));
  }

  if (resolved->biomes.empty()) {
    return ResolvedDimensionResult::failure(
        error(resolved->raw.id, "biome_source", "resolved dimension has no biomes"));
  }

  if (const auto water = resolve_block_variant(block_registry, "arvane:water"); water) {
    resolved->water_block = water.value();
  }

  return ResolvedDimensionResult::success(std::move(resolved));
}

} // namespace arv::world::worldgen
