#pragma once

#include "arv/types.h"
#include "arv/version.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace arv {

inline constexpr std::uint32_t ApiVersion = ARV_API_VERSION;

[[nodiscard]] constexpr ArvStringView view(std::string_view text) noexcept {
  return {text.data(), text.size()};
}

[[nodiscard]] constexpr std::string_view view(ArvStringView text) noexcept {
  return text.data == nullptr ? std::string_view{} : std::string_view{text.data, text.size};
}

[[nodiscard]] inline ArvBytesView bytes(std::span<const std::byte> data) noexcept {
  return {
      reinterpret_cast<const std::uint8_t*>(data.data()),
      data.size(),
  };
}

[[nodiscard]] inline ArvBytesView bytes(std::string_view text) noexcept {
  return {
      reinterpret_cast<const std::uint8_t*>(text.data()),
      text.size(),
  };
}

} // namespace arv
