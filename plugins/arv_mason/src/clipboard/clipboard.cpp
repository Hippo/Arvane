#include "arv_mason/clipboard/clipboard.hpp"

namespace arv_mason {

std::uint64_t clipboard_index(
    ArvBlockPos size,
    std::uint64_t x,
    std::uint64_t y,
    std::uint64_t z) noexcept {
  return x + static_cast<std::uint64_t>(size.x) *
                 (z + static_cast<std::uint64_t>(size.z) * y);
}

ArvBlockPos add_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept {
  return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

ArvBlockPos sub_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

} // namespace arv_mason
