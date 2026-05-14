#pragma once

#include "arv/asset/resource.hpp"

#include <filesystem>
#include <string_view>

namespace arv::plugin::detail {

[[nodiscard]] bool path_is_under_lexically(
    const std::filesystem::path& root,
    const std::filesystem::path& path);

[[nodiscard]] std::filesystem::path data_relative_path_for(
    const asset::ResourceID& id,
    std::string_view extension);

} // namespace arv::plugin::detail
