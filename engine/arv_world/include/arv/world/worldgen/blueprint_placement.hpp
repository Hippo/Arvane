#pragma once

#include "arv/world/world_generation.hpp"
#include "arv/world/worldgen/blueprint_bounds.hpp"

#include <optional>
#include <vector>

namespace arv::world::worldgen {

struct GeneratedChunkEditView {
  ChunkCoord coord{};
  i32 min_chunk_y{};
  i32 max_chunk_y{};
  std::vector<GeneratedChunkSection>* sections{};

  [[nodiscard]] bool contains(BlockPos pos) const noexcept;
  [[nodiscard]] std::optional<usize> section_index(i32 chunk_y) const noexcept;
  [[nodiscard]] voxel::BlockVariantID get(BlockPos pos) const noexcept;
  void set(BlockPos pos, voxel::BlockVariantID block) noexcept;
};

struct BlueprintPlacementInput {
  const ResolvedConfiguredFeature* feature{};
  BlockPos origin{};
  StructureRotationY rotation{StructureRotationY::Deg0};
  BlockAabb bounds{};
  u64 world_seed{};
  u64 salt{};
};

void place_blueprint_intersection(
    const BlueprintPlacementInput& input,
    GeneratedChunkEditView& view);

} // namespace arv::world::worldgen
