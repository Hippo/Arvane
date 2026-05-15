#pragma once

#include "arv/world/worldgen/biome.hpp"
#include "arv/world/worldgen/common.hpp"
#include "arv/world/worldgen/noise.hpp"
#include "arv/voxel/storage.hpp"

#include <array>
#include <string>

namespace arv::world::worldgen {

struct TerrainHeightConfig {
  i32 base{8};
  i32 scale{28};
  i32 detail_scale{4};
};

struct TerrainBiomeModifierToggles {
  bool base_height_offset{true};
  bool height_scale_multiplier{true};
  bool roughness_multiplier{true};
};

struct TerrainConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  std::string type{"arvane:terrain/heightmap"};
  TerrainHeightConfig height{};
  NoiseFieldConfig primary_noise{};
  NoiseFieldConfig detail_noise{};
  TerrainBiomeModifierToggles biome_modifiers{};
};

struct TerrainProfile {
  std::array<i32, voxel::ChunkArea> surface_y_by_column{};
  std::array<f32, voxel::ChunkArea> slope_by_column{};
  std::array<usize, voxel::ChunkArea> biome_by_column{};
  std::array<BiomeSample, voxel::ChunkArea> biome_samples_by_column{};
  i32 min_surface_y{};
  i32 max_surface_y{};
};

} // namespace arv::world::worldgen
