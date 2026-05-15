#pragma once

#include "arv/world/worldgen/blueprint_bounds.hpp"
#include "arv/world/worldgen/registry.hpp"

#include <vector>

namespace arv::world::worldgen {

struct StructureCandidate {
  usize structure_set_index{};
  usize structure_entry_index{};
  usize feature_index{};
  ChunkCoord placement_cell{};
  ChunkCoord candidate_chunk{};
  BlockPos origin{};
  StructureRotationY rotation{StructureRotationY::Deg0};
  BlockAabb bounds{};
};

struct StructureCandidateSearchStats {
  u32 inspected_cells{};
  u32 accepted_candidates{};
};

[[nodiscard]] i32 structure_set_search_radius_chunks(
    const ResolvedStructureSet& structure_set) noexcept;

[[nodiscard]] std::vector<StructureCandidate> collect_structure_candidates_for_chunk(
    const ResolvedDimension& dimension,
    u64 seed,
    ChunkCoord chunk,
    i32 min_chunk_y,
    i32 max_chunk_y,
    StructureCandidateSearchStats* stats = nullptr);

} // namespace arv::world::worldgen
