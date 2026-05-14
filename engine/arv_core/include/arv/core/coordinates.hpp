#pragma once

#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <functional>

namespace arv {

inline constexpr i32 ChunkSize = 16;
inline constexpr i32 RegionSizeInChunks = 32;

struct BlockPos {
  Vec3i value{};

  constexpr bool operator==(const BlockPos&) const = default;
};

struct ChunkCoord {
  i32 x{};
  i32 z{};

  constexpr bool operator==(const ChunkCoord&) const = default;
};

struct ChunkSectionCoord {
  Vec3i value{};

  constexpr bool operator==(const ChunkSectionCoord&) const = default;
};

struct RegionCoord {
  i32 x{};
  i32 z{};

  constexpr bool operator==(const RegionCoord&) const = default;
};

struct RegionLocalChunkCoord {
  i32 x{};
  i32 z{};

  constexpr bool operator==(const RegionLocalChunkCoord&) const = default;
};

struct SectionLocalPos {
  Vec3i value{};

  constexpr bool operator==(const SectionLocalPos&) const = default;
};

enum class AxisDirection {
  NegativeX,
  PositiveX,
  NegativeY,
  PositiveY,
  NegativeZ,
  PositiveZ,
};

[[nodiscard]] constexpr i32 floor_div(i32 value, i32 divisor) noexcept {
  const i32 quotient = value / divisor;
  const i32 remainder = value % divisor;
  return (remainder != 0 && ((remainder < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

[[nodiscard]] constexpr i32 floor_mod(i32 value, i32 divisor) noexcept {
  const i32 remainder = value % divisor;
  return remainder < 0 ? remainder + (divisor < 0 ? -divisor : divisor) : remainder;
}

[[nodiscard]] constexpr ChunkSectionCoord block_to_chunk(BlockPos pos) noexcept {
  return {{
      floor_div(pos.value.x, ChunkSize),
      floor_div(pos.value.y, ChunkSize),
      floor_div(pos.value.z, ChunkSize)}};
}

[[nodiscard]] constexpr SectionLocalPos block_to_local(BlockPos pos) noexcept {
  return {{
      floor_mod(pos.value.x, ChunkSize),
      floor_mod(pos.value.y, ChunkSize),
      floor_mod(pos.value.z, ChunkSize)}};
}

[[nodiscard]] constexpr BlockPos chunk_local_to_block(ChunkSectionCoord chunk, SectionLocalPos local) noexcept {
  return {{
      chunk.value.x * ChunkSize + local.value.x,
      chunk.value.y * ChunkSize + local.value.y,
      chunk.value.z * ChunkSize + local.value.z}};
}

[[nodiscard]] constexpr ChunkCoord chunk_to_column(ChunkSectionCoord chunk) noexcept {
  return {chunk.value.x, chunk.value.z};
}

[[nodiscard]] constexpr ChunkSectionCoord chunk_section(ChunkCoord chunk, i32 section_y) noexcept {
  return {{chunk.x, section_y, chunk.z}};
}

[[nodiscard]] constexpr RegionCoord chunk_column_to_region(ChunkCoord column) noexcept {
  return {
      floor_div(column.x, RegionSizeInChunks),
      floor_div(column.z, RegionSizeInChunks)};
}

[[nodiscard]] constexpr RegionLocalChunkCoord chunk_column_to_region_local(ChunkCoord column) noexcept {
  return {
      floor_mod(column.x, RegionSizeInChunks),
      floor_mod(column.z, RegionSizeInChunks)};
}

[[nodiscard]] constexpr ChunkSectionCoord neighbor_chunk(ChunkSectionCoord chunk, AxisDirection direction) noexcept {
  switch (direction) {
  case AxisDirection::NegativeX:
    --chunk.value.x;
    break;
  case AxisDirection::PositiveX:
    ++chunk.value.x;
    break;
  case AxisDirection::NegativeY:
    --chunk.value.y;
    break;
  case AxisDirection::PositiveY:
    ++chunk.value.y;
    break;
  case AxisDirection::NegativeZ:
    --chunk.value.z;
    break;
  case AxisDirection::PositiveZ:
    ++chunk.value.z;
    break;
  }
  return chunk;
}

[[nodiscard]] constexpr bool chunk_section_less_xyz(
    ChunkSectionCoord lhs,
    ChunkSectionCoord rhs) noexcept {
  if (lhs.value.x != rhs.value.x) {
    return lhs.value.x < rhs.value.x;
  }
  if (lhs.value.y != rhs.value.y) {
    return lhs.value.y < rhs.value.y;
  }
  return lhs.value.z < rhs.value.z;
}

} // namespace arv

template<>
struct std::hash<arv::ChunkSectionCoord> {
  arv::usize operator()(const arv::ChunkSectionCoord& coord) const noexcept {
    const arv::usize x = std::hash<arv::i32>{}(coord.value.x);
    const arv::usize y = std::hash<arv::i32>{}(coord.value.y);
    const arv::usize z = std::hash<arv::i32>{}(coord.value.z);
    return ((x * 16777619U) ^ y) * 16777619U ^ z;
  }
};

template<>
struct std::hash<arv::BlockPos> {
  arv::usize operator()(const arv::BlockPos& pos) const noexcept {
    const arv::usize x = std::hash<arv::i32>{}(pos.value.x);
    const arv::usize y = std::hash<arv::i32>{}(pos.value.y);
    const arv::usize z = std::hash<arv::i32>{}(pos.value.z);
    return ((x * 16777619U) ^ y) * 16777619U ^ z;
  }
};

template<>
struct std::hash<arv::ChunkCoord> {
  arv::usize operator()(const arv::ChunkCoord& coord) const noexcept {
    const arv::usize x = std::hash<arv::i32>{}(coord.x);
    const arv::usize z = std::hash<arv::i32>{}(coord.z);
    return (x * 16777619U) ^ z;
  }
};

template<>
struct std::hash<arv::RegionCoord> {
  arv::usize operator()(const arv::RegionCoord& coord) const noexcept {
    const arv::usize x = std::hash<arv::i32>{}(coord.x);
    const arv::usize z = std::hash<arv::i32>{}(coord.z);
    return (x * 16777619U) ^ z;
  }
};
