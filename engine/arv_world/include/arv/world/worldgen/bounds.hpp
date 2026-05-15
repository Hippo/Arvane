#pragma once

#include "arv/core/coordinates.hpp"

#include <algorithm>
#include <optional>

namespace arv::world::worldgen {

struct BlockAabb {
  BlockPos min{};
  BlockPos max{};
};

[[nodiscard]] constexpr bool valid_aabb(BlockAabb bounds) noexcept {
  return bounds.min.value.x <= bounds.max.value.x &&
         bounds.min.value.y <= bounds.max.value.y &&
         bounds.min.value.z <= bounds.max.value.z;
}

[[nodiscard]] constexpr bool intersects(BlockAabb lhs, BlockAabb rhs) noexcept {
  return valid_aabb(lhs) && valid_aabb(rhs) &&
         lhs.min.value.x <= rhs.max.value.x && lhs.max.value.x >= rhs.min.value.x &&
         lhs.min.value.y <= rhs.max.value.y && lhs.max.value.y >= rhs.min.value.y &&
         lhs.min.value.z <= rhs.max.value.z && lhs.max.value.z >= rhs.min.value.z;
}

[[nodiscard]] inline std::optional<BlockAabb> intersection(
    BlockAabb lhs,
    BlockAabb rhs) noexcept {
  if (!intersects(lhs, rhs)) {
    return std::nullopt;
  }
  return BlockAabb{{
                       std::max(lhs.min.value.x, rhs.min.value.x),
                       std::max(lhs.min.value.y, rhs.min.value.y),
                       std::max(lhs.min.value.z, rhs.min.value.z),
                   },
                   {
                       std::min(lhs.max.value.x, rhs.max.value.x),
                       std::min(lhs.max.value.y, rhs.max.value.y),
                       std::min(lhs.max.value.z, rhs.max.value.z),
                   }};
}

[[nodiscard]] constexpr BlockAabb chunk_column_bounds(
    ChunkCoord coord,
    i32 min_chunk_y,
    i32 max_chunk_y) noexcept {
  const i32 lower_y = min_chunk_y < max_chunk_y ? min_chunk_y : max_chunk_y;
  const i32 upper_y = min_chunk_y < max_chunk_y ? max_chunk_y : min_chunk_y;
  return {{
              coord.x * ChunkSize,
              lower_y * ChunkSize,
              coord.z * ChunkSize,
          },
          {
              coord.x * ChunkSize + ChunkSize - 1,
              upper_y * ChunkSize + ChunkSize - 1,
              coord.z * ChunkSize + ChunkSize - 1,
          }};
}

} // namespace arv::world::worldgen
