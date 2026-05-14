#include "arv/player/selection.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace arv::player {
namespace {

[[nodiscard]] bool checked_axis_size(i32 min_value, i32 max_value, u64& out) noexcept {
  const auto min64 = static_cast<i64>(min_value);
  const auto max64 = static_cast<i64>(max_value);
  if (max64 < min64) {
    return false;
  }
  out = static_cast<u64>(max64 - min64) + 1U;
  return true;
}

} // namespace

bool same_pos(BlockPos lhs, BlockPos rhs) noexcept {
  return lhs.value.x == rhs.value.x &&
         lhs.value.y == rhs.value.y &&
         lhs.value.z == rhs.value.z;
}

BlockPos min_pos(BlockPos lhs, BlockPos rhs) noexcept {
  return {{
      std::min(lhs.value.x, rhs.value.x),
      std::min(lhs.value.y, rhs.value.y),
      std::min(lhs.value.z, rhs.value.z),
  }};
}

BlockPos max_pos(BlockPos lhs, BlockPos rhs) noexcept {
  return {{
      std::max(lhs.value.x, rhs.value.x),
      std::max(lhs.value.y, rhs.value.y),
      std::max(lhs.value.z, rhs.value.z),
  }};
}

SelectionBoundsResult normalized_bounds(const PlayerSelection& selection) {
  if (!selection.world.has_value()) {
    return {false, {}, "Selection has no world."};
  }
  if (!selection.a.has_value() || !selection.b.has_value()) {
    return {false, {}, "Selection is incomplete. Set both points with /select a and /select b."};
  }

  SelectionBounds bounds;
  bounds.world = selection.world.value();
  bounds.min = min_pos(selection.a.value(), selection.b.value());
  bounds.max = max_pos(selection.a.value(), selection.b.value());

  if (!checked_axis_size(bounds.min.value.x, bounds.max.value.x, bounds.size_x) ||
      !checked_axis_size(bounds.min.value.y, bounds.max.value.y, bounds.size_y) ||
      !checked_axis_size(bounds.min.value.z, bounds.max.value.z, bounds.size_z)) {
    return {false, {}, "Selection bounds are invalid."};
  }

  if (bounds.size_x != 0U &&
      bounds.size_y > std::numeric_limits<u64>::max() / bounds.size_x) {
    return {false, {}, "Selection block count overflowed."};
  }
  const u64 xy = bounds.size_x * bounds.size_y;
  if (bounds.size_z != 0U &&
      xy > std::numeric_limits<u64>::max() / bounds.size_z) {
    return {false, {}, "Selection block count overflowed."};
  }
  bounds.block_count = xy * bounds.size_z;
  return {true, bounds, {}};
}

std::string format_pos(BlockPos pos) {
  std::ostringstream out;
  out << pos.value.x << ", " << pos.value.y << ", " << pos.value.z;
  return out.str();
}

std::string format_bounds(const SelectionBounds& bounds) {
  std::ostringstream out;
  out << "world " << bounds.world.value << " | AABB min " << format_pos(bounds.min)
      << " max " << format_pos(bounds.max) << " | "
      << bounds.size_x << " x " << bounds.size_y << " x " << bounds.size_z
      << " = " << bounds.block_count << " blocks";
  return out.str();
}

} // namespace arv::player
