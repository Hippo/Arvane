#include "arv/world/worldgen/chunk_generator.hpp"

#include "arv/core/profiling.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/worldgen/seeds.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arv::world::worldgen {
namespace {

struct ClimateFieldRuntime {
  std::string name{};
  NoiseSampler sampler{};
  ChunkNoiseLattice2D lattice{};
};

struct ChunkEditView {
  ChunkCoord coord{};
  i32 min_chunk_y{};
  i32 max_chunk_y{};
  std::vector<GeneratedChunkSection>* sections{};

  [[nodiscard]] std::optional<usize> section_index(i32 chunk_y) const noexcept {
    if (chunk_y < min_chunk_y || chunk_y > max_chunk_y) {
      return std::nullopt;
    }
    return static_cast<usize>(chunk_y - min_chunk_y);
  }

  [[nodiscard]] voxel::BlockVariantID get(BlockPos pos) const noexcept {
    if (floor_div(pos.value.x, ChunkSize) != coord.x ||
        floor_div(pos.value.z, ChunkSize) != coord.z) {
      return voxel::AirBlockVariantID;
    }
    const i32 chunk_y = floor_div(pos.value.y, ChunkSize);
    const std::optional<usize> section = section_index(chunk_y);
    if (!section || section.value() >= sections->size()) {
      return voxel::AirBlockVariantID;
    }
    const SectionLocalPos local = block_to_local(pos);
    return (*sections)[section.value()].blocks[voxel::local_index(local)];
  }

