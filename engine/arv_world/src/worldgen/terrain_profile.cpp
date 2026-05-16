#include "arv/world/worldgen/chunk_generator.hpp"

#include "arv/core/profiling.hpp"
#include "arv/world/worldgen/noise.hpp"
#include "arv/world/worldgen/seeds.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace arv::world::worldgen {
namespace {

inline constexpr f32 BiomeDistanceEpsilon = 0.000001F;
inline constexpr f32 MissingClimateFieldPenalty = 0.35F;
inline constexpr i32 ExpandedTerrainBorder = 4;
inline constexpr i32 ExpandedTerrainSide = ChunkSize + ExpandedTerrainBorder * 2;
inline constexpr usize ExpandedTerrainArea =
    static_cast<usize>(ExpandedTerrainSide * ExpandedTerrainSide);

[[nodiscard]] f32 lerp(f32 a, f32 b, f32 t) noexcept {
  return a + (b - a) * t;
}

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
  f32 scale{1.0F};
  f32 offset{};
};

struct TerrainFieldRuntime {
  NoiseSampler sampler{};
  ChunkNoiseLattice2D lattice{};
  bool enabled{};
  bool direct_samples{};

  [[nodiscard]] f32 sample(
      i32 world_x,
      i32 world_z,
      i32 local_x,
      i32 local_z,
      f32 warp_x,
      f32 warp_z) const noexcept {
    if (!enabled) {
      return 0.0F;
    }
    if (direct_samples) {
      return sampler.sample_2d(
          static_cast<f32>(world_x) + warp_x,
          static_cast<f32>(world_z) + warp_z);
    }
    return lattice.sample(local_x, local_z);
  }
};

struct BiomeCandidate {
  usize entry_index{};
  usize biome_index{};
  f32 distance_sq{};
  f32 strength{};
};

struct RawTerrainColumn {
  i32 surface_y{};
  usize biome_index{};
  BiomeSample biome_sample{};
};

[[nodiscard]] constexpr usize expanded_index(i32 local_x, i32 local_z) noexcept {
  return static_cast<usize>(local_x + ExpandedTerrainBorder) +
         static_cast<usize>(ExpandedTerrainSide) *
             static_cast<usize>(local_z + ExpandedTerrainBorder);
}

[[nodiscard]] TerrainFieldRuntime make_terrain_field(
    const NoiseFieldConfig& config,
    u64 dimension_seed,
    i32 origin_x,
    i32 origin_z,
    bool direct_samples) {
  TerrainFieldRuntime field;
  field.enabled = true;
  field.direct_samples = direct_samples;
  field.sampler = NoiseSampler{config, dimension_seed};
  if (!direct_samples) {
    field.lattice = ChunkNoiseLattice2D{field.sampler, origin_x, origin_z};
  }
  return field;
}

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
  blended.hill_multiplier = 0.0F;
  blended.ridge_multiplier = 0.0F;
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
    blended.hill_multiplier += modifiers.hill_multiplier * weight.weight;
    blended.ridge_multiplier += modifiers.ridge_multiplier * weight.weight;
    blended.roughness_multiplier += modifiers.roughness_multiplier * weight.weight;
    blended.terrace_step += modifiers.terrace_step * weight.weight;
    blended.terrace_strength += modifiers.terrace_strength * weight.weight;
    total_weight += weight.weight;
  }
  if (total_weight <= BiomeDistanceEpsilon) {
    return dimension.biomes[std::min(sample.dominant_biome, dimension.biomes.size() - 1U)]
        .raw.terrain;
  }
  const f32 inv_total = 1.0F / total_weight;
  blended.base_height_offset *= inv_total;
  blended.height_scale_multiplier *= inv_total;
  blended.hill_multiplier *= inv_total;
  blended.ridge_multiplier *= inv_total;
  blended.roughness_multiplier *= inv_total;
  blended.terrace_step *= inv_total;
  blended.terrace_strength *= inv_total;
  return blended;
}

[[nodiscard]] f32 ridged_value(f32 noise, f32 sharpness) noexcept {
  const f32 ridge = std::clamp(1.0F - std::abs(noise), 0.0F, 1.0F);
  return std::pow(ridge, std::max(0.1F, sharpness));
}

