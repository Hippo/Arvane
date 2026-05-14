#pragma once

#include "arv/asset/pack.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace arv::plugin::detail {

[[nodiscard]] std::optional<std::filesystem::path> resolve_native_library_path(
    const asset::PackSource& pack,
    std::string_view platform_key,
    std::string& error);

} // namespace arv::plugin::detail
