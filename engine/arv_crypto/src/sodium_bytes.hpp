#pragma once

#include <cstddef>
#include <span>

namespace arv::crypto::detail {

[[nodiscard]] inline const unsigned char* bytes_data(
    std::span<const std::byte> bytes) noexcept {
  if (bytes.empty()) {
    return nullptr;
  }
  return reinterpret_cast<const unsigned char*>(bytes.data());
}

} // namespace arv::crypto::detail
