#pragma once

#include "arv/core/id.hpp"
#include "arv/core/types.hpp"

namespace arv::voxel {

struct FluidTypeTag {};
using FluidTypeID = ID<FluidTypeTag, u32>;

inline constexpr FluidTypeID NoFluidTypeID{0};
inline constexpr FluidTypeID DefaultWaterFluidTypeID{1};

enum class FluidFlags : u8 {
  None = 0,
  Source = 1U << 0U,
  Falling = 1U << 1U,
  Settled = 1U << 2U,
};

[[nodiscard]] constexpr FluidFlags operator|(FluidFlags lhs, FluidFlags rhs) noexcept {
  return static_cast<FluidFlags>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

[[nodiscard]] constexpr FluidFlags operator&(FluidFlags lhs, FluidFlags rhs) noexcept {
  return static_cast<FluidFlags>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

constexpr FluidFlags& operator|=(FluidFlags& lhs, FluidFlags rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool any(FluidFlags flags) noexcept {
  return static_cast<u8>(flags) != 0U;
}

[[nodiscard]] constexpr bool has_flag(FluidFlags flags, FluidFlags flag) noexcept {
  return any(flags & flag);
}

struct FluidCell {
  FluidTypeID type{NoFluidTypeID};
  u8 level{};
  FluidFlags flags{FluidFlags::None};

  constexpr bool operator==(const FluidCell&) const = default;

  [[nodiscard]] constexpr bool empty() const noexcept {
    return type == NoFluidTypeID || level == 0;
  }

  [[nodiscard]] constexpr bool source() const noexcept {
    return !empty() && has_flag(flags, FluidFlags::Source);
  }

  [[nodiscard]] constexpr bool falling() const noexcept {
    return !empty() && has_flag(flags, FluidFlags::Falling);
  }
};

[[nodiscard]] constexpr FluidCell empty_fluid_cell() noexcept {
  return {};
}

struct FluidDefinition {
  FluidTypeID type{DefaultWaterFluidTypeID};
  u8 max_level{8};
  u8 horizontal_decay{1};
  u8 vertical_flow_level{8};
  u8 max_horizontal_distance{7};
  u32 flow_interval_ticks{5};
  bool can_create_sources{true};
  bool can_flow_down{true};
  bool can_flow_sideways{true};
  bool can_replace_air{true};
  bool blocks_movement{};
  bool transparent{true};
  bool reduces_sky_light{true};
  u8 sky_light_reduction{1};
  bool emits_light{};
  u8 emission_r{};
  u8 emission_g{};
  u8 emission_b{};
};

[[nodiscard]] constexpr FluidDefinition make_water_fluid_definition(
    FluidTypeID type = DefaultWaterFluidTypeID) noexcept {
  FluidDefinition definition;
  definition.type = type;
  return definition;
}

[[nodiscard]] constexpr FluidCell make_fluid_source(const FluidDefinition& definition) noexcept {
  return {definition.type, definition.max_level, FluidFlags::Source};
}

[[nodiscard]] constexpr FluidCell make_flowing_fluid(
    const FluidDefinition& definition,
    u8 level,
    FluidFlags flags = FluidFlags::None) noexcept {
  return {definition.type, level > definition.max_level ? definition.max_level : level, flags};
}

} // namespace arv::voxel
