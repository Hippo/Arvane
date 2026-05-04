#pragma once

#include "arv/core/types.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace arv::crypto {

void fill_random(std::span<std::byte> out) noexcept;

template<usize Size>
[[nodiscard]] std::array<std::byte, Size> random_bytes() noexcept {
  std::array<std::byte, Size> bytes{};
  fill_random(bytes);
  return bytes;
}

} // namespace arv::crypto
