#pragma once

#include "arv/blueprint/blueprint.hpp"
#include "arv/world/worldgen/bounds.hpp"

#include <optional>

namespace arv::world::worldgen {

enum class StructureRotationY : u8 {
  Deg0,
  Deg90,
  Deg180,
  Deg270,
};

[[nodiscard]] constexpr StructureRotationY structure_rotation_y_from_quarter_turns(
    i32 quarter_turns) noexcept {
  switch (floor_mod(quarter_turns, 4)) {
    case 1:
      return StructureRotationY::Deg90;
    case 2:
      return StructureRotationY::Deg180;
    case 3:
      return StructureRotationY::Deg270;
    default:
      return StructureRotationY::Deg0;
  }
}

[[nodiscard]] constexpr Vec3i rotate_blueprint_relative_y(
    Vec3i relative,
    StructureRotationY rotation) noexcept {
  switch (rotation) {
    case StructureRotationY::Deg0:
      return relative;
    case StructureRotationY::Deg90:
      return {-relative.z, relative.y, relative.x};
    case StructureRotationY::Deg180:
      return {-relative.x, relative.y, -relative.z};
    case StructureRotationY::Deg270:
      return {relative.z, relative.y, -relative.x};
  }
  return relative;
}

[[nodiscard]] constexpr Vec3i inverse_rotate_blueprint_relative_y(
    Vec3i relative,
    StructureRotationY rotation) noexcept {
  switch (rotation) {
    case StructureRotationY::Deg0:
      return relative;
    case StructureRotationY::Deg90:
      return {relative.z, relative.y, -relative.x};
    case StructureRotationY::Deg180:
      return {-relative.x, relative.y, -relative.z};
    case StructureRotationY::Deg270:
      return {-relative.z, relative.y, relative.x};
  }
  return relative;
}

[[nodiscard]] BlockPos blueprint_local_to_world(
    const blueprint::Blueprint& blueprint,
    BlockPos origin,
    BlockPos local,
    StructureRotationY rotation) noexcept;

[[nodiscard]] std::optional<BlockPos> blueprint_world_to_local(
    const blueprint::Blueprint& blueprint,
    BlockPos origin,
    BlockPos world,
    StructureRotationY rotation) noexcept;

[[nodiscard]] BlockAabb blueprint_world_bounds(
    const blueprint::Blueprint& blueprint,
    BlockPos origin,
    StructureRotationY rotation) noexcept;

[[nodiscard]] i32 blueprint_max_horizontal_extent_blocks(
    const blueprint::Blueprint& blueprint) noexcept;

} // namespace arv::world::worldgen