  void set(BlockPos pos, voxel::BlockVariantID block) noexcept {
    if (floor_div(pos.value.x, ChunkSize) != coord.x ||
        floor_div(pos.value.z, ChunkSize) != coord.z) {
      return;
    }
    const i32 chunk_y = floor_div(pos.value.y, ChunkSize);
    const std::optional<usize> section = section_index(chunk_y);
    if (!section || section.value() >= sections->size()) {
      return;
    }
    const SectionLocalPos local = block_to_local(pos);
    (*sections)[section.value()].blocks[voxel::local_index(local)] = block;
  }
};

[[nodiscard]] usize stage_index(GenerationStage stage) noexcept {
  return static_cast<usize>(stage);
}

[[nodiscard]] bool has_tag(const BiomeConfig& biome, std::string_view tag) noexcept {
  return std::any_of(biome.tags.begin(), biome.tags.end(), [tag](const ResourceRef& ref) {
    return ref.id == tag;
  });
}

[[nodiscard]] usize select_biome(
    const ResolvedDimension& dimension,
    const std::vector<ClimateFieldRuntime>& fields,
    i32 local_x,
    i32 local_z) noexcept {
  usize best_biome = dimension.biome_source.entries.empty()
                         ? 0U
                         : dimension.biome_source.entries.front().biome_index;
  usize best_score = 0U;
  bool found = false;
  for (const ResolvedBiomeSourceEntry& entry : dimension.biome_source.entries) {
    bool matches = true;
    for (const ClimateRange& range : entry.climate) {
      const auto field_iter = std::find_if(
          fields.begin(),
          fields.end(),
          [&](const ClimateFieldRuntime& field) {
            return field.name == range.field;
          });
      if (field_iter == fields.end()) {
        matches = false;
        break;
      }
      const f32 value = field_iter->lattice.sample(local_x, local_z);
      if (!range.range.contains(value)) {
        matches = false;
        break;
      }
    }
    if (matches && (!found || entry.climate.size() > best_score)) {
      found = true;
      best_score = entry.climate.size();
      best_biome = entry.biome_index;
    }
  }
  return std::min(best_biome, dimension.biomes.size() - 1U);
}

[[nodiscard]] bool surface_predicate_matches(
    const SurfaceRulePredicate& predicate,
    const ResolvedDimension& dimension,
    usize biome_index,
    i32 surface_y,
    f32 slope,
    bool underwater) noexcept {
  const i32 sea_level = dimension.raw.vertical.sea_level;
  const BiomeConfig& biome = dimension.biomes[biome_index].raw;
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
  if (predicate.biome.has_value() && predicate.biome->id != biome.id) {
    return false;
  }
  if (predicate.biome_tag.has_value() && !has_tag(biome, predicate.biome_tag->id)) {
    return false;
  }
  if (predicate.biome_tag_not.has_value() && has_tag(biome, predicate.biome_tag_not->id)) {
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

[[nodiscard]] const std::vector<ResolvedSurfaceLayer>* choose_layers(
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
          surface_predicate_matches(rule.when, dimension, biome_index, surface_y, slope, underwater)) {
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

[[nodiscard]] voxel::BlockVariantID choose_surface_block(
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

[[nodiscard]] bool can_replace(
    voxel::BlockVariantID existing,
    const ResolvedReplaceRule& replace) noexcept {
  if (replace.only_air) {
    return existing == voxel::AirBlockVariantID;
  }
  if (!replace.blocks.empty()) {
    return std::find(replace.blocks.begin(), replace.blocks.end(), existing) != replace.blocks.end();
  }
  return false;
}

[[nodiscard]] bool filters_pass(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    const ChunkEditView& view,
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
        if (!has_tag(dimension.biomes[biome_index].raw, filter.raw.tag.id)) {
          return false;
        }
        break;
      case PlacementFilterConfig::Type::BlockBelow: {
        const voxel::BlockVariantID below =
            view.get({{pos.value.x, pos.value.y - 1, pos.value.z}});
        if (std::find(filter.blocks.begin(), filter.blocks.end(), below) == filter.blocks.end()) {
          return false;
        }
        break;
      }
      case PlacementFilterConfig::Type::Replaceable:
        if (view.get(pos) != voxel::AirBlockVariantID) {
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

[[nodiscard]] i32 placement_count(
    const PlacementCountConfig& count,
    DeterministicRng& rng) noexcept {
  switch (count.type) {
    case PlacementCountConfig::Type::ChancePerChunk:
      return rng.chance(count.chance) ? 1 : 0;
    case PlacementCountConfig::Type::Uniform:
      return rng.range_i32(count.min, count.max);
  }
  return 0;
}

[[nodiscard]] i32 placement_y(
    const PlacementHeightConfig& height,
    const TerrainProfile& profile,
    i32 local_x,
    i32 local_z,
    DeterministicRng& rng) noexcept {
  switch (height.type) {
    case PlacementHeightConfig::Type::Surface:
      return profile.surface_y_by_column[voxel::local_column_index({local_x, local_z})] + 1;
    case PlacementHeightConfig::Type::Uniform:
      return rng.range_i32(height.min_y, height.max_y);
    case PlacementHeightConfig::Type::Trapezoid: {
      const i32 first = rng.range_i32(height.min_y, height.max_y);
      const i32 second = rng.range_i32(height.min_y, height.max_y);
      return (first + second + height.peak_y) / 3;
    }
  }
  return 0;
}

void place_feature(
    const ResolvedConfiguredFeature& feature,
    ChunkEditView& view,
    BlockPos pos,
    DeterministicRng& rng) {
  if (const auto* tree = std::get_if<SimpleTreeFeatureConfig>(&feature.raw.config)) {
    const i32 height = rng.range_i32(tree->trunk_height.min, tree->trunk_height.max);
    const i32 radius = rng.range_i32(tree->leaves_radius.min, tree->leaves_radius.max);
    const voxel::BlockVariantID trunk = feature.blocks[0];
    const voxel::BlockVariantID leaves = feature.blocks[1];
    for (i32 y = 0; y < height; ++y) {
      BlockPos target{{pos.value.x, pos.value.y + y, pos.value.z}};
      if (view.get(target) == voxel::AirBlockVariantID) {
        view.set(target, trunk);
      }
    }
    const i32 top = pos.value.y + height;
    for (i32 y = -radius; y <= radius; ++y) {
      for (i32 z = -radius; z <= radius; ++z) {
        for (i32 x = -radius; x <= radius; ++x) {
          if (std::abs(x) + std::abs(y) + std::abs(z) > radius + 1) {
            continue;
          }
          BlockPos target{{pos.value.x + x, top + y, pos.value.z + z}};
          if (view.get(target) == voxel::AirBlockVariantID) {
            view.set(target, leaves);
          }
        }
      }
    }
  } else if (const auto* patch = std::get_if<PatchFeatureConfig>(&feature.raw.config)) {
    for (i32 attempt = 0; attempt < patch->tries; ++attempt) {
      BlockPos target{{
          pos.value.x + rng.range_i32(-patch->radius_xz, patch->radius_xz),
          pos.value.y + rng.range_i32(-patch->radius_y, patch->radius_y),
          pos.value.z + rng.range_i32(-patch->radius_xz, patch->radius_xz)}};
      const voxel::BlockVariantID below =
          view.get({{target.value.x, target.value.y - 1, target.value.z}});
      if (!feature.support_blocks.empty() &&
          std::find(feature.support_blocks.begin(), feature.support_blocks.end(), below) ==
              feature.support_blocks.end()) {
        continue;
      }
      if (can_replace(view.get(target), feature.replace)) {
        view.set(target, feature.blocks.front());
      }
    }
  } else if (const auto* disk = std::get_if<DiskFeatureConfig>(&feature.raw.config)) {
    const i32 radius = rng.range_i32(disk->radius.min, disk->radius.max);
    for (i32 z = -radius; z <= radius; ++z) {
      for (i32 x = -radius; x <= radius; ++x) {
        if ((x * x + z * z) > radius * radius) {
          continue;
        }
        for (i32 y = 0; y < disk->depth; ++y) {
          BlockPos target{{pos.value.x + x, pos.value.y - y, pos.value.z + z}};
          if (can_replace(view.get(target), feature.replace)) {
            view.set(target, feature.blocks.front());
          }
        }
      }
    }
  } else if (const auto* ore = std::get_if<OreVeinFeatureConfig>(&feature.raw.config)) {
    const i32 count = rng.range_i32(ore->vein_size.min, ore->vein_size.max);
    BlockPos cursor = pos;
    for (i32 index = 0; index < count; ++index) {
      if (can_replace(view.get(cursor), feature.replace)) {
        view.set(cursor, feature.blocks.front());
      }
      cursor.value.x += rng.range_i32(-1, 1);
      cursor.value.y += rng.range_i32(-1, 1);
      cursor.value.z += rng.range_i32(-1, 1);
    }
  } else if (std::holds_alternative<SingleBlockFeatureConfig>(feature.raw.config)) {
    const voxel::BlockVariantID below = view.get({{pos.value.x, pos.value.y - 1, pos.value.z}});
    if (!feature.support_blocks.empty() &&
        std::find(feature.support_blocks.begin(), feature.support_blocks.end(), below) ==
            feature.support_blocks.end()) {
      return;
    }
    if (can_replace(view.get(pos), feature.replace)) {
      view.set(pos, feature.blocks.front());
    }
  } else if (const auto* blueprint_feature = std::get_if<BlueprintFeatureConfig>(&feature.raw.config)) {
    (void)blueprint_feature;
    const blueprint::Blueprint& blueprint = feature.blueprint;
    for (i32 y = 0; y < blueprint.size.value.y; ++y) {
      for (i32 z = 0; z < blueprint.size.value.z; ++z) {
        for (i32 x = 0; x < blueprint.size.value.x; ++x) {
          const usize index = static_cast<usize>(x) +
                              static_cast<usize>(blueprint.size.value.x) *
                                  (static_cast<usize>(z) +
                                   static_cast<usize>(blueprint.size.value.z) *
                                       static_cast<usize>(y));
          if (index >= blueprint.blocks.size()) {
            continue;
          }
          const u32 palette_index = blueprint.blocks[index];
          if (palette_index >= feature.blueprint_palette.size()) {
            continue;
          }
          const voxel::BlockVariantID block = feature.blueprint_palette[palette_index];
          if (block == voxel::AirBlockVariantID) {
            continue;
          }
          BlockPos target{{
              pos.value.x + x - blueprint.origin.value.x,
              pos.value.y + y - blueprint.origin.value.y,
              pos.value.z + z - blueprint.origin.value.z}};
          if (blueprint_feature->integrity < 1.0F && !rng.chance(blueprint_feature->integrity)) {
            continue;
          }
          view.set(target, block);
        }
      }
    }
  }
}

void run_placed_feature(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    ChunkEditView& view,
    const ResolvedPlacedFeature& placed,
    u64 seed,
    ChunkCoord origin_chunk) {
  const ResolvedConfiguredFeature& feature = dimension.configured_features[placed.feature_index];
  DeterministicRng rng{coordinate_seed(
      seed,
      origin_chunk.x,
      static_cast<i32>(placed.raw.salt & 0x7FFFFFFF),
      origin_chunk.z,
      placed.raw.salt)};
  const i32 count = placement_count(placed.raw.count, rng);
  for (i32 attempt = 0; attempt < count; ++attempt) {
    const i32 local_x = rng.range_i32(0, ChunkSize - 1);
    const i32 local_z = rng.range_i32(0, ChunkSize - 1);
    const i32 world_x = origin_chunk.x * ChunkSize + local_x;
    const i32 world_z = origin_chunk.z * ChunkSize + local_z;
    if (origin_chunk != view.coord) {
      const i32 current_local_x = world_x - view.coord.x * ChunkSize;
      const i32 current_local_z = world_z - view.coord.z * ChunkSize;
      if (current_local_x < -feature.radius_blocks || current_local_x >= ChunkSize + feature.radius_blocks ||
          current_local_z < -feature.radius_blocks || current_local_z >= ChunkSize + feature.radius_blocks) {
        continue;
      }
    }
    if (floor_div(world_x, ChunkSize) != view.coord.x ||
        floor_div(world_z, ChunkSize) != view.coord.z) {
      continue;
    }
    const i32 y = placement_y(placed.raw.height, profile, local_x, local_z, rng);
    BlockPos pos{{world_x, y, world_z}};
    if (!filters_pass(dimension, profile, view, placed.filters, pos)) {
      continue;
    }
    place_feature(feature, view, pos, rng);
  }
}

void run_structure_set(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    ChunkEditView& view,
    const ResolvedStructureSet& structure_set,
    u64 seed,
    ChunkCoord cell_chunk) {
  const i32 average = std::max(1, structure_set.raw.placement.average_distance_chunks);
  const i32 separation = std::clamp(
      structure_set.raw.placement.separation_chunks,
      0,
      std::max(0, average - 1));
  const i32 region_x = floor_div(cell_chunk.x, average);
  const i32 region_z = floor_div(cell_chunk.z, average);
  DeterministicRng rng{coordinate_seed(
      seed,
      region_x,
      static_cast<i32>(structure_set.raw.salt & 0x7FFFFFFF),
      region_z,
      structure_set.raw.salt)};
  const i32 span = std::max(1, average - separation);
  const ChunkCoord candidate{
      region_x * average + rng.range_i32(0, span - 1),
      region_z * average + rng.range_i32(0, span - 1)};
  if (candidate != cell_chunk || candidate != view.coord || structure_set.structures.empty()) {
    return;
  }
  i32 total_weight = 0;
  for (const ResolvedStructureEntry& entry : structure_set.structures) {
    total_weight += std::max(0, entry.weight);
  }
  if (total_weight <= 0) {
    return;
  }
  i32 pick = rng.range_i32(1, total_weight);
  const ResolvedStructureEntry* selected = &structure_set.structures.front();
  for (const ResolvedStructureEntry& entry : structure_set.structures) {
    pick -= std::max(0, entry.weight);
    if (pick <= 0) {
      selected = &entry;
      break;
    }
  }
  const i32 local_x = rng.range_i32(0, ChunkSize - 1);
  const i32 local_z = rng.range_i32(0, ChunkSize - 1);
  const usize column_index = voxel::local_column_index({local_x, local_z});
  BlockPos pos{{
      view.coord.x * ChunkSize + local_x,
      profile.surface_y_by_column[column_index] + 1,
      view.coord.z * ChunkSize + local_z}};
  if (!filters_pass(dimension, profile, view, structure_set.filters, pos)) {
    return;
  }
  place_feature(dimension.configured_features[selected->feature_index], view, pos, rng);
}

} // namespace

TerrainProfile generate_terrain_profile(
    const ResolvedDimension& dimension,
    u64 seed,
    ChunkCoord coord) {
  ARV_PROFILE_ZONE("Worldgen terrain profile");
  const u64 dimension_seed = salted_seed(seed, dimension.raw.seed_salt, dimension.raw.version);
  const i32 origin_x = coord.x * ChunkSize;
  const i32 origin_z = coord.z * ChunkSize;

  std::vector<ClimateFieldRuntime> climate_fields;
  climate_fields.reserve(dimension.biome_source.noise.fields.size());
  for (const auto& [name, field] : dimension.biome_source.noise.fields) {
    ClimateFieldRuntime runtime;
    runtime.name = name;
    runtime.sampler = NoiseSampler{field, dimension_seed};
    runtime.lattice = ChunkNoiseLattice2D{runtime.sampler, origin_x, origin_z};
    climate_fields.push_back(std::move(runtime));
  }

  NoiseSampler primary{dimension.terrain.primary_noise, dimension_seed};
  NoiseSampler detail{dimension.terrain.detail_noise, dimension_seed};
  const ChunkNoiseLattice2D primary_lattice{primary, origin_x, origin_z};
  const ChunkNoiseLattice2D detail_lattice{detail, origin_x, origin_z};

  TerrainProfile profile;
  profile.min_surface_y = std::numeric_limits<i32>::max();
  profile.max_surface_y = std::numeric_limits<i32>::min();
  for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
    const ChunkLocalColumn column = voxel::local_column(column_index);
    const usize biome_index = select_biome(dimension, climate_fields, column.x, column.z);
    const BiomeTerrainModifiers& modifiers = dimension.biomes[biome_index].raw.terrain;
    const f32 base = static_cast<f32>(dimension.terrain.height.base) +
                     (dimension.terrain.biome_modifiers.base_height_offset
                          ? modifiers.base_height_offset
                          : 0.0F);
    const f32 scale = static_cast<f32>(dimension.terrain.height.scale) *
                      (dimension.terrain.biome_modifiers.height_scale_multiplier
                           ? modifiers.height_scale_multiplier
                           : 1.0F);
    const f32 roughness = static_cast<f32>(dimension.terrain.height.detail_scale) *
                          (dimension.terrain.biome_modifiers.roughness_multiplier
                               ? modifiers.roughness_multiplier
                               : 1.0F);
    const f32 height =
        base + primary_lattice.sample(column.x, column.z) * scale +
        detail_lattice.sample(column.x, column.z) * roughness;
    const i32 surface_y = std::clamp(
        static_cast<i32>(std::floor(height + 0.5F)),
        dimension.raw.vertical.min_y,
        dimension.raw.vertical.max_y);
    profile.surface_y_by_column[column_index] = surface_y;
    profile.biome_by_column[column_index] = biome_index;
    profile.min_surface_y = std::min(profile.min_surface_y, surface_y);
    profile.max_surface_y = std::max(profile.max_surface_y, surface_y);
  }

  for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
    const ChunkLocalColumn column = voxel::local_column(column_index);
    const i32 center = profile.surface_y_by_column[column_index];
    const i32 x0 = std::max(0, column.x - 1);
    const i32 x1 = std::min(ChunkSize - 1, column.x + 1);
    const i32 z0 = std::max(0, column.z - 1);
    const i32 z1 = std::min(ChunkSize - 1, column.z + 1);
    const i32 dx = std::max(
        std::abs(center - profile.surface_y_by_column[voxel::local_column_index({x0, column.z})]),
        std::abs(center - profile.surface_y_by_column[voxel::local_column_index({x1, column.z})]));
    const i32 dz = std::max(
        std::abs(center - profile.surface_y_by_column[voxel::local_column_index({column.x, z0})]),
        std::abs(center - profile.surface_y_by_column[voxel::local_column_index({column.x, z1})]));
    profile.slope_by_column[column_index] =
        static_cast<f32>(std::max(dx, dz)) / static_cast<f32>(ChunkSize);
  }

  return profile;
}

GeneratedChunkResult generate_resolved_chunk(
    const ChunkGenerationRequest& request,
    const ResolvedDimension& dimension) {
  ARV_PROFILE_ZONE("Resolved chunk generation");
  GeneratedChunk chunk;
  chunk.world_id = request.world_id;
  chunk.seed = request.seed;
  chunk.generator_version = dimension.raw.version;
  chunk.coord = request.coord;
  const i32 min_chunk_y = std::min(request.min_chunk_y, request.max_chunk_y);
  const i32 max_chunk_y = std::max(request.min_chunk_y, request.max_chunk_y);
  chunk.sections.reserve(static_cast<usize>(max_chunk_y - min_chunk_y + 1));

  const TerrainProfile profile = generate_terrain_profile(dimension, request.seed, request.coord);
  chunk.surface_y_by_column = profile.surface_y_by_column;

  {
  ARV_PROFILE_ZONE("Worldgen base fill and surfaces");
  for (i32 chunk_y = min_chunk_y; chunk_y <= max_chunk_y; ++chunk_y) {
    GeneratedChunkSection section;
    section.coord = chunk_section(request.coord, chunk_y);
    section.blocks.fill(voxel::AirBlockVariantID);

    const i32 section_min_y = chunk_y * ChunkSize;
    const i32 section_max_y = section_min_y + ChunkSize - 1;
    if (section_max_y < dimension.raw.vertical.min_y ||
        section_min_y > dimension.raw.vertical.max_y ||
        section_min_y > std::max(profile.max_surface_y, dimension.raw.vertical.sea_level)) {
      chunk.sections.emplace_back(std::move(section));
      continue;
    }

    for (usize index = 0; index < voxel::ChunkVolume; ++index) {
      const SectionLocalPos local = voxel::local_pos(index);
      const usize column_index = voxel::local_column_index({local.value.x, local.value.z});
      const i32 world_y = section_min_y + local.value.y;
      if (world_y < dimension.raw.vertical.min_y || world_y > dimension.raw.vertical.max_y) {
        continue;
      }
      const i32 surface_y = profile.surface_y_by_column[column_index];
      if (world_y <= surface_y) {
        const std::vector<ResolvedSurfaceLayer>* layers =
            choose_layers(dimension, profile, column_index);
        section.blocks[index] = layers == nullptr
                                    ? voxel::AirBlockVariantID
                                    : choose_surface_block(*layers, surface_y - world_y);
      } else if (world_y <= dimension.raw.vertical.sea_level) {
        section.blocks[index] = dimension.water_block;
      }
    }
    chunk.sections.emplace_back(std::move(section));
  }
  }

  ChunkEditView view{request.coord, min_chunk_y, max_chunk_y, &chunk.sections};

  {
  ARV_PROFILE_ZONE("Worldgen carvers");
  for (const CarverConfig& carver : dimension.carvers) {
    if (!carver.enabled) {
      continue;
    }
    const NoiseSampler sampler{carver.noise, salted_seed(request.seed, dimension.raw.seed_salt)};
    for (GeneratedChunkSection& section : chunk.sections) {
      const i32 section_min_y = section.coord.value.y * ChunkSize;
      const i32 section_max_y = section_min_y + ChunkSize - 1;
      if (section_max_y < carver.y_range.min || section_min_y > carver.y_range.max) {
        continue;
      }
      for (usize index = 0; index < voxel::ChunkVolume; ++index) {
        if (section.blocks[index] == voxel::AirBlockVariantID ||
            section.blocks[index] == dimension.water_block) {
          continue;
        }
        const SectionLocalPos local = voxel::local_pos(index);
        const usize column_index = voxel::local_column_index({local.value.x, local.value.z});
        const i32 world_x = request.coord.x * ChunkSize + local.value.x;
        const i32 world_y = section_min_y + local.value.y;
        const i32 world_z = request.coord.z * ChunkSize + local.value.z;
        if (world_y < carver.y_range.min || world_y > carver.y_range.max ||
            world_y > profile.surface_y_by_column[column_index] - carver.surface_margin) {
          continue;
        }
        if (sampler.sample_3d(
                static_cast<f32>(world_x),
                static_cast<f32>(world_y),
                static_cast<f32>(world_z)) > carver.threshold) {
          section.blocks[index] = voxel::AirBlockVariantID;
        }
      }
    }
  }
  }

  {
  ARV_PROFILE_ZONE("Worldgen features and structures");
  for (GenerationStage stage : StageOrder) {
    const usize index = stage_index(stage);
    std::vector<usize> feature_indices = dimension.dimension_features_by_stage[index];
    feature_indices.insert(
        feature_indices.end(),
        dimension.biome_features_by_stage[index].begin(),
        dimension.biome_features_by_stage[index].end());
    std::sort(feature_indices.begin(), feature_indices.end());
    feature_indices.erase(
        std::unique(feature_indices.begin(), feature_indices.end()),
        feature_indices.end());
    for (const usize feature_index : feature_indices) {
      const ResolvedPlacedFeature& placed = dimension.placed_features[feature_index];
      const i32 radius_chunks = std::max(0, (placed.radius_blocks + ChunkSize - 1) / ChunkSize);
      for (i32 dz = -radius_chunks; dz <= radius_chunks; ++dz) {
        for (i32 dx = -radius_chunks; dx <= radius_chunks; ++dx) {
          run_placed_feature(
              dimension,
              profile,
              view,
              placed,
              request.seed,
              {request.coord.x + dx, request.coord.z + dz});
        }
      }
    }
    if (stage == GenerationStage::Structures) {
      for (const ResolvedStructureSet& structure_set : dimension.structure_sets) {
        run_structure_set(dimension, profile, view, structure_set, request.seed, request.coord);
      }
    }
  }
  }

  return GeneratedChunkResult::success(std::move(chunk));
}

} // namespace arv::world::worldgen
