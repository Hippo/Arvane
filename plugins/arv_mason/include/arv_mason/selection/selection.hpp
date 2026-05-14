#pragma once

#include <arv/types.h>

#include <cstdint>
#include <optional>
#include <string>

namespace arv_mason {

struct MasonSelection {
  std::optional<ArvWorldHandle> world{};
  std::optional<ArvBlockPos> a{};
  std::optional<ArvBlockPos> b{};
};

struct SelectionBounds {
  ArvWorldHandle world{};
  ArvBlockPos min{};
  ArvBlockPos max{};
  std::uint64_t size_x{};
  std::uint64_t size_y{};
  std::uint64_t size_z{};
  std::uint64_t block_count{};
};

struct SelectionBoundsResult {
  bool ok{};
  SelectionBounds bounds{};
  std::string message{};
};

[[nodiscard]] bool same_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept;
[[nodiscard]] ArvBlockPos min_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept;
[[nodiscard]] ArvBlockPos max_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept;
[[nodiscard]] SelectionBoundsResult normalized_bounds(const MasonSelection& selection);
[[nodiscard]] std::string format_pos(ArvBlockPos pos);
[[nodiscard]] std::string format_bounds(const SelectionBounds& bounds);

} // namespace arv_mason
