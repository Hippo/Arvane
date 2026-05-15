#include "arv/world/worldgen/chunk_generator.hpp"

#include "arv/core/profiling.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/worldgen/blueprint_placement.hpp"
#include "arv/world/worldgen/seeds.hpp"
#include "arv/world/worldgen/structure_candidate.hpp"
#include "arv/world/worldgen/terrain_query.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace arv::world::worldgen {
namespace {

inline constexpr f32 BiomeDistanceEpsilon = 0.000001F;
inline constexpr f32 MissingClimateFieldPenalty = 0.35F;

class ClimateNoiseLattice2D {
public:
  ClimateNoiseLattice2D() = default;

  ClimateNoiseLattice2D(
      const NoiseSampler& sampler,
      i32 origin_x,
      i32 origin_z,
      i32 step)
      : step_(std::max(1, step)),
        base_x_(floor_div(origin_x, step_) * step_),
        base_z_(floor_div(origin_z, step_) * step_) {
    const i32 max_x = origin_x + ChunkSize - 1;
    const i32 max_z = origin_z + ChunkSize - 1;
    grid_side_x_ = ((max_x - base_x_) / step_) + 2;
    grid_side_z_ = ((max_z - base_z_) / step_) + 2;
    lattice_.resize(static_cast<usize>(grid_side_x_) * static_cast<usize>(grid_side_z_));

    for (i32 grid_z = 0; grid_z < grid_side_z_; ++grid_z) {
      for (i32 grid_x = 0; grid_x < grid_side_x_; ++grid_x) {
        lattice_[grid_index(grid_x, grid_z)] = sampler.sample_2d(
            static_cast<f32>(base_x_ + grid_x * step_),
            static_cast<f32>(base_z_ + grid_z * step_));
      }
    }
  }

  [[nodiscard]] f32 sample(i32 world_x, i32 world_z) const noexcept {
    if (lattice_.empty() || grid_side_x_ < 2 || grid_side_z_ < 2) {
      return 0.0F;
    }
    const i32 offset_x = world_x - base_x_;
    const i32 offset_z = world_z - base_z_;
    const i32 grid_x = std::clamp(offset_x / step_, 0, grid_side_x_ - 2);
    const i32 grid_z = std::clamp(offset_z / step_, 0, grid_side_z_ - 2);
    const f32 t_x =
        static_cast<f32>(offset_x - grid_x * step_) / static_cast<f32>(step_);
    const f32 t_z =
        static_cast<f32>(offset_z - grid_z * step_) / static_cast<f32>(step_);
    return lerp(
        lerp(
            lattice_[grid_index(grid_x, grid_z)],
            lattice_[grid_index(grid_x + 1, grid_z)],
            t_x),
        lerp(
            lattice_[grid_index(grid_x, grid_z + 1)],
            lattice_[grid_index(grid_x + 1, grid_z + 1)],
            t_x),
        t_z);
  }

private:
  [[nodiscard]] static f32 lerp(f32 a, f32 b, f32 t) noexcept {
    return a + (b - a) * t;
  }

  [[nodiscard]] usize grid_index(i32 grid_x, i32 grid_z) const noexcept {
    return static_cast<usize>(grid_x) +
           static_cast<usize>(grid_side_x_) * static_cast<usize>(grid_z);
  }

