#include "plugin_alias_data.hpp"

#include <utility>

namespace arv::plugin::detail {

PluginCommandAliasLoadResult load_command_aliases_from_data(
    const asset::PackSource& pack,
    const PluginInfo& plugin_info) {
  (void)plugin_info;
  PluginCommandAliasLoadResult result;
  result.source_file = "pack.yaml";
  result.aliases.reserve(pack.manifest.commands.size());
  for (const asset::PackCommandAlias& alias : pack.manifest.commands) {
    result.aliases.push_back({
        alias.alias,
        alias.command,
        alias.visible_in_help});
  }
  return result;
}

} // namespace arv::plugin::detail
