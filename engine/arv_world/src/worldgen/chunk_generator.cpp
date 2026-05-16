#include "arv/world/worldgen/chunk_generator.hpp"

#include "arv/core/profiling.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/worldgen/blueprint_placement.hpp"
#include "arv/world/worldgen/carver_runtime.hpp"
#include "arv/world/worldgen/seeds.hpp"
#include "arv/world/worldgen/structure_candidate.hpp"
#include "arv/world/worldgen/terrain_query.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace arv::world::worldgen {
namespace {

[[nodiscard]] usize stage_index(GenerationStage stage) noexcept {
  return static_cast<usize>(stage);
}

[[nodiscard]] u64 elapsed_ns(std::chrono::steady_clock::time_point start) noexcept {
  return static_cast<u64>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
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
        if (!filter.biome_tag_index.has_value() ||
            !biome_has_resolved_tag(
                dimension.biomes[biome_index],
                filter.biome_tag_index.value())) {
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

[[nodiscard]] bool profile_may_satisfy_filter(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    const ResolvedPlacementFilter& filter) noexcept {
  switch (filter.raw.type) {
    case PlacementFilterConfig::Type::Biome:
      return filter.biome_index.has_value() &&
             std::find(
                 profile.biome_by_column.begin(),
                 profile.biome_by_column.end(),
                 filter.biome_index.value()) != profile.biome_by_column.end();
    case PlacementFilterConfig::Type::BiomeTag:
      if (!filter.biome_tag_index.has_value()) {
        return false;
      }
      for (const usize biome_index : profile.biome_by_column) {
        if (biome_index < dimension.biomes.size() &&
            biome_has_resolved_tag(dimension.biomes[biome_index], filter.biome_tag_index.value())) {
          return true;
        }
      }
      return false;
    case PlacementFilterConfig::Type::Underwater:
      return filter.raw.bool_value
                 ? profile.min_surface_y < dimension.raw.vertical.sea_level
                 : profile.max_surface_y >= dimension.raw.vertical.sea_level;
    case PlacementFilterConfig::Type::BlockBelow:
    case PlacementFilterConfig::Type::Replaceable:
    case PlacementFilterConfig::Type::SlopeLessThan:
    case PlacementFilterConfig::Type::SlopeGreaterThan:
      return true;
  }
  return true;
}

[[nodiscard]] bool profile_may_satisfy_filters(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    const std::vector<ResolvedPlacementFilter>& filters) noexcept {
  for (const ResolvedPlacementFilter& filter : filters) {
    if (!profile_may_satisfy_filter(dimension, profile, filter)) {
      return false;
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

[[nodiscard]] std::optional<voxel::BlockVariantID> uniform_surface_block_for_depth_range(
    const std::vector<ResolvedSurfaceLayer>& layers,
    i32 min_depth_from_surface,
    i32 max_depth_from_surface) noexcept {
  if (layers.empty() || min_depth_from_surface < 0 ||
      max_depth_from_surface < min_depth_from_surface) {
    return std::nullopt;
  }

  i32 consumed = 0;
  for (const ResolvedSurfaceLayer& layer : layers) {
    const i32 layer_begin = consumed;
    const i32 layer_end = layer.remaining
                              ? std::numeric_limits<i32>::max()
                              : consumed + std::max(0, layer.depth) - 1;
    if (min_depth_from_surface >= layer_begin && max_depth_from_surface <= layer_end) {
      return layer.block;
    }
    if (!layer.remaining) {
      consumed += std::max(0, layer.depth);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<voxel::BlockVariantID> uniform_solid_section_block(
    const TerrainProfile& profile,
    const std::array<const std::vector<ResolvedSurfaceLayer>*, voxel::ChunkArea>&
        surface_layers_by_column,
    i32 section_min_y,
    i32 section_max_y) noexcept {
  std::optional<voxel::BlockVariantID> uniform_block;
  for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
    const std::vector<ResolvedSurfaceLayer>* layers = surface_layers_by_column[column_index];
    if (layers == nullptr) {
      return std::nullopt;
    }
    const i32 surface_y = profile.surface_y_by_column[column_index];
    if (section_max_y > surface_y) {
      return std::nullopt;
    }
    const std::optional<voxel::BlockVariantID> column_block =
        uniform_surface_block_for_depth_range(
            *layers,
            surface_y - section_max_y,
            surface_y - section_min_y);
    if (!column_block.has_value()) {
      return std::nullopt;
    }
    if (!uniform_block.has_value()) {
      uniform_block = column_block;
    } else if (uniform_block.value() != column_block.value()) {
      return std::nullopt;
    }
  }
  return uniform_block;
}

[[nodiscard]] const std::vector<usize>& resolved_stage_features(
    const ResolvedDimension& dimension,
    usize index) noexcept {
  if (index < StageOrder.size() && !dimension.features_by_stage[index].empty()) {
    return dimension.features_by_stage[index];
  }
  if (index < StageOrder.size() && dimension.dimension_features_by_stage[index].empty()) {
    return dimension.biome_features_by_stage[index];
  }
  return dimension.dimension_features_by_stage[index];
}

struct CachedTerrainProfile {
  ChunkCoord coord{};
  TerrainProfile profile{};
};

class TerrainProfileCache {
public:
  TerrainProfileCache(
      const ResolvedDimension& dimension,
      u64 seed,
      ChunkCoord origin,
      const TerrainProfile& origin_profile,
      WorldGenerationStats* stats) : dimension_(dimension), seed_(seed), stats_(stats) {
    profiles_.reserve(16U);
    profiles_.push_back({origin, origin_profile});
  }

  [[nodiscard]] const TerrainProfile& get(ChunkCoord coord) {
    const auto found = std::find_if(
        profiles_.begin(),
        profiles_.end(),
        [coord](const CachedTerrainProfile& cached) {
          return cached.coord == coord;
        });
    if (found != profiles_.end()) {
      if (stats_ != nullptr) {
        ++stats_->candidates.feature_origin_chunk_cache_hits;
      }
      return found->profile;
    }
    if (stats_ != nullptr) {
      ++stats_->candidates.feature_origin_chunks_profiled;
    }
    profiles_.push_back({coord, generate_terrain_profile(dimension_, seed_, coord, stats_)});
    return profiles_.back().profile;
  }

private:
  const ResolvedDimension& dimension_;
  u64 seed_{};
  WorldGenerationStats* stats_{};
  std::vector<CachedTerrainProfile> profiles_{};
};

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
      BlockPos target{{pos.value.x + rng.range_i32(-patch->radius_xz, patch->radius_xz),
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
    TerrainProfileCache& terrain_profile_cache,
    GeneratedChunkEditView& view,
    const ResolvedPlacedFeature& placed,
    u64 seed,
    ChunkCoord origin_chunk,
    WorldGenerationStats* stats) {
  const ResolvedConfiguredFeature& feature = dimension.configured_features[placed.feature_index];
  DeterministicRng rng{coordinate_seed(
      seed,
      origin_chunk.x,
      static_cast<i32>(placed.raw.salt & 0x7FFFFFFF),
      origin_chunk.z,
      placed.raw.salt)};
  const i32 count = placement_count(placed.raw.count, rng);
  if (stats != nullptr) {
    ++stats->candidates.feature_origin_chunks_considered;
  }
  if (count <= 0) {
    return;
  }
  const TerrainProfile* origin_profile = nullptr;
  for (i32 attempt = 0; attempt < count; ++attempt) {
    if (stats != nullptr) {
      ++stats->candidates.feature_candidates_considered;
    }
    const i32 local_x = rng.range_i32(0, ChunkSize - 1);
    const i32 local_z = rng.range_i32(0, ChunkSize - 1);
    const i32 world_x = origin_chunk.x * ChunkSize + local_x;
    const i32 world_z = origin_chunk.z * ChunkSize + local_z;
    if (origin_chunk != view.coord) {
      const i32 current_local_x = world_x - view.coord.x * ChunkSize;
      const i32 current_local_z = world_z - view.coord.z * ChunkSize;
      if (current_local_x < -feature.radius_blocks || current_local_x >= ChunkSize + feature.radius_blocks ||
          current_local_z < -feature.radius_blocks || current_local_z >= ChunkSize + feature.radius_blocks) {
        if (stats != nullptr) {
          ++stats->candidates.feature_candidates_rejected;
          ++stats->candidates.feature_candidates_rejected_by_extent;
        }
        continue;
      }
    }
    if (origin_profile == nullptr) {
      origin_profile = &terrain_profile_cache.get(origin_chunk);
      if (!profile_may_satisfy_filters(dimension, *origin_profile, placed.filters)) {
        if (stats != nullptr) {
          ++stats->candidates.feature_origin_chunks_rejected_by_summary;
          stats->candidates.feature_candidates_rejected +=
              static_cast<u64>(std::max(0, count - attempt));
        }
        return;
      }
    }
    const i32 y = placement_y(placed.raw.height, *origin_profile, local_x, local_z, rng);
    BlockPos pos{{world_x, y, world_z}};
    if (!filters_pass(dimension, *origin_profile, origin_chunk, view, placed.filters, pos)) {
      if (stats != nullptr) {
        ++stats->candidates.feature_candidates_rejected;
      }
      continue;
    }
    if (stats != nullptr) {
      ++stats->candidates.feature_candidates_applied;
    }
    place_feature(dimension, feature, view, pos, seed, placed.raw.salt, rng);
  }
}

} // namespace

GeneratedChunkResult generate_resolved_chunk(
    const ChunkGenerationRequest& request,
    const ResolvedDimension& dimension) {
  ARV_PROFILE_ZONE("Resolved chunk generation");
  WorldGenerationStats* stats = request.stats;
  const auto total_start = std::chrono::steady_clock::now();
  GeneratedChunk chunk;
  chunk.world_id = request.world_id;
  chunk.seed = request.seed;
  chunk.generator_version = dimension.raw.version;
  chunk.coord = request.coord;
  const i32 min_chunk_y = std::min(request.min_chunk_y, request.max_chunk_y);
  const i32 max_chunk_y = std::max(request.min_chunk_y, request.max_chunk_y);
  {
    ScopedWorldGenerationStageTimer timer{stats, WorldGenerationStage::AllocationHandoff};
    chunk.sections.reserve(static_cast<usize>(max_chunk_y - min_chunk_y + 1));
  }
  auto make_section = [&](i32 chunk_y) {
    const auto section_start = std::chrono::steady_clock::now();
    GeneratedChunkSection section;
    section.coord = chunk_section(request.coord, chunk_y);
    if (stats != nullptr) {
      ++stats->sections.allocated_sections;
      stats->add_stage_time(
          WorldGenerationStage::AllocationHandoff,
          elapsed_ns(section_start));
    }
    return section;
  };
  auto append_section = [&](GeneratedChunkSection section) {
    const auto handoff_start = std::chrono::steady_clock::now();
    chunk.sections.emplace_back(std::move(section));
    if (stats != nullptr) {
      stats->add_stage_time(
          WorldGenerationStage::AllocationHandoff,
          elapsed_ns(handoff_start));
    }
  };

  TerrainProfile profile = generate_terrain_profile(dimension, request.seed, request.coord, stats);
  chunk.surface_y_by_column = profile.surface_y_by_column;

  std::array<const std::vector<ResolvedSurfaceLayer>*, voxel::ChunkArea> surface_layers_by_column{};
  {
    ScopedWorldGenerationStageTimer timer{stats, WorldGenerationStage::SurfaceRules};
    for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
      surface_layers_by_column[column_index] =
          choose_surface_layers(dimension, profile, column_index);
    }
  }

  {
    ARV_PROFILE_ZONE("Worldgen base fill and surfaces");
    const voxel::FluidCell water_source =
        voxel::make_fluid_source(voxel::make_water_fluid_definition());
    const i32 fill_ceiling =
        std::max(profile.max_surface_y, dimension.raw.vertical.sea_level);
    for (i32 chunk_y = min_chunk_y; chunk_y <= max_chunk_y; ++chunk_y) {
      GeneratedChunkSection section = make_section(chunk_y);
      if (stats != nullptr) {
        ++stats->sections.generated_sections;
      }

      const i32 section_min_y = chunk_y * ChunkSize;
      const i32 section_max_y = section_min_y + ChunkSize - 1;
      if (section_max_y < dimension.raw.vertical.min_y ||
          section_min_y > dimension.raw.vertical.max_y ||
          section_min_y > fill_ceiling) {
        if (stats != nullptr) {
          ++stats->sections.skipped_sections;
          ++stats->sections.all_air_sections;
        }
        append_section(std::move(section));
        continue;
      }

      const i32 fill_min_y = std::max(section_min_y, dimension.raw.vertical.min_y);
      const i32 fill_max_y = std::min({section_max_y, dimension.raw.vertical.max_y, fill_ceiling});
      if (fill_max_y < fill_min_y) {
        if (stats != nullptr) {
          ++stats->sections.skipped_sections;
          ++stats->sections.all_air_sections;
        }
        append_section(std::move(section));
        continue;
      }

      if (section_min_y > profile.max_surface_y &&
          section_max_y <= dimension.raw.vertical.sea_level) {
        {
          ScopedWorldGenerationStageTimer timer{stats, WorldGenerationStage::FluidFill};
          section.fluids.fill(water_source);
        }
        if (stats != nullptr) {
          ++stats->sections.bulk_fluid_sections;
        }
        append_section(std::move(section));
        continue;
      }

      if (section_min_y >= dimension.raw.vertical.min_y &&
          section_max_y <= dimension.raw.vertical.max_y &&
          section_max_y <= profile.min_surface_y) {
        const std::optional<voxel::BlockVariantID> uniform_block =
            uniform_solid_section_block(
                profile,
                surface_layers_by_column,
                section_min_y,
                section_max_y);
        if (uniform_block.has_value()) {
          {
            ScopedWorldGenerationStageTimer timer{stats, WorldGenerationStage::BaseFill};
            section.blocks.fill(uniform_block.value());
          }
          if (stats != nullptr) {
            ++stats->sections.bulk_solid_sections;
          }
          append_section(std::move(section));
          continue;
        }
      }

      if (stats != nullptr) {
        ++stats->sections.mixed_sections;
      }

      {
        ScopedWorldGenerationStageTimer timer{stats, WorldGenerationStage::BaseFill};
        for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
          const ChunkLocalColumn column = voxel::local_column(column_index);
          const i32 surface_y = profile.surface_y_by_column[column_index];
          const std::vector<ResolvedSurfaceLayer>* layers = surface_layers_by_column[column_index];

          const i32 terrain_top_y = std::min(fill_max_y, surface_y);
          if (layers != nullptr && fill_min_y <= terrain_top_y) {
            const i32 local_y_begin = fill_min_y - section_min_y;
            const i32 local_y_end = terrain_top_y - section_min_y;
            usize block_index = voxel::local_index({{column.x, local_y_begin, column.z}});
            for (i32 local_y = local_y_begin; local_y <= local_y_end; ++local_y) {
              const i32 world_y = section_min_y + local_y;
              section.blocks[block_index] =
                  choose_terrain_surface_block(*layers, surface_y - world_y);
              block_index += voxel::ChunkSide;
            }
          }
        }
      }

      {
        ScopedWorldGenerationStageTimer timer{stats, WorldGenerationStage::FluidFill};
        for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
          const ChunkLocalColumn column = voxel::local_column(column_index);
          const i32 surface_y = profile.surface_y_by_column[column_index];
          const i32 water_bottom_y = std::max(fill_min_y, surface_y + 1);
          const i32 water_top_y = std::min(fill_max_y, dimension.raw.vertical.sea_level);
          if (water_bottom_y > water_top_y) {
            continue;
          }
          const i32 local_y_begin = water_bottom_y - section_min_y;
          const i32 local_y_end = water_top_y - section_min_y;
          usize block_index = voxel::local_index({{column.x, local_y_begin, column.z}});
          for (i32 local_y = local_y_begin; local_y <= local_y_end; ++local_y) {
            section.fluids[block_index] = water_source;
            block_index += voxel::ChunkSide;
          }
        }
      }
      append_section(std::move(section));
    }
  }

  GeneratedChunkEditView view{request.coord, min_chunk_y, max_chunk_y, &chunk.sections};
  TerrainProfileCache terrain_profile_cache{dimension, request.seed, request.coord, profile, stats};

  {
    ARV_PROFILE_ZONE("Worldgen carvers");
    apply_carvers(dimension, profile, view, request.seed, stats);
  }

  {
    ARV_PROFILE_ZONE("Worldgen features and structures");
    for (GenerationStage stage : StageOrder) {
      const usize index = stage_index(stage);
      const bool structure_stage = stage == GenerationStage::Structures;
      ScopedWorldGenerationStageTimer timer{
          stats,
          structure_stage ? WorldGenerationStage::Structures : WorldGenerationStage::Features};
      std::vector<usize> fallback_feature_indices;
      const std::vector<usize>* feature_indices = &resolved_stage_features(dimension, index);
      if (dimension.features_by_stage[index].empty() &&
          !dimension.dimension_features_by_stage[index].empty() &&
          !dimension.biome_features_by_stage[index].empty()) {
        fallback_feature_indices.reserve(
            dimension.dimension_features_by_stage[index].size() +
            dimension.biome_features_by_stage[index].size());
        fallback_feature_indices.insert(
            fallback_feature_indices.end(),
            dimension.dimension_features_by_stage[index].begin(),
            dimension.dimension_features_by_stage[index].end());
        fallback_feature_indices.insert(
            fallback_feature_indices.end(),
            dimension.biome_features_by_stage[index].begin(),
            dimension.biome_features_by_stage[index].end());
        std::sort(fallback_feature_indices.begin(), fallback_feature_indices.end());
        fallback_feature_indices.erase(
            std::unique(fallback_feature_indices.begin(), fallback_feature_indices.end()),
            fallback_feature_indices.end());
        feature_indices = &fallback_feature_indices;
      }
      for (const usize feature_index : *feature_indices) {
        const ResolvedPlacedFeature& placed = dimension.placed_features[feature_index];
        if (stats != nullptr) {
          ++stats->candidates.placed_features_considered;
        }
        const i32 radius_chunks = std::max(0, (placed.radius_blocks + ChunkSize - 1) / ChunkSize);
        for (i32 dz = -radius_chunks; dz <= radius_chunks; ++dz) {
          for (i32 dx = -radius_chunks; dx <= radius_chunks; ++dx) {
            const ChunkCoord origin_chunk{request.coord.x + dx, request.coord.z + dz};
            run_placed_feature(
                dimension,
                terrain_profile_cache,
                view,
                placed,
                request.seed,
                origin_chunk,
                stats);
          }
        }
      }
      if (structure_stage) {
        StructureCandidateSearchStats structure_stats;
        const std::vector<StructureCandidate> structure_candidates =
            collect_structure_candidates_for_chunk(
                dimension,
                request.seed,
                request.coord,
                min_chunk_y,
                max_chunk_y,
                stats != nullptr ? &structure_stats : nullptr);
        if (stats != nullptr) {
          stats->candidates.structure_cells_inspected += structure_stats.inspected_cells;
          stats->candidates.structure_candidates_considered +=
              structure_stats.considered_candidates;
          stats->candidates.structure_candidates_intersecting +=
              structure_stats.intersecting_candidates;
          stats->candidates.structure_candidates_rejected +=
              structure_stats.rejected_candidates;
          stats->candidates.structure_candidates_applied += structure_stats.accepted_candidates;
        }
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

  if (stats != nullptr) {
    stats->total_ns += elapsed_ns(total_start);
  }

  return GeneratedChunkResult::success(std::move(chunk));
}

} // namespace arv::world::worldgen
