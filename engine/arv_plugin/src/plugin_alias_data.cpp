#include "plugin_alias_data.hpp"

#include "arv/core/string.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"

#include "plugin_paths.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <utility>

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
    const io::DataValue& value,
    std::string& error) {
  if (value.kind() != io::DataKind::Object) {
    error = "alias entry must be a map";
    return std::nullopt;
  }

  const io::DataValue* alias_node = value.get("alias");
  if (alias_node == nullptr || alias_node->kind() != io::DataKind::String) {
    error = "alias entry requires a non-empty scalar alias with no whitespace";
    return std::nullopt;
  }
  std::string alias{alias_node->as_string_or({})};
  if (alias.empty() || contains_ascii_space(alias)) {
    error = "alias entry requires a non-empty scalar alias with no whitespace";
    return std::nullopt;
  }

  const io::DataValue* command_node = value.get("command");
  if (command_node == nullptr) {
    command_node = value.get("target");
  }
  if (command_node == nullptr || command_node->kind() != io::DataKind::String) {
    error = "alias entry requires a non-empty scalar command with no whitespace";
    return std::nullopt;
  }
  std::string command_name{command_node->as_string_or({})};
  if (command_name.empty() || contains_ascii_space(command_name)) {
    error = "alias entry requires a non-empty scalar command with no whitespace";
    return std::nullopt;
  }

  bool visible_in_help = false;
  if (const io::DataValue* visible = value.get("visible_in_help"); visible != nullptr) {
    if (visible->kind() != io::DataKind::Bool) {
      error = "alias visible_in_help must be a boolean";
      return std::nullopt;
    }
    visible_in_help = visible->as_bool_or(false);
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
  auto text = io::read_text_file_contents(file);
  if (!text) {
    append_alias_diagnostic(
        result,
        plugin_info,
        relative_file,
        "failed to open commands.yaml");
    return result;
  }
  auto decoded = io::decode_yaml_value(text.value());
  if (!decoded) {
    append_alias_diagnostic(
        result,
        plugin_info,
        relative_file,
        "failed to parse commands.yaml: " + decoded.error().describe());
    return result;
  }
  const io::DataValue& root = decoded.value().root;
  if (root.kind() != io::DataKind::Object) {
    append_alias_diagnostic(
        result,
        plugin_info,
        relative_file,
        "commands.yaml root must be a map");
    return result;
  }
  const io::DataValue* aliases = root.get("aliases");
  if (aliases == nullptr) {
    return result;
  }
  const io::DataValue::Array* alias_array = aliases->array_if();
  if (alias_array == nullptr) {
    append_alias_diagnostic(
        result,
        plugin_info,
        relative_file,
        "commands.yaml aliases must be a list");
    return result;
  }

  for (usize index = 0U; index < alias_array->size(); ++index) {
    std::string error;
    auto alias = command_alias_from_data((*alias_array)[index], error);
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
