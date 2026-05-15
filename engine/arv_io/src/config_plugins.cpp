#include "config_internal.hpp"

namespace arv::io::detail {

void read_plugin_sections(const DataValue& root, EngineConfig& config) {
  const DataValue* native_plugins = root.get("native_plugins");
  if (native_plugins == nullptr) {
    return;
  }

  read_scalar(*native_plugins, "enabled", config.native_plugins.enabled);
  read_string_sequence(
      *native_plugins,
      "allowed_packs",
      config.native_plugins.allowed_packs);
}

void put_plugin_policy_config(DataValue& root, const EngineConfig& config) {
  put(root, "native_plugins.enabled", config.native_plugins.enabled);
  put_path(
      root,
      "native_plugins.allowed_packs",
      string_array_value(config.native_plugins.allowed_packs));
}

} // namespace arv::io::detail
