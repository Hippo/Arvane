#pragma once

#include <filesystem>

namespace arv::persistence::detail {

[[nodiscard]] inline std::filesystem::path temporary_path_for(
    const std::filesystem::path& path) {
  return path.parent_path() / (path.filename().string() + ".tmp");
}

} // namespace arv::persistence::detail
