#include "plugin_paths.hpp"

namespace arv::plugin::detail {

bool path_is_under_lexically(
    const std::filesystem::path& root,
    const std::filesystem::path& path) {
  const std::filesystem::path normalized_root =
      std::filesystem::absolute(root).lexically_normal();
  const std::filesystem::path normalized_path =
      std::filesystem::absolute(path).lexically_normal();
  auto root_it = normalized_root.begin();
  auto path_it = normalized_path.begin();
  for (; root_it != normalized_root.end(); ++root_it, ++path_it) {
    if (path_it == normalized_path.end() || *path_it != *root_it) {
      return false;
    }
  }
  return true;
}

std::filesystem::path data_relative_path_for(
    const asset::ResourceID& id,
    std::string_view extension) {
  std::filesystem::path path{id.name_space};
  path /= std::filesystem::path{id.path};
  if (!extension.empty()) {
    path += std::string{extension};
  }
  return path;
}

} // namespace arv::plugin::detail
