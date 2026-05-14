#include "arv_mason/selection/selection.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace arv_mason {
namespace {

[[nodiscard]] bool checked_axis_size(
    std::int32_t min_value,
    std::int32_t max_value,
    std::uint64_t& out) noexcept {
  const auto min64 = static_cast<std::int64_t>(min_value);
  const auto max64 = static_cast<std::int64_t>(max_value);
  if (max64 < min64) {
    return false;
  }
  const auto size = static_cast<std::uint64_t>(max64 - min64) + 1U;
  out = size;
  return true;
}

} // namespace

bool same_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

ArvBlockPos min_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept {
  return {
      std::min(lhs.x, rhs.x),
      std::min(lhs.y, rhs.y),
      std::min(lhs.z, rhs.z),
  };
}

ArvBlockPos max_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept {
  return {
      std::max(lhs.x, rhs.x),
      std::max(lhs.y, rhs.y),
      std::max(lhs.z, rhs.z),
  };
}

SelectionBoundsResult normalized_bounds(const MasonSelection& selection) {
  if (!selection.world.has_value()) {
    return {false, {}, "Selection has no world."};
  }
  if (!selection.a.has_value() || !selection.b.has_value()) {
    return {false, {}, "Selection is incomplete. Set both points with /mason select a and /mason select b."};
  }

  SelectionBounds bounds;
  bounds.world = *selection.world;
  bounds.min = min_pos(*selection.a, *selection.b);
  bounds.max = max_pos(*selection.a, *selection.b);

  if (!checked_axis_size(bounds.min.x, bounds.max.x, bounds.size_x) ||
      !checked_axis_size(bounds.min.y, bounds.max.y, bounds.size_y) ||
      !checked_axis_size(bounds.min.z, bounds.max.z, bounds.size_z)) {
    return {false, {}, "Selection bounds are invalid."};
  }

  if (bounds.size_x != 0U &&
      bounds.size_y > std::numeric_limits<std::uint64_t>::max() / bounds.size_x) {
    return {false, {}, "Selection block count overflowed."};
  }
  const std::uint64_t xy = bounds.size_x * bounds.size_y;
  if (bounds.size_z != 0U &&
      xy > std::numeric_limits<std::uint64_t>::max() / bounds.size_z) {
    return {false, {}, "Selection block count overflowed."};
  }
  bounds.block_count = xy * bounds.size_z;
  return {true, bounds, {}};
}

std::string format_pos(ArvBlockPos pos) {
  std::ostringstream out;
  out << pos.x << ", " << pos.y << ", " << pos.z;
  return out.str();
}

std::string format_bounds(const SelectionBounds& bounds) {
  std::ostringstream out;
  out << "world " << bounds.world << " | AABB min " << format_pos(bounds.min)
      << " max " << format_pos(bounds.max) << " | "
      << bounds.size_x << " x " << bounds.size_y << " x " << bounds.size_z
      << " = " << bounds.block_count << " blocks";
  return out.str();
}

} // namespace arv_mason
