#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/location.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/storage.hpp"
#include "arv/world/worldgen/registry.hpp"
#include "arv/world/worldgen/stats.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arv::world {

inline constexpr std::string_view VoidWorldGenerationID = "arvane:worldgen/void";

struct WorldGenerationError {
  std::string message{};
};

struct SurfaceLayerDefinition {
  std::string block_name{};
  i32 depth{};
  bool remaining{};
};

struct WorldGenerationConfig {
  std::string id{};
  u32 version{1};
  std::string seed_salt{"overworld"};
  std::string dimension_id{"arvane:worldgen/dimension/overworld"};
  i32 min_y{-64};
  i32 max_y{128};
  i32 sea_level{32};
  i32 base_height{16};
  i32 height_scale{8};
  std::string terrain_noise_type{"simplex_2d"};
  f32 terrain_frequency{0.015F};
  u32 terrain_octaves{3};
  f32 terrain_amplitude{1.0F};
  f32 terrain_lacunarity{2.0F};
  f32 terrain_gain{0.5F};
  bool void_world{};
  std::vector<SurfaceLayerDefinition> surface_layers{};
  std::shared_ptr<const worldgen::ResolvedDimension> resolved_dimension{};
};

struct ResolvedSurfaceLayer {
  voxel::BlockVariantID block{};
  i32 depth{};
  bool remaining{};
};

struct WorldGenerationPalette {
  std::vector<ResolvedSurfaceLayer> surface_layers{};
  std::shared_ptr<const worldgen::ResolvedDimension> resolved_dimension{};
};

using GenerationSurfaceProfile = std::array<i32, voxel::ChunkArea>;

struct ChunkGenerationRequest {
  WorldID world_id{};
  u64 seed{};
  ChunkCoord coord{};
  i32 min_chunk_y{};
  i32 max_chunk_y{};
  WorldGenerationConfig config{};
  WorldGenerationPalette palette{};
  std::shared_ptr<const worldgen::ResolvedDimension> resolved_dimension{};
  worldgen::WorldGenerationStats* stats{};
};

struct GeneratedChunkSection {
  ChunkSectionCoord coord{};
  std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks{};
  std::array<voxel::FluidCell, voxel::ChunkVolume> fluids{};

  constexpr bool operator==(const GeneratedChunkSection&) const = default;
};

struct GeneratedChunk {
  WorldID world_id{};
  u64 seed{};
  u32 generator_version{};
  ChunkCoord coord{};
  GenerationSurfaceProfile surface_y_by_column{};
  std::vector<GeneratedChunkSection> sections{};
};

using WorldGenerationConfigResult = Expected<WorldGenerationConfig, WorldGenerationError>;
using WorldGenerationPaletteResult = Expected<WorldGenerationPalette, WorldGenerationError>;
using GeneratedChunkResult = Expected<GeneratedChunk, WorldGenerationError>;

[[nodiscard]] WorldGenerationConfig config_from_resolved_dimension(
    std::shared_ptr<const worldgen::ResolvedDimension> dimension);

[[nodiscard]] WorldGenerationConfig make_void_world_generation_config();

[[nodiscard]] WorldGenerationConfig refresh_generation_config_for_current_dimension(
    WorldGenerationConfig saved_config,
    const WorldGenerationConfig& current_default_config);

[[nodiscard]] WorldGenerationConfigResult parse_world_generation_yaml(std::string_view text);

[[nodiscard]] WorldGenerationPaletteResult resolve_world_generation_palette(
    const WorldGenerationConfig& config,
    const voxel::BlockRegistry& registry);

[[nodiscard]] i32 terrain_height_at(
    const WorldGenerationConfig& config,
    u64 seed,
    i32 world_x,
    i32 world_z) noexcept;

[[nodiscard]] GenerationSurfaceProfile terrain_height_profile_at(
    const WorldGenerationConfig& config,
    u64 seed,
    ChunkCoord coord) noexcept;

[[nodiscard]] voxel::BlockVariantID choose_surface_block(
    const WorldGenerationPalette& palette,
    i32 depth_from_surface) noexcept;

[[nodiscard]] arv::Location generate_world_spawn_location(
    const WorldGenerationConfig& config,
    u64 seed) noexcept;

[[nodiscard]] GeneratedChunkResult generate_chunk(const ChunkGenerationRequest& request);

} // namespace arv::world
