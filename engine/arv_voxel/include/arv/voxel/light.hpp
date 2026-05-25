#pragma once

#include "arv/core/types.hpp"

namespace arv::voxel {

using LightWord = u16;

inline constexpr u8 MaxLightLevel = 15;
inline constexpr LightWord NoLight = 0;

[[nodiscard]] constexpr u8 clamp_light_channel(u8 value) noexcept {
  return value > MaxLightLevel ? MaxLightLevel : value;
}

[[nodiscard]] constexpr LightWord pack_light(u8 r, u8 g, u8 b, u8 sky) noexcept {
  return static_cast<LightWord>(
      ((static_cast<u16>(clamp_light_channel(r)) & 0x0FU) << 0U) |
      ((static_cast<u16>(clamp_light_channel(g)) & 0x0FU) << 4U) |
      ((static_cast<u16>(clamp_light_channel(b)) & 0x0FU) << 8U) |
      ((static_cast<u16>(clamp_light_channel(sky)) & 0x0FU) << 12U));
}

[[nodiscard]] constexpr u8 light_r(LightWord value) noexcept {
  return static_cast<u8>((value >> 0U) & 0x0FU);
}

[[nodiscard]] constexpr u8 light_g(LightWord value) noexcept {
  return static_cast<u8>((value >> 4U) & 0x0FU);
}

[[nodiscard]] constexpr u8 light_b(LightWord value) noexcept {
  return static_cast<u8>((value >> 8U) & 0x0FU);
}

[[nodiscard]] constexpr u8 light_sky(LightWord value) noexcept {
  return static_cast<u8>((value >> 12U) & 0x0FU);
}

[[nodiscard]] constexpr u8 visible_light_level(LightWord value) noexcept {
  const u8 rgb_max = light_r(value) > light_g(value) ? light_r(value) : light_g(value);
  const u8 block_max = rgb_max > light_b(value) ? rgb_max : light_b(value);
  return block_max > light_sky(value) ? block_max : light_sky(value);
}

[[nodiscard]] constexpr LightWord pack_block_light(u8 r, u8 g, u8 b) noexcept {
  return pack_light(r, g, b, 0);
}

[[nodiscard]] constexpr LightWord with_sky_light(LightWord value, u8 sky) noexcept {
  return pack_light(light_r(value), light_g(value), light_b(value), sky);
}

[[nodiscard]] constexpr LightWord with_block_light(LightWord value, u8 r, u8 g, u8 b) noexcept {
  return pack_light(r, g, b, light_sky(value));
}

} // namespace arv::voxel
