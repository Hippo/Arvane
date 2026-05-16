#include "arv/world/worldgen/terrain_query.hpp"

#include <algorithm>

namespace arv::world::worldgen {
namespace {

[[nodiscard]] bool surface_predicate_matches(
    const ResolvedSurfaceRule& rule,
    const ResolvedDimension& dimension,
    usize biome_index,
    i32 surface_y,
    f32 slope,
    bool underwater) noexcept {
  const SurfaceRulePredicate& predicate = rule.when;
  const i32 sea_level = dimension.raw.vertical.sea_level;
  if (predicate.above_sea_level.has_value() &&
      predicate.above_sea_level.value() != (surface_y >= sea_level)) {
    return false;
  }
  if (predicate.below_sea_level.has_value() &&
      predicate.below_sea_level.value() != (surface_y < sea_level)) {
    return false;
  }
  if (predicate.underwater.has_value() &&
      predicate.underwater.value() != underwater) {
    return false;
  }
  if (predicate.biome.has_value() &&
      (!rule.biome_index.has_value() || rule.biome_index.value() != biome_index)) {
    return false;
  }
  if (predicate.biome_tag.has_value() &&
      (!rule.biome_tag_index.has_value() ||
       !biome_has_resolved_tag(dimension.biomes[biome_index], rule.biome_tag_index.value()))) {
    return false;
  }
  if (predicate.biome_tag_not.has_value() &&
      rule.biome_tag_not_index.has_value() &&
      biome_has_resolved_tag(dimension.biomes[biome_index], rule.biome_tag_not_index.value())) {
    return false;
  }
  if (predicate.slope_less_than.has_value() && !(slope < predicate.slope_less_than.value())) {
    return false;
  }
  if (predicate.slope_greater_than.has_value() && !(slope > predicate.slope_greater_than.value())) {
    return false;
  }
  if (predicate.height_above.has_value() && !(surface_y > predicate.height_above.value())) {
    return false;
  }
  if (predicate.height_below.has_value() && !(surface_y < predicate.height_below.value())) {
    return false;
  }
  return true;
}

} // namespace

bool biome_has_tag(const BiomeConfig& biome, std::string_view tag) noexcept {
  return std::any_of(biome.tags.begin(), biome.tags.end(), [tag](const ResourceRef& ref) {
    return ref.id == tag;
  });
}

bool biome_has_resolved_tag(const ResolvedBiome& biome, usize tag_index) noexcept {
  return std::binary_search(
      biome.tag_indices.begin(),
      biome.tag_indices.end(),
      tag_index);
}

const std::vector<ResolvedSurfaceLayer>* choose_surface_layers(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    usize column_index) noexcept {
  const usize biome_index = profile.biome_by_column[column_index];
  const i32 surface_y = profile.surface_y_by_column[column_index];
  const bool underwater = surface_y < dimension.raw.vertical.sea_level;
  const f32 slope = profile.slope_by_column[column_index];
  const auto try_rule_set = [&](usize rule_set_index) -> const std::vector<ResolvedSurfaceLayer>* {
    if (rule_set_index >= dimension.surface_rule_sets.size()) {
      return nullptr;
    }
    const ResolvedSurfaceRuleSet& set = dimension.surface_rule_sets[rule_set_index];
    for (const ResolvedSurfaceRule& rule : set.rules) {
      if (!rule.has_predicate ||
          surface_predicate_matches(rule, dimension, biome_index, surface_y, slope, underwater)) {
        return &rule.layers;
      }
    }
    return nullptr;
  };

  const ResolvedBiome& biome = dimension.biomes[biome_index];
  if (biome.surface_rule_set_index.has_value()) {
    if (const auto* layers = try_rule_set(biome.surface_rule_set_index.value());
        layers != nullptr) {
      return layers;
    }
  }
  if (dimension.dimension_surface_rule_set.has_value()) {
    if (const auto* layers = try_rule_set(dimension.dimension_surface_rule_set.value());
        layers != nullptr) {
      return layers;
    }
  }
  return nullptr;
}

voxel::BlockVariantID choose_terrain_surface_block(
    const std::vector<ResolvedSurfaceLayer>& layers,
    i32 depth_from_surface) noexcept {
  i32 consumed = 0;
  for (const ResolvedSurfaceLayer& layer : layers) {
    if (layer.remaining) {
      return layer.block;
    }
    consumed += layer.depth;
    if (depth_from_surface < consumed) {
      return layer.block;
    }
  }
  return layers.empty() ? voxel::AirBlockVariantID : layers.back().block;
}

voxel::BlockVariantID terrain_block_at(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    BlockPos pos) noexcept {
  const i32 local_x = floor_mod(pos.value.x, ChunkSize);
  const i32 local_z = floor_mod(pos.value.z, ChunkSize);
  const usize column_index = voxel::local_column_index({local_x, local_z});
  const i32 surface_y = profile.surface_y_by_column[column_index];
  if (pos.value.y <= surface_y) {
    const std::vector<ResolvedSurfaceLayer>* layers =
        choose_surface_layers(dimension, profile, column_index);
    return layers == nullptr
               ? voxel::AirBlockVariantID
               : choose_terrain_surface_block(*layers, surface_y - pos.value.y);
  }
  if (pos.value.y <= dimension.raw.vertical.sea_level) {
    return dimension.water_block;
  }
  return voxel::AirBlockVariantID;
}

bool terrain_filters_pass(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    const std::vector<ResolvedPlacementFilter>& filters,
    BlockPos pos) noexcept {
  const i32 local_x = floor_mod(pos.value.x, ChunkSize);
  const i32 local_z = floor_mod(pos.value.z, ChunkSize);
  const usize column_index = voxel::local_column_index({local_x, local_z});
  const usize biome_index = profile.biome_by_column[column_index];
  const bool underwater = profile.surface_y_by_column[column_index] < dimension.raw.vertical.sea_level;
  for (const ResolvedPlacementFilter& filter : filters) {
    switch (filter.raw.type) {
      case PlacementFilterConfig::Type::Biome:
        if (!filter.biome_index || filter.biome_index.value() != biome_index) {
          return false;
        }
        break;
      case PlacementFilterConfig::Type::BiomeTag:
        if (!filter.biome_tag_index.has_value() ||
            !biome_has_resolved_tag(
                dimension.biomes[biome_index],
                filter.biome_tag_index.value())) {
          return false;
        }
        break;
      case PlacementFilterConfig::Type::BlockBelow: {
        const voxel::BlockVariantID below =
            terrain_block_at(dimension, profile, {{pos.value.x, pos.value.y - 1, pos.value.z}});
        if (std::find(filter.blocks.begin(), filter.blocks.end(), below) == filter.blocks.end()) {
          return false;
        }
        break;
      }
      case PlacementFilterConfig::Type::Replaceable:
        if (terrain_block_at(dimension, profile, pos) != voxel::AirBlockVariantID) {
          return false;
        }
        break;
      case PlacementFilterConfig::Type::SlopeLessThan:
        if (!(profile.slope_by_column[column_index] < filter.raw.value)) {
          return false;
        }
        break;
      case PlacementFilterConfig::Type::SlopeGreaterThan:
        if (!(profile.slope_by_column[column_index] > filter.raw.value)) {
          return false;
        }
        break;
      case PlacementFilterConfig::Type::Underwater:
        if (underwater != filter.raw.bool_value) {
          return false;
        }
        break;
    }
  }
  return true;
}

} // namespace arv::world::worldgen
