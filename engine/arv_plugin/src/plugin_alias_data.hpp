#pragma once

#include "arv/asset/pack.hpp"
#include "arv/plugin/plugin_info.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace arv::plugin::detail {

struct PluginCommandAliasData {
  std::string alias{};
  std::string command_name{};
  bool visible_in_help{};
};

struct PluginCommandAliasLoadResult {
  std::filesystem::path source_file{};
  std::vector<PluginCommandAliasData> aliases{};
  std::vector<PluginError> diagnostics{};
};

[[nodiscard]] PluginCommandAliasLoadResult load_command_aliases_from_data(
    const asset::PackSource& pack,
    const PluginInfo& plugin_info);

} // namespace arv::plugin::detail
