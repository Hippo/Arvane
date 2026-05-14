#include "arv/server/permission_config.hpp"

#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"

#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace arv::server {
namespace {

using Result = PermissionGroupConfigResult;

[[nodiscard]] PermissionConfigError error(std::string message) {
  return {std::move(message)};
}

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& root,
    std::string_view key) noexcept {
  return root.get(key);
}

[[nodiscard]] bool is_object(const io::DataValue& value) noexcept {
  return value.kind() == io::DataKind::Object;
}

[[nodiscard]] bool is_string(const io::DataValue& value) noexcept {
  return value.kind() == io::DataKind::String;
}

[[nodiscard]] std::optional<player::TriState> parse_tri_state(const io::DataValue* node) {
  if (node == nullptr || !is_string(*node)) {
    return std::nullopt;
  }

  return player::tri_state_from_string(node->as_string_or({}));
}

[[nodiscard]] Expected<std::vector<std::string>, PermissionConfigError> parse_inherits(
    const io::DataValue* node,
    std::string_view group_id) {
  std::vector<std::string> inherits;
  if (node == nullptr) {
    return Expected<std::vector<std::string>, PermissionConfigError>::success(
        std::move(inherits));
  }

  if (is_string(*node)) {
    inherits.emplace_back(node->as_string_or({}));
    return Expected<std::vector<std::string>, PermissionConfigError>::success(
        std::move(inherits));
  }
  const io::DataValue::Array* array = node->array_if();
  if (array == nullptr) {
    return Expected<std::vector<std::string>, PermissionConfigError>::failure(
        error("permission group '" + std::string{group_id} + "' inherits must be a list"));
  }

  inherits.reserve(array->size());
  for (const io::DataValue& entry : *array) {
    if (!is_string(entry)) {
      return Expected<std::vector<std::string>, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' inherits entries must be strings"));
    }
    inherits.emplace_back(entry.as_string_or({}));
  }
  return Expected<std::vector<std::string>, PermissionConfigError>::success(
      std::move(inherits));
}

[[nodiscard]] Expected<void, PermissionConfigError> parse_core_permissions(
    player::CorePermissionSet& core,
    const io::DataValue* node,
    std::string_view group_id) {
  if (node == nullptr) {
    return Expected<void, PermissionConfigError>::success();
  }
  const io::DataValue::Object* object = node->object_if();
  if (object == nullptr) {
    return Expected<void, PermissionConfigError>::failure(
        error("permission group '" + std::string{group_id} + "' core must be a map"));
  }

  for (const auto& [key, raw_value] : *object) {
    const std::optional<player::TriState> value = parse_tri_state(&raw_value);
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
    const io::DataValue* node,
    std::string_view group_id) {
  if (node == nullptr) {
    return Expected<void, PermissionConfigError>::success();
  }
  const io::DataValue::Object* object = node->object_if();
  if (object == nullptr) {
    return Expected<void, PermissionConfigError>::failure(
        error("permission group '" + std::string{group_id} + "' permissions must be a map"));
  }

  for (const auto& [node_name, raw_value] : *object) {
    if (!player::is_valid_custom_permission_node(node_name)) {
      return Expected<void, PermissionConfigError>::failure(
          error("permission group '" + std::string{group_id} +
                "' has invalid permission node '" + node_name + "'"));
    }
    const std::optional<player::TriState> value = parse_tri_state(&raw_value);
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
    const io::DataValue& node) {
  if (!is_object(node)) {
    return Expected<player::PermissionGroup, PermissionConfigError>::failure(
        error("permission group '" + id + "' must be a map"));
  }

  player::PermissionGroup group;
  group.id = std::move(id);
  if (const io::DataValue* priority = member(node, "priority"); priority != nullptr) {
    if (priority->kind() != io::DataKind::I64 && priority->kind() != io::DataKind::U64) {
      return Expected<player::PermissionGroup, PermissionConfigError>::failure(
          error("permission group '" + group.id + "' priority must be an integer"));
    }
    group.priority = static_cast<i32>(priority->as_i64_or(0));
  }

  auto inherits = parse_inherits(member(node, "inherits"), group.id);
  if (!inherits) {
    return Expected<player::PermissionGroup, PermissionConfigError>::failure(
        inherits.error());
  }
  group.inherits = std::move(inherits.value());

  auto core = parse_core_permissions(group.core, member(node, "core"), group.id);
  if (!core) {
    return Expected<player::PermissionGroup, PermissionConfigError>::failure(core.error());
  }

  auto custom = parse_custom_permissions(group.custom, member(node, "permissions"), group.id);
  if (!custom) {
    return Expected<player::PermissionGroup, PermissionConfigError>::failure(custom.error());
  }
  if (const io::DataValue* custom_node = member(node, "custom"); custom_node != nullptr) {
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
  auto decoded = io::decode_yaml_value(text);
  if (!decoded) {
    return Result::failure(
        error("failed to parse permissions.yaml: " + decoded.error().describe()));
  }
  player::PermissionGroupRegistry registry;
  const io::DataValue* groups = member(decoded.value().root, "groups");
  if (groups == nullptr) {
    return Result::success(std::move(registry));
  }

  const io::DataValue::Object* groups_object = groups->object_if();
  if (groups_object == nullptr) {
    return Result::failure(error("permissions.yaml groups must be a map"));
  }

  for (const auto& [id, group_node] : *groups_object) {
    auto parsed = parse_group(id, group_node);
    if (!parsed) {
      return Result::failure(parsed.error());
    }

    auto added = registry.add_group(std::move(parsed.value()));
    if (!added) {
      return Result::failure(error(added.error().message));
    }
  }

  return Result::success(std::move(registry));
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
