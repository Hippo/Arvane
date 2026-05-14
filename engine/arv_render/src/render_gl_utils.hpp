#pragma once

#include "arv/core/types.hpp"

#include <algorithm>

namespace arv::render::detail {

[[nodiscard]] inline u32 full_mipmap_max_level(u32 width, u32 height) noexcept {
  u32 size = std::max(width, height);
  u32 level = 0;
  while (size > 1U) {
    size /= 2U;
    ++level;
  }
  return level;
}

} // namespace arv::render::detail
