#pragma once

#include "arv/world/worldgen/common.hpp"
#include "arv/world/worldgen/noise.hpp"

#include <string>

namespace arv::world::worldgen {

enum class CarverType : u8 {
  NoiseCaves,
  Ravines,
  TunnelCaves,
};

struct RavineCarverConfig {
  u64 salt{};
  i32 region_size_chunks{4};
  f32 chance_per_region{};
  IntRange length_chunks{3, 6};
  FloatRange width{2.0F, 5.0F};
  IntRange depth{18, 42};
};

struct TunnelCaveCarverConfig {
  u64 salt{};
  i32 region_size_chunks{4};
  f32 chance_per_region{};
  IntRange tunnels_per_region{1, 3};
  IntRange length_blocks{48, 128};
  FloatRange radius{1.5F, 3.0F};
  FloatRange vertical_radius_multiplier{0.55F, 0.9F};
  IntRange start_y{-48, 40};
  f32 horizontal_curl{0.28F};
  f32 vertical_curl{0.16F};
  f32 branch_chance{0.2F};
  IntRange branch_count{0, 2};
  f32 room_chance{0.1F};
  FloatRange room_radius{3.5F, 7.0F};
};

struct CarverConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  std::string type{"arvane:carver/noise_caves"};
  CarverType carver_type{CarverType::NoiseCaves};
  bool enabled{true};
  NoiseFieldConfig noise{};
  f32 threshold{0.62F};
  IntRange y_range{-32, 64};
  i32 surface_margin{8};
  i32 floor_margin{};
  RavineCarverConfig ravine{};
  TunnelCaveCarverConfig tunnel{};
};

} // namespace arv::world::worldgen
