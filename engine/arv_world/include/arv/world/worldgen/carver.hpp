#pragma once

#include "arv/world/worldgen/common.hpp"
#include "arv/world/worldgen/noise.hpp"

#include <string>

namespace arv::world::worldgen {

struct CarverConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  std::string type{"arvane:carver/noise_caves"};
  bool enabled{true};
  NoiseFieldConfig noise{};
  f32 threshold{0.62F};
  IntRange y_range{-32, 64};
  i32 surface_margin{8};
};

} // namespace arv::world::worldgen
