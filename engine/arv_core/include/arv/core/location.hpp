#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <optional>

namespace arv {

struct Location {
  f32 x{};
  f32 y{};
  f32 z{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  std::optional<WorldID> world{};

  constexpr bool operator==(const Location&) const = default;
};

[[nodiscard]] constexpr Vec3f location_position(Location location) noexcept {
  return {location.x, location.y, location.z};
}

[[nodiscard]] constexpr Location location_from_position(
    Vec3f position,
    f32 yaw_radians = 0.0F,
    f32 pitch_radians = 0.0F,
    std::optional<WorldID> world = std::nullopt) noexcept {
  return {
      position.x,
      position.y,
      position.z,
      yaw_radians,
      pitch_radians,
      world};
}

[[nodiscard]] constexpr Location default_spawn_location() noexcept {
  return {8.0F, 20.0F, -20.0F, 0.0F, 0.0F, std::nullopt};
}

} // namespace arv
