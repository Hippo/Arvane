#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::world::worldgen {

inline constexpr u32 SupportedSchemaVersion = 1U;
inline constexpr i32 DefaultLatticeStep = 4;

struct ResourceRef {
  std::string id{};

  [[nodiscard]] bool empty() const noexcept {
    return id.empty();
  }
};

struct WorldGenDiagnostic {
  std::string resource_id{};
  std::string field_path{"root"};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

enum class GenerationStage : u8 {
  UndergroundOres,
  UndergroundDecoration,
  SurfaceDecoration,
  UnderwaterDecoration,
  Structures,
};

inline constexpr std::array<GenerationStage, 5> StageOrder{
    GenerationStage::UndergroundOres,
    GenerationStage::UndergroundDecoration,
    GenerationStage::SurfaceDecoration,
    GenerationStage::UnderwaterDecoration,
    GenerationStage::Structures};

[[nodiscard]] std::optional<GenerationStage> generation_stage_from_string(
    std::string_view value) noexcept;
[[nodiscard]] std::string_view generation_stage_name(GenerationStage stage) noexcept;

struct StageFeatureList {
  GenerationStage stage{GenerationStage::SurfaceDecoration};
  std::vector<ResourceRef> features{};
};

struct VerticalConfig {
  i32 min_y{-32};
  i32 max_y{128};
  i32 sea_level{32};
};

struct IntRange {
  i32 min{};
  i32 max{};
};

struct FloatRange {
  f32 min{};
  f32 max{};

  [[nodiscard]] bool contains(f32 value) const noexcept {
    return value >= min && value <= max;
  }
};

struct ColorValue {
  std::string text{};
};

[[nodiscard]] bool valid_resource_id(std::string_view value) noexcept;
[[nodiscard]] bool valid_color(std::string_view value) noexcept;

} // namespace arv::world::worldgen