  i32 step_{DefaultLatticeStep};
  i32 base_x_{};
  i32 base_z_{};
  i32 grid_side_x_{};
  i32 grid_side_z_{};
  std::vector<f32> lattice_{};
};

struct ClimateFieldRuntime {
  NoiseSampler sampler{};
  ClimateNoiseLattice2D lattice{};
};

[[nodiscard]] usize stage_index(GenerationStage stage) noexcept {
  return static_cast<usize>(stage);
}

struct BiomeCandidate {
  usize entry_index{};
  usize biome_index{};
  f32 distance_sq{};
  f32 strength{};
};

[[nodiscard]] f32 climate_range_score(FloatRange range, f32 value) noexcept {
  const f32 center = (range.min + range.max) * 0.5F;
  const f32 half_width = std::max((range.max - range.min) * 0.5F, 0.001F);
  const f32 normalized_distance = (value - center) / half_width;
  return normalized_distance * normalized_distance;
}

[[nodiscard]] bool candidate_is_closer(
    const BiomeCandidate& lhs,
    const BiomeCandidate& rhs) noexcept {
  if (lhs.distance_sq + BiomeDistanceEpsilon < rhs.distance_sq) {
    return true;
  }
  if (rhs.distance_sq + BiomeDistanceEpsilon < lhs.distance_sq) {
    return false;
  }
  return lhs.entry_index < rhs.entry_index;
}

void insert_candidate(
    std::array<BiomeCandidate, BiomeBlendWeightCount>& selected,
    usize& selected_count,
    BiomeCandidate candidate) noexcept {
  usize insert_at = 0U;
  while (insert_at < selected_count &&
         !candidate_is_closer(candidate, selected[insert_at])) {
    ++insert_at;
  }
  if (insert_at >= BiomeBlendWeightCount) {
    return;
  }
  const usize new_count = std::min(selected_count + 1U, BiomeBlendWeightCount);
  for (usize index = new_count - 1U; index > insert_at; --index) {
    selected[index] = selected[index - 1U];
  }
  selected[insert_at] = candidate;
  selected_count = new_count;
}

[[nodiscard]] BiomeSample sample_biomes(
    const ResolvedDimension& dimension,
    const std::vector<f32>& climate_values) noexcept {
  BiomeSample sample;
  if (dimension.biomes.empty()) {
    return sample;
  }
  if (dimension.biome_source.entries.empty()) {
    sample.dominant_biome = 0U;
    sample.weight_count = 1U;
    sample.weights[0] = {0U, 1.0F};
    return sample;
  }

  BiomeCandidate best{
      0U,
      std::min(dimension.biome_source.entries.front().biome_index, dimension.biomes.size() - 1U),
      std::numeric_limits<f32>::max(),
      0.0F};
  std::array<BiomeCandidate, BiomeBlendWeightCount> selected{};
  usize selected_count = 0U;

  for (usize entry_index = 0; entry_index < dimension.biome_source.entries.size(); ++entry_index) {
    const ResolvedBiomeSourceEntry& entry = dimension.biome_source.entries[entry_index];
    f32 score = 0.0F;
    for (const ResolvedClimateRange& range : entry.resolved_climate) {
      if (range.field_index >= climate_values.size()) {
        score += 4.0F;
        continue;
      }
      score += climate_range_score(range.range, climate_values[range.field_index]);
    }
    if (!entry.resolved_climate.empty()) {
      score /= static_cast<f32>(entry.resolved_climate.size());
    }
    if (dimension.biome_source.fields.size() > entry.resolved_climate.size()) {
      const usize missing_fields =
          dimension.biome_source.fields.size() - entry.resolved_climate.size();
      score += MissingClimateFieldPenalty *
               static_cast<f32>(missing_fields) /
               static_cast<f32>(dimension.biome_source.fields.size());
    }

    BiomeCandidate candidate{
        entry_index,
        std::min(entry.biome_index, dimension.biomes.size() - 1U),
        score,
        1.0F / (0.05F + std::max(0.0F, score))};
    if (candidate_is_closer(candidate, best)) {
      best = candidate;
    }
    insert_candidate(selected, selected_count, candidate);
  }

  sample.dominant_biome = best.biome_index;
  sample.weight_count = selected_count;
  f32 total_strength = 0.0F;
  for (usize index = 0; index < selected_count; ++index) {
    total_strength += selected[index].strength;
  }
  const f32 inv_total = total_strength > BiomeDistanceEpsilon ? 1.0F / total_strength : 1.0F;
  for (usize index = 0; index < selected_count; ++index) {
    sample.weights[index] = {selected[index].biome_index, selected[index].strength * inv_total};
  }
  if (sample.weight_count == 0U) {
    sample.weight_count = 1U;
    sample.weights[0] = {sample.dominant_biome, 1.0F};
  }
  return sample;
}

[[nodiscard]] BiomeTerrainModifiers blend_terrain_modifiers(
    const ResolvedDimension& dimension,
    const BiomeSample& sample) noexcept {
  BiomeTerrainModifiers blended;
  blended.height_scale_multiplier = 0.0F;
  blended.roughness_multiplier = 0.0F;
  f32 total_weight = 0.0F;
  for (usize index = 0; index < sample.weight_count; ++index) {
    const BiomeWeight& weight = sample.weights[index];
    if (weight.biome_index >= dimension.biomes.size() || weight.weight <= 0.0F) {
      continue;
    }
    const BiomeTerrainModifiers& modifiers = dimension.biomes[weight.biome_index].raw.terrain;
    blended.base_height_offset += modifiers.base_height_offset * weight.weight;
    blended.height_scale_multiplier += modifiers.height_scale_multiplier * weight.weight;
    blended.roughness_multiplier += modifiers.roughness_multiplier * weight.weight;
    total_weight += weight.weight;
  }
  if (total_weight <= BiomeDistanceEpsilon) {
    return dimension.biomes[std::min(sample.dominant_biome, dimension.biomes.size() - 1U)]
        .raw.terrain;
  }
  const f32 inv_total = 1.0F / total_weight;
  blended.base_height_offset *= inv_total;
  blended.height_scale_multiplier *= inv_total;
  blended.roughness_multiplier *= inv_total;
  return blended;
}

[[nodiscard]] bool can_replace(
    voxel::BlockVariantID existing,
    const ResolvedReplaceRule& replace) noexcept {
  if (!replace.specified) {
    return true;
  }
  if (replace.only_air) {
    return existing == voxel::AirBlockVariantID;
  }
  if (!replace.blocks.empty()) {
    return std::find(replace.blocks.begin(), replace.blocks.end(), existing) != replace.blocks.end();
  }
  return false;
}

[[nodiscard]] bool is_replaceable_block(
    const ResolvedDimension& dimension,
    voxel::BlockVariantID existing) noexcept {
  return existing == voxel::AirBlockVariantID ||
         std::binary_search(
             dimension.replaceable_blocks.begin(),
             dimension.replaceable_blocks.end(),
             existing);
}

[[nodiscard]] bool filters_pass(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    ChunkCoord profile_chunk,
    const GeneratedChunkEditView& view,
    const std::vector<ResolvedPlacementFilter>& filters,
    BlockPos pos) noexcept {
  const i32 local_x = floor_mod(pos.value.x, ChunkSize);
  const i32 local_z = floor_mod(pos.value.z, ChunkSize);
  const usize column_index = voxel::local_column_index({local_x, local_z});
  const usize biome_index = profile.biome_by_column[column_index];
  const bool underwater = profile.surface_y_by_column[column_index] < dimension.raw.vertical.sea_level;
  const auto placement_block_at = [&](BlockPos target) noexcept {
    if (floor_div(target.value.x, ChunkSize) == view.coord.x &&
        floor_div(target.value.z, ChunkSize) == view.coord.z) {
      return view.get(target);
    }
    if (floor_div(target.value.x, ChunkSize) == profile_chunk.x &&
        floor_div(target.value.z, ChunkSize) == profile_chunk.z) {
      return terrain_block_at(dimension, profile, target);
    }
    return voxel::AirBlockVariantID;
  };
  for (const ResolvedPlacementFilter& filter : filters) {
    switch (filter.raw.type) {
      case PlacementFilterConfig::Type::Biome:
        if (!filter.biome_index || filter.biome_index.value() != biome_index) {
          return false;
        }
        break;
      case PlacementFilterConfig::Type::BiomeTag:
        if (!biome_has_tag(dimension.biomes[biome_index].raw, filter.raw.tag.id)) {
          return false;
        }
        break;
      case PlacementFilterConfig::Type::BlockBelow: {
        const voxel::BlockVariantID below =
            placement_block_at({{pos.value.x, pos.value.y - 1, pos.value.z}});
        if (std::find(filter.blocks.begin(), filter.blocks.end(), below) == filter.blocks.end()) {
          return false;
        }
        break;
      }
      case PlacementFilterConfig::Type::Replaceable:
        if (!is_replaceable_block(dimension, placement_block_at(pos))) {
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
    const ResolvedDimension& dimension,
    const ResolvedConfiguredFeature& feature,
    GeneratedChunkEditView& view,
    BlockPos pos,
    u64 seed,
    u64 salt,
    DeterministicRng& rng) {
  if (const auto* tree = std::get_if<SimpleTreeFeatureConfig>(&feature.raw.config)) {
    const i32 height = rng.range_i32(tree->trunk_height.min, tree->trunk_height.max);
    const i32 radius = rng.range_i32(tree->leaves_radius.min, tree->leaves_radius.max);
    const voxel::BlockVariantID trunk = feature.blocks[0];
    const voxel::BlockVariantID leaves = feature.blocks[1];
    for (i32 y = 0; y < height; ++y) {
      BlockPos target{{pos.value.x, pos.value.y + y, pos.value.z}};
      if (is_replaceable_block(dimension, view.get(target))) {
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
          if (is_replaceable_block(dimension, view.get(target))) {
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
    const StructureRotationY rotation =
        blueprint_feature->random_y_rotation
            ? structure_rotation_y_from_quarter_turns(rng.range_i32(0, 3))
            : StructureRotationY::Deg0;
    BlueprintPlacementInput input;
    input.feature = &feature;
    input.origin = pos;
    input.rotation = rotation;
    input.bounds = blueprint_world_bounds(feature.blueprint, pos, rotation);
    input.world_seed = seed;
    input.salt = salt;
    place_blueprint_intersection(input, view);
  }
}

void run_placed_feature(
    const ResolvedDimension& dimension,
    const TerrainProfile& origin_profile,
    GeneratedChunkEditView& view,
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
    const i32 y = placement_y(placed.raw.height, origin_profile, local_x, local_z, rng);
    BlockPos pos{{world_x, y, world_z}};
    if (!filters_pass(dimension, origin_profile, origin_chunk, view, placed.filters, pos)) {
      continue;
    }
    place_feature(dimension, feature, view, pos, seed, placed.raw.salt, rng);
  }
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
  climate_fields.reserve(dimension.biome_source.fields.size());
  const i32 climate_step = std::max(1, dimension.biome_source.raw.sample_scale_xz);
  for (const ResolvedClimateField& field : dimension.biome_source.fields) {
    ClimateFieldRuntime runtime;
    runtime.sampler = NoiseSampler{field.noise, dimension_seed};
    runtime.lattice = ClimateNoiseLattice2D{runtime.sampler, origin_x, origin_z, climate_step};
    climate_fields.push_back(std::move(runtime));
  }
  std::vector<f32> climate_values(climate_fields.size());

  NoiseSampler primary{dimension.terrain.primary_noise, dimension_seed};
  NoiseSampler detail{dimension.terrain.detail_noise, dimension_seed};
  const ChunkNoiseLattice2D primary_lattice{primary, origin_x, origin_z};
  const ChunkNoiseLattice2D detail_lattice{detail, origin_x, origin_z};

  TerrainProfile profile;
  profile.min_surface_y = std::numeric_limits<i32>::max();
  profile.max_surface_y = std::numeric_limits<i32>::min();
  for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
    const ChunkLocalColumn column = voxel::local_column(column_index);
    const i32 world_x = origin_x + column.x;
    const i32 world_z = origin_z + column.z;
    for (usize field_index = 0; field_index < climate_fields.size(); ++field_index) {
      climate_values[field_index] = climate_fields[field_index].lattice.sample(world_x, world_z);
    }
    const BiomeSample biome_sample = sample_biomes(dimension, climate_values);
    const usize biome_index =
        std::min(biome_sample.dominant_biome, dimension.biomes.size() - 1U);
    const BiomeTerrainModifiers modifiers =
        blend_terrain_modifiers(dimension, biome_sample);
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
    profile.biome_samples_by_column[column_index] = biome_sample;
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
            choose_surface_layers(dimension, profile, column_index);
        section.blocks[index] = layers == nullptr
                                    ? voxel::AirBlockVariantID
                                    : choose_terrain_surface_block(*layers, surface_y - world_y);
      } else if (world_y <= dimension.raw.vertical.sea_level) {
        section.blocks[index] = dimension.water_block;
      }
    }
    chunk.sections.emplace_back(std::move(section));
  }
  }

  GeneratedChunkEditView view{request.coord, min_chunk_y, max_chunk_y, &chunk.sections};

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
          const ChunkCoord origin_chunk{request.coord.x + dx, request.coord.z + dz};
          const TerrainProfile origin_profile =
              origin_chunk == request.coord
                  ? profile
                  : generate_terrain_profile(dimension, request.seed, origin_chunk);
          run_placed_feature(
              dimension,
              origin_profile,
              view,
              placed,
              request.seed,
              origin_chunk);
        }
      }
    }
    if (stage == GenerationStage::Structures) {
      const std::vector<StructureCandidate> structure_candidates =
          collect_structure_candidates_for_chunk(
              dimension,
              request.seed,
              request.coord,
              min_chunk_y,
              max_chunk_y);
      for (const StructureCandidate& candidate : structure_candidates) {
        const ResolvedStructureSet& structure_set =
            dimension.structure_sets[candidate.structure_set_index];
        const ResolvedConfiguredFeature& feature =
            dimension.configured_features[candidate.feature_index];
        BlueprintPlacementInput input;
        input.feature = &feature;
        input.origin = candidate.origin;
        input.rotation = candidate.rotation;
        input.bounds = candidate.bounds;
        input.world_seed = request.seed;
        input.salt = structure_set.raw.salt;
        place_blueprint_intersection(input, view);
      }
    }
  }
  }

  return GeneratedChunkResult::success(std::move(chunk));
}

} // namespace arv::world::worldgen
