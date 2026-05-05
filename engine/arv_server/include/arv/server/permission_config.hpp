#pragma once

#include "arv/core/expected.hpp"
#include "arv/player/permissions.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace arv::server {

struct PermissionConfigError {
  std::string message{};
};

using PermissionGroupConfigResult =
    Expected<player::PermissionGroupRegistry, PermissionConfigError>;

[[nodiscard]] std::filesystem::path permission_config_file_path(
    const std::filesystem::path& server_save_root);

[[nodiscard]] PermissionGroupConfigResult parse_permission_groups_yaml(
    std::string_view text);

[[nodiscard]] PermissionGroupConfigResult load_server_permission_groups(
    const std::filesystem::path& server_save_root);

} // namespace arv::server
