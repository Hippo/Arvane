#include "arv/world/worldgen/structure_candidate.hpp"

#include "arv/world/worldgen/chunk_generator.hpp"
#include "arv/world/worldgen/seeds.hpp"
#include "arv/world/worldgen/terrain_query.hpp"

#include <algorithm>
#include <optional>
#include <variant>

namespace arv::world::worldgen {
namespace {

[[nodiscard]] bool candidate_less(
    const ResolvedDimension& dimension,
    const StructureCandidate& lhs,
    const StructureCandidate& rhs) noexcept {
  const ResolvedStructureSet& lhs_set = dimension.structure_sets[lhs.structure_set_index];
  const ResolvedStructureSet& rhs_set = dimension.structure_sets[rhs.structure_set_index];
  if (lhs_set.raw.id != rhs_set.raw.id) {
    return lhs_set.raw.id < rhs_set.raw.id;
  }
  if (lhs.placement_cell.x != rhs.placement_cell.x) {
    return lhs.placement_cell.x < rhs.placement_cell.x;
  }
  if (lhs.placement_cell.z != rhs.placement_cell.z) {
    return lhs.placement_cell.z < rhs.placement_cell.z;
  }
  const ResolvedConfiguredFeature& lhs_feature = dimension.configured_features[lhs.feature_index];
  const ResolvedConfiguredFeature& rhs_feature = dimension.configured_features[rhs.feature_index];
  if (lhs_feature.id != rhs_feature.id) {
    return lhs_feature.id < rhs_feature.id;
  }
  if (lhs.origin.value.x != rhs.origin.value.x) {
    return lhs.origin.value.x < rhs.origin.value.x;
  }
  if (lhs.origin.value.y != rhs.origin.value.y) {
    return lhs.origin.value.y < rhs.origin.value.y;
  }
  if (lhs.origin.value.z != rhs.origin.value.z) {
    return lhs.origin.value.z < rhs.origin.value.z;
  }
  return static_cast<u8>(lhs.rotation) < static_cast<u8>(rhs.rotation);
}

[[nodiscard]] std::optional<usize> select_weighted_structure_entry(
    const ResolvedStructureSet& structure_set,
    DeterministicRng& rng) noexcept {
  i32 total_weight = 0;
  for (const ResolvedStructureEntry& entry : structure_set.structures) {
    total_weight += std::max(0, entry.weight);
  }
  if (total_weight <= 0) {
    return std::nullopt;
  }
  i32 pick = rng.range_i32(1, total_weight);
  for (usize index = 0; index < structure_set.structures.size(); ++index) {
    pick -= std::max(0, structure_set.structures[index].weight);
    if (pick <= 0) {
      return index;
    }
  }
  return structure_set.structures.empty() ? std::nullopt : std::optional<usize>{0U};
}

[[nodiscard]] std::optional<StructureCandidate> candidate_for_cell(
    const ResolvedDimension& dimension,
    u64 seed,
    usize structure_set_index,
    ChunkCoord placement_cell,
    BlockAabb current_chunk_bounds,
    StructureCandidateSearchStats* stats) {
  const ResolvedStructureSet& structure_set = dimension.structure_sets[structure_set_index];
  if (structure_set.structures.empty()) {
    if (stats != nullptr) {
      ++stats->rejected_candidates;
    }
    return std::nullopt;
  }
  const i32 average = std::max(1, structure_set.raw.placement.average_distance_chunks);
  const i32 separation = std::clamp(
      structure_set.raw.placement.separation_chunks,
      0,
      std::max(0, average - 1));
  const i32 span = std::max(1, average - separation);
  DeterministicRng rng{coordinate_seed(
      seed,
      placement_cell.x,
      static_cast<i32>(structure_set.raw.salt & 0x7FFFFFFF),
      placement_cell.z,
      structure_set.raw.salt ^ mix64(fnv1a(structure_set.raw.id)))};
  const ChunkCoord candidate_chunk{
      placement_cell.x * average + rng.range_i32(0, span - 1),
      placement_cell.z * average + rng.range_i32(0, span - 1)};
  const std::optional<usize> entry_index =
      select_weighted_structure_entry(structure_set, rng);
  if (!entry_index.has_value()) {
    if (stats != nullptr) {
      ++stats->rejected_candidates;
    }
    return std::nullopt;
  }
  if (stats != nullptr) {
    ++stats->considered_candidates;
  }
  const ResolvedStructureEntry& entry = structure_set.structures[entry_index.value()];
  const ResolvedConfiguredFeature& feature = dimension.configured_features[entry.feature_index];
  const auto* blueprint_feature = std::get_if<BlueprintFeatureConfig>(&feature.raw.config);
  if (blueprint_feature == nullptr) {
    if (stats != nullptr) {
      ++stats->rejected_candidates;
    }
    return std::nullopt;
  }

  const i32 local_x = rng.range_i32(0, ChunkSize - 1);
  const i32 local_z = rng.range_i32(0, ChunkSize - 1);
  const TerrainProfile candidate_profile =
      generate_terrain_profile(dimension, seed, candidate_chunk);
  const usize column_index = voxel::local_column_index({local_x, local_z});
  const BlockPos origin{{
      candidate_chunk.x * ChunkSize + local_x,
      candidate_profile.surface_y_by_column[column_index] + 1,
      candidate_chunk.z * ChunkSize + local_z}};
  if (!terrain_filters_pass(dimension, candidate_profile, structure_set.filters, origin)) {
    if (stats != nullptr) {
      ++stats->rejected_candidates;
    }
    return std::nullopt;
  }
  const StructureRotationY rotation =
      blueprint_feature->random_y_rotation
          ? structure_rotation_y_from_quarter_turns(rng.range_i32(0, 3))
          : StructureRotationY::Deg0;
  const BlockAabb bounds = blueprint_world_bounds(feature.blueprint, origin, rotation);
  if (!intersects(bounds, current_chunk_bounds)) {
    if (stats != nullptr) {
      ++stats->rejected_candidates;
    }
    return std::nullopt;
  }
  if (stats != nullptr) {
    ++stats->intersecting_candidates;
  }
  return StructureCandidate{
      structure_set_index,
      entry_index.value(),
      entry.feature_index,
      placement_cell,
      candidate_chunk,
      origin,
      rotation,
      bounds};
}

} // namespace

i32 structure_set_search_radius_chunks(const ResolvedStructureSet& structure_set) noexcept {
  return std::max(0, (structure_set.radius_blocks + ChunkSize - 1) / ChunkSize);
}

std::vector<StructureCandidate> collect_structure_candidates_for_chunk(
    const ResolvedDimension& dimension,
    u64 seed,
    ChunkCoord chunk,
    i32 min_chunk_y,
    i32 max_chunk_y,
    StructureCandidateSearchStats* stats) {
  std::vector<StructureCandidate> candidates;
  const BlockAabb current_bounds = chunk_column_bounds(chunk, min_chunk_y, max_chunk_y);
  for (usize structure_set_index = 0; structure_set_index < dimension.structure_sets.size();
       ++structure_set_index) {
    const ResolvedStructureSet& structure_set = dimension.structure_sets[structure_set_index];
    const i32 average = std::max(1, structure_set.raw.placement.average_distance_chunks);
    const i32 radius_chunks = structure_set_search_radius_chunks(structure_set);
    const i32 min_cell_x = floor_div(chunk.x - radius_chunks, average);
    const i32 max_cell_x = floor_div(chunk.x + radius_chunks, average);
    const i32 min_cell_z = floor_div(chunk.z - radius_chunks, average);
    const i32 max_cell_z = floor_div(chunk.z + radius_chunks, average);
    for (i32 cell_z = min_cell_z; cell_z <= max_cell_z; ++cell_z) {
      for (i32 cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x) {
        if (stats != nullptr) {
          ++stats->inspected_cells;
        }
        std::optional<StructureCandidate> candidate = candidate_for_cell(
            dimension,
            seed,
            structure_set_index,
            {cell_x, cell_z},
            current_bounds,
            stats);
        if (!candidate.has_value()) {
          continue;
        }
        if (stats != nullptr) {
          ++stats->accepted_candidates;
        }
        candidates.push_back(candidate.value());
      }
    }
  }
  std::sort(candidates.begin(), candidates.end(), [&](const auto& lhs, const auto& rhs) {
    return candidate_less(dimension, lhs, rhs);
  });
  return candidates;
}

} // namespace arv::world::worldgen
