#include "arv/server/permission_config.hpp"

#include "arv/io/files.hpp"

#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace arv::server {
namespace {

using Result = PermissionGroupConfigResult;

[[nodiscard]] PermissionConfigError error(std::string message) {
  return {std::move(message)};
}

[[nodiscard]] std::optional<player::TriState> parse_tri_state(const YAML::Node& node) {
  if (!node || !node.IsScalar()) {
    return std::nullopt;
  }

  return player::tri_state_from_string(node.as<std::string>());
}

[[nodiscard]] Expected<std::vector<std::string>, PermissionConfigError> parse_inherits(
    const YAML::Node& node,
    std::string_view group_id) {
  std::vector<std::string> inherits;
  if (!node) {
    return Expected<std::vector<std::string>, PermissionConfigError>::success(
        std::move(inherits));
  }

  if (node.IsScalar()) {
    inherits.push_back(node.as<std::string>());
    return Expected<std::vector<std::string>, PermissionConfigError>::success(
        std::move(inherits));
  }
  if (!node.IsSequence()) {
    return Expected<std::vector<std::string>, PermissionConfigError>::failure(
        error("permission group '" + std::string{group_id} + "' inherits must be a list"));
  }

  inherits.reserve(node.size());
  for (const YAML::Node& entry : node) {
    if (!entry.IsScalar()) {
      return Expected<std::vector<std::string>, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' inherits entries must be strings"));
    }
    inherits.push_back(entry.as<std::string>());
  }
  return Expected<std::vector<std::string>, PermissionConfigError>::success(
      std::move(inherits));
}

[[nodiscard]] Expected<void, PermissionConfigError> parse_core_permissions(
    player::CorePermissionSet& core,
    const YAML::Node& node,
    std::string_view group_id) {
  if (!node) {
    return Expected<void, PermissionConfigError>::success();
  }
  if (!node.IsMap()) {
    return Expected<void, PermissionConfigError>::failure(
        error("permission group '" + std::string{group_id} + "' core must be a map"));
  }

  for (const auto& entry : node) {
    if (!entry.first.IsScalar()) {
      return Expected<void, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' core keys must be strings"));
    }
    const std::string key = entry.first.as<std::string>();
    const std::optional<player::TriState> value = parse_tri_state(entry.second);
    if (!value.has_value()) {
      return Expected<void, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' has invalid core permission value for '" + key + "'"));
    }
    if (!player::set_core_permission_by_name(core, key, value.value())) {
      return Expected<void, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' has unknown core permission '" + key + "'"));
    }
  }
  return Expected<void, PermissionConfigError>::success();
}

[[nodiscard]] Expected<void, PermissionConfigError> parse_custom_permissions(
    player::CustomPermissionMap& custom,
    const YAML::Node& node,
    std::string_view group_id) {
  if (!node) {
    return Expected<void, PermissionConfigError>::success();
  }
  if (!node.IsMap()) {
    return Expected<void, PermissionConfigError>::failure(
        error("permission group '" + std::string{group_id} + "' permissions must be a map"));
  }

  for (const auto& entry : node) {
    if (!entry.first.IsScalar()) {
      return Expected<void, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' permission keys must be strings"));
    }
    const std::string node_name = entry.first.as<std::string>();
    if (!player::is_valid_custom_permission_node(node_name)) {
      return Expected<void, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' has invalid permission node '" + node_name + "'"));
    }
    const std::optional<player::TriState> value = parse_tri_state(entry.second);
    if (!value.has_value()) {
      return Expected<void, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' has invalid permission value for '" + node_name + "'"));
    }
    custom[node_name] = value.value();
  }
  return Expected<void, PermissionConfigError>::success();
}

[[nodiscard]] Expected<player::PermissionGroup, PermissionConfigError> parse_group(
    std::string id,
    const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return Expected<player::PermissionGroup, PermissionConfigError>::failure(
        error("permission group '" + id + "' must be a map"));
  }

  player::PermissionGroup group;
  group.id = std::move(id);
  if (const YAML::Node priority = node["priority"]) {
    if (!priority.IsScalar()) {
      return Expected<player::PermissionGroup, PermissionConfigError>::failure(
          error("permission group '" + group.id + "' priority must be an integer"));
    }
    group.priority = priority.as<i32>();
  }

  auto inherits = parse_inherits(node["inherits"], group.id);
  if (!inherits) {
    return Expected<player::PermissionGroup, PermissionConfigError>::failure(
        inherits.error());
  }
  group.inherits = std::move(inherits.value());

  auto core = parse_core_permissions(group.core, node["core"], group.id);
  if (!core) {
    return Expected<player::PermissionGroup, PermissionConfigError>::failure(core.error());
  }

  auto custom = parse_custom_permissions(group.custom, node["permissions"], group.id);
  if (!custom) {
    return Expected<player::PermissionGroup, PermissionConfigError>::failure(custom.error());
  }
  if (const YAML::Node custom_node = node["custom"]) {
    custom = parse_custom_permissions(group.custom, custom_node, group.id);
    if (!custom) {
      return Expected<player::PermissionGroup, PermissionConfigError>::failure(
          custom.error());
    }
  }

  return Expected<player::PermissionGroup, PermissionConfigError>::success(
      std::move(group));
}

} // namespace

std::filesystem::path permission_config_file_path(
    const std::filesystem::path& server_save_root) {
  return server_save_root / "permissions.yaml";
}

PermissionGroupConfigResult parse_permission_groups_yaml(std::string_view text) {
  try {
    const YAML::Node root = YAML::Load(std::string{text});
    player::PermissionGroupRegistry registry;
    if (!root || !root["groups"]) {
      return Result::success(std::move(registry));
    }

    const YAML::Node groups = root["groups"];
    if (!groups.IsMap()) {
      return Result::failure(error("permissions.yaml groups must be a map"));
    }

    for (const auto& entry : groups) {
      if (!entry.first.IsScalar()) {
        return Result::failure(error("permission group ids must be strings"));
      }

      auto parsed = parse_group(entry.first.as<std::string>(), entry.second);
      if (!parsed) {
        return Result::failure(parsed.error());
      }

      auto added = registry.add_group(std::move(parsed.value()));
      if (!added) {
        return Result::failure(error(added.error().message));
      }
    }

    return Result::success(std::move(registry));
  } catch (const YAML::Exception& ex) {
    return Result::failure(
        error("failed to parse permissions.yaml: " + std::string{ex.what()}));
  }
}

PermissionGroupConfigResult load_server_permission_groups(
    const std::filesystem::path& server_save_root) {
  const std::filesystem::path path = permission_config_file_path(server_save_root);
  std::error_code error_code;
  if (!std::filesystem::exists(path, error_code)) {
    return Result::success(player::PermissionGroupRegistry{});
  }
  if (error_code) {
    return Result::failure(
        error("failed to inspect permissions file: " + error_code.message()));
  }
  if (!std::filesystem::is_regular_file(path, error_code) || error_code) {
    return Result::failure(error("permissions path is not a file: " + path.string()));
  }

  auto text = io::read_text_file_contents(path);
  if (!text) {
    return Result::failure(error("failed to open permissions file: " + path.string()));
  }
  return parse_permission_groups_yaml(text.value());
}

} // namespace arv::server
