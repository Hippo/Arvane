#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <string>

namespace arv::player {

struct PlayerSelection {
  std::optional<WorldID> world{};
  std::optional<BlockPos> a{};
  std::optional<BlockPos> b{};
};

struct SelectionBounds {
  WorldID world{};
  BlockPos min{};
  BlockPos max{};
  u64 size_x{};
  u64 size_y{};
  u64 size_z{};
  u64 block_count{};
};

struct SelectionBoundsResult {
  bool ok{};
  SelectionBounds bounds{};
  std::string message{};
};

[[nodiscard]] bool same_pos(BlockPos lhs, BlockPos rhs) noexcept;
[[nodiscard]] BlockPos min_pos(BlockPos lhs, BlockPos rhs) noexcept;
[[nodiscard]] BlockPos max_pos(BlockPos lhs, BlockPos rhs) noexcept;
[[nodiscard]] SelectionBoundsResult normalized_bounds(const PlayerSelection& selection);
[[nodiscard]] std::string format_pos(BlockPos pos);
[[nodiscard]] std::string format_bounds(const SelectionBounds& bounds);

} // namespace arv::player
