#pragma once

#include "arv/world/worldgen/registry.hpp"

namespace arv::world::worldgen {

[[nodiscard]] bool biome_has_tag(const BiomeConfig& biome, std::string_view tag) noexcept;

[[nodiscard]] const std::vector<ResolvedSurfaceLayer>* choose_surface_layers(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    usize column_index) noexcept;

[[nodiscard]] voxel::BlockVariantID choose_terrain_surface_block(
    const std::vector<ResolvedSurfaceLayer>& layers,
    i32 depth_from_surface) noexcept;

[[nodiscard]] voxel::BlockVariantID terrain_block_at(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    BlockPos pos) noexcept;

[[nodiscard]] bool terrain_filters_pass(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    const std::vector<ResolvedPlacementFilter>& filters,
    BlockPos pos) noexcept;

} // namespace arv::world::worldgen