[[nodiscard]] f32 apply_terrace(f32 height, f32 step, f32 strength) noexcept {
  if (step < 1.0F || strength <= 0.0F) {
    return height;
  }
  const f32 terraced = std::round(height / step) * step;
  return lerp(height, terraced, std::clamp(strength, 0.0F, 1.0F));
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
    runtime.scale = field.scale;
    runtime.offset = field.offset;
    climate_fields.push_back(std::move(runtime));
  }
  std::vector<f32> climate_values(climate_fields.size());

  const bool use_domain_warp =
      dimension.terrain.shape.domain_warp_amplitude > 0.0F &&
      dimension.terrain.warp_x_noise.has_value() &&
      dimension.terrain.warp_z_noise.has_value();
  const bool use_step_smoothing = dimension.terrain.shape.max_column_step > 0;
  const bool use_direct_terrain_samples = use_domain_warp || use_step_smoothing;
  const TerrainFieldRuntime primary = make_terrain_field(
      dimension.terrain.primary_noise,
      dimension_seed,
      origin_x,
      origin_z,
      use_direct_terrain_samples);
  const TerrainFieldRuntime detail = make_terrain_field(
      dimension.terrain.detail_noise,
      dimension_seed,
      origin_x,
      origin_z,
      use_direct_terrain_samples);
  const TerrainFieldRuntime hill =
      dimension.terrain.hill_noise.has_value()
          ? make_terrain_field(
                *dimension.terrain.hill_noise,
                dimension_seed,
                origin_x,
                origin_z,
                use_direct_terrain_samples)
          : TerrainFieldRuntime{};
  const TerrainFieldRuntime ridge =
      dimension.terrain.ridge_noise.has_value()
          ? make_terrain_field(
                *dimension.terrain.ridge_noise,
                dimension_seed,
                origin_x,
                origin_z,
                use_direct_terrain_samples)
          : TerrainFieldRuntime{};
  const TerrainFieldRuntime warp_x =
      use_domain_warp
          ? make_terrain_field(
                *dimension.terrain.warp_x_noise,
                dimension_seed,
                origin_x,
                origin_z,
                use_step_smoothing)
          : TerrainFieldRuntime{};
  const TerrainFieldRuntime warp_z =
      use_domain_warp
          ? make_terrain_field(
                *dimension.terrain.warp_z_noise,
                dimension_seed,
                origin_x,
                origin_z,
                use_step_smoothing)
          : TerrainFieldRuntime{};

  auto climate_sample = [&](const ClimateFieldRuntime& field, i32 world_x, i32 world_z) {
    const f32 raw =
        use_step_smoothing
            ? field.sampler.sample_2d(static_cast<f32>(world_x), static_cast<f32>(world_z))
            : field.lattice.sample(world_x, world_z);
    return std::clamp(raw * field.scale + field.offset, -1.0F, 1.0F);
  };

  auto sample_column = [&](i32 local_x, i32 local_z) {
    const i32 world_x = origin_x + local_x;
    const i32 world_z = origin_z + local_z;
    for (usize field_index = 0; field_index < climate_fields.size(); ++field_index) {
      climate_values[field_index] =
          climate_sample(climate_fields[field_index], world_x, world_z);
    }
    const BiomeSample biome_sample = sample_biomes(dimension, climate_values);
    const usize biome_index =
        std::min(biome_sample.dominant_biome, dimension.biomes.size() - 1U);
    const BiomeTerrainModifiers modifiers =
        blend_terrain_modifiers(dimension, biome_sample);

    const f32 warp_amount = dimension.terrain.shape.domain_warp_amplitude;
    const f32 offset_x =
        use_domain_warp
            ? warp_x.sample(world_x, world_z, local_x, local_z, 0.0F, 0.0F) * warp_amount
            : 0.0F;
    const f32 offset_z =
        use_domain_warp
            ? warp_z.sample(world_x, world_z, local_x, local_z, 0.0F, 0.0F) * warp_amount
            : 0.0F;

    const f32 base = static_cast<f32>(dimension.terrain.height.base) +
                     (dimension.terrain.biome_modifiers.base_height_offset
                          ? modifiers.base_height_offset
                          : 0.0F);
    const f32 continental_scale =
        static_cast<f32>(dimension.terrain.height.scale) *
        (dimension.terrain.biome_modifiers.height_scale_multiplier
             ? modifiers.height_scale_multiplier
             : 1.0F);
    const f32 hill_scale =
        static_cast<f32>(dimension.terrain.height.hill_scale) *
        (dimension.terrain.biome_modifiers.hill_multiplier ? modifiers.hill_multiplier : 1.0F);
    const f32 ridge_scale =
        static_cast<f32>(dimension.terrain.height.ridge_scale) *
        (dimension.terrain.biome_modifiers.ridge_multiplier ? modifiers.ridge_multiplier : 1.0F);
    const f32 detail_scale =
        static_cast<f32>(dimension.terrain.height.detail_scale) *
        (dimension.terrain.biome_modifiers.roughness_multiplier
             ? modifiers.roughness_multiplier
             : 1.0F);

    const f32 continental =
        primary.sample(world_x, world_z, local_x, local_z, offset_x, offset_z);
    const f32 hills = hill.sample(world_x, world_z, local_x, local_z, offset_x, offset_z);
    const f32 ridge_height = ridged_value(
        ridge.sample(world_x, world_z, local_x, local_z, offset_x, offset_z),
        dimension.terrain.shape.ridge_sharpness);
    const f32 detail_height =
        detail.sample(world_x, world_z, local_x, local_z, offset_x, offset_z);

    f32 height = base + continental * continental_scale + hills * hill_scale +
                 ridge_height * ridge_scale + detail_height * detail_scale;
    const f32 terrace_step =
        dimension.terrain.biome_modifiers.terrace_step && modifiers.terrace_step > 0.0F
            ? modifiers.terrace_step
            : dimension.terrain.shape.terrace_step;
    const f32 terrace_strength =
        dimension.terrain.biome_modifiers.terrace_strength && modifiers.terrace_strength > 0.0F
            ? modifiers.terrace_strength
            : dimension.terrain.shape.terrace_strength;
    height = apply_terrace(height, terrace_step, terrace_strength);

    return RawTerrainColumn{
        std::clamp(
            static_cast<i32>(std::floor(height + 0.5F)),
            dimension.raw.vertical.min_y,
            dimension.raw.vertical.max_y),
        biome_index,
        biome_sample};
  };

  std::array<RawTerrainColumn, ExpandedTerrainArea> raw_columns{};
  std::array<i32, ExpandedTerrainArea> smoothed_heights{};
  const i32 min_local = use_step_smoothing ? -ExpandedTerrainBorder : 0;
  const i32 max_local =
      use_step_smoothing ? ChunkSize + ExpandedTerrainBorder - 1 : ChunkSize - 1;
  for (i32 local_z = min_local; local_z <= max_local; ++local_z) {
    for (i32 local_x = min_local; local_x <= max_local; ++local_x) {
      const RawTerrainColumn raw = sample_column(local_x, local_z);
      raw_columns[expanded_index(local_x, local_z)] = raw;
      smoothed_heights[expanded_index(local_x, local_z)] = raw.surface_y;
    }
  }

  auto relax_height_at = [&](i32 local_x, i32 local_z) {
    const i32 max_step = dimension.terrain.shape.max_column_step;
    i32& height = smoothed_heights[expanded_index(local_x, local_z)];
    for (const auto [offset_x, offset_z] :
         std::array<std::pair<i32, i32>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
      const i32 neighbor_x = local_x + offset_x;
      const i32 neighbor_z = local_z + offset_z;
      if (neighbor_x < min_local || neighbor_x > max_local ||
          neighbor_z < min_local || neighbor_z > max_local) {
        continue;
      }
      const i32 neighbor = smoothed_heights[expanded_index(neighbor_x, neighbor_z)];
      height = std::min(height, neighbor + max_step);
    }
  };

  const i32 max_step = dimension.terrain.shape.max_column_step;
  if (max_step > 0) {
    const i32 max_passes = ExpandedTerrainSide * 2;
    for (i32 pass = 0; pass < max_passes; ++pass) {
      std::array<i32, ExpandedTerrainArea> before = smoothed_heights;
      for (i32 local_z = min_local; local_z <= max_local; ++local_z) {
        for (i32 local_x = min_local; local_x <= max_local; ++local_x) {
          relax_height_at(local_x, local_z);
        }
      }
      for (i32 local_z = max_local; local_z >= min_local; --local_z) {
        for (i32 local_x = max_local; local_x >= min_local; --local_x) {
          relax_height_at(local_x, local_z);
        }
      }
      if (smoothed_heights == before) {
        break;
      }
    }
  }

  TerrainProfile profile;
  profile.min_surface_y = std::numeric_limits<i32>::max();
  profile.max_surface_y = std::numeric_limits<i32>::min();
  for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
    const ChunkLocalColumn column = voxel::local_column(column_index);
    const RawTerrainColumn& raw = raw_columns[expanded_index(column.x, column.z)];
    const i32 surface_y = smoothed_heights[expanded_index(column.x, column.z)];
    profile.surface_y_by_column[column_index] = surface_y;
    profile.biome_by_column[column_index] = raw.biome_index;
    profile.biome_samples_by_column[column_index] = raw.biome_sample;
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

} // namespace arv::world::worldgen
