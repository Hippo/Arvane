#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::crypto {

struct HexError {
  std::string message{};
};

[[nodiscard]] std::string to_lower_hex(std::span<const std::byte> data);
[[nodiscard]] std::string to_prefixed_lower_hex(std::span<const std::byte> data);

[[nodiscard]] Expected<std::vector<std::byte>, HexError> from_lower_hex(
    std::string_view text);
[[nodiscard]] Expected<std::vector<std::byte>, HexError> from_prefixed_lower_hex(
    std::string_view text);
[[nodiscard]] bool is_prefixed_lower_hex(std::string_view text) noexcept;

} // namespace arv::crypto
