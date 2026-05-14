#include "plugin_alias_data.hpp"

#include "arv/core/string.hpp"

#include "plugin_paths.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace arv::plugin::detail {
namespace {

void append_alias_diagnostic(
    PluginCommandAliasLoadResult& result,
    const PluginInfo& plugin_info,
    const std::filesystem::path& file,
    std::string message) {
  result.diagnostics.push_back({
      plugin_info.pack_id,
      "command alias data",
      file,
      std::move(message),
  });
}

[[nodiscard]] std::optional<PluginCommandAliasData> command_alias_from_data(
    const YAML::Node& value,
    std::string& error) {
  if (!value.IsMap()) {
    error = "alias entry must be a map";
    return std::nullopt;
  }

  const YAML::Node alias_node = value["alias"];
  if (!alias_node || !alias_node.IsScalar()) {
    error = "alias entry requires a non-empty scalar alias with no whitespace";
    return std::nullopt;
  }
  std::string alias = alias_node.as<std::string>();
  if (alias.empty() || contains_ascii_space(alias)) {
    error = "alias entry requires a non-empty scalar alias with no whitespace";
    return std::nullopt;
  }

  YAML::Node command_node = value["command"];
  if (!command_node) {
    command_node = value["target"];
  }
  if (!command_node || !command_node.IsScalar()) {
    error = "alias entry requires a non-empty scalar command with no whitespace";
    return std::nullopt;
  }
  std::string command_name = command_node.as<std::string>();
  if (command_name.empty() || contains_ascii_space(command_name)) {
    error = "alias entry requires a non-empty scalar command with no whitespace";
    return std::nullopt;
  }

  bool visible_in_help = false;
  if (const YAML::Node visible = value["visible_in_help"]; visible) {
    if (!visible.IsScalar()) {
      error = "alias visible_in_help must be a boolean";
      return std::nullopt;
    }
    try {
      visible_in_help = visible.as<bool>();
    } catch (const YAML::Exception&) {
      error = "alias visible_in_help must be a boolean";
      return std::nullopt;
    }
  }

  return PluginCommandAliasData{
      std::move(alias),
      std::move(command_name),
      visible_in_help};
}

} // namespace

PluginCommandAliasLoadResult load_command_aliases_from_data(
    const asset::PackSource& pack,
    const PluginInfo& plugin_info) {
  PluginCommandAliasLoadResult result;
  const std::filesystem::path file =
      (pack.root / "data" / "commands.yaml").lexically_normal();
  if (!path_is_under_lexically(pack.root, file)) {
    append_alias_diagnostic(
        result,
        plugin_info,
        file,
        "commands.yaml path escapes pack root");
    return result;
  }
  std::error_code status_error;
  if (!std::filesystem::is_regular_file(file, status_error) || status_error) {
    return result;
  }

  const std::filesystem::path relative_file = file.lexically_relative(pack.root);
  result.source_file = relative_file;
  std::ifstream input(file);
  if (!input) {
    append_alias_diagnostic(
        result,
        plugin_info,
        relative_file,
        "failed to open commands.yaml");
    return result;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  YAML::Node root;
  try {
    root = YAML::Load(buffer.str());
  } catch (const YAML::Exception& ex) {
    append_alias_diagnostic(
        result,
        plugin_info,
        relative_file,
        std::string{"failed to parse commands.yaml: "} + ex.what());
    return result;
  }
  if (!root || !root.IsMap()) {
    append_alias_diagnostic(
        result,
        plugin_info,
        relative_file,
        "commands.yaml root must be a map");
    return result;
  }
  const YAML::Node aliases = root["aliases"];
  if (!aliases) {
    return result;
  }
  if (!aliases.IsSequence()) {
    append_alias_diagnostic(
        result,
        plugin_info,
        relative_file,
        "commands.yaml aliases must be a list");
    return result;
  }

  for (usize index = 0U; index < aliases.size(); ++index) {
    std::string error;
    auto alias = command_alias_from_data(aliases[index], error);
    if (!alias.has_value()) {
      append_alias_diagnostic(
          result,
          plugin_info,
          relative_file,
          "aliases[" + std::to_string(index) + "]: " + error);
      continue;
    }
    result.aliases.push_back(std::move(alias.value()));
  }
  return result;
}

} // namespace arv::plugin::detail
