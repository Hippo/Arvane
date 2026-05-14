#pragma once

#include "arv/types.h"
#include "arv/version.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace arv {

inline constexpr std::uint32_t ApiVersion = ARV_API_VERSION;

[[nodiscard]] ArvStringView view(std::string_view text) noexcept;

[[nodiscard]] std::string_view view(ArvStringView text) noexcept;

[[nodiscard]] ArvBytesView bytes(std::span<const std::byte> data) noexcept;

[[nodiscard]] ArvBytesView bytes(std::string_view text) noexcept;

} // namespace arv
