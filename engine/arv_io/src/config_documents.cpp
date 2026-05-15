#include "config_internal.hpp"

#include <fstream>
#include <system_error>

namespace arv::io::detail {

void read_config_node(const DataValue& root, EngineConfig& config) {
  read_scalar(root, "world", config.server.world);
  read_runtime_sections(root, config);
  read_network_sections(root, config);
  read_plugin_sections(root, config);
  read_gameplay_sections(root, config);
  read_render_ui_sections(root, config);
}

void put_full_config(DataValue& root, const EngineConfig& config) {
  put_runtime_full(root, config);
  put_network_full(root, config);
  put_plugin_policy_config(root, config);
  put_gameplay_full(root, config);
  put_render_ui_full(root, config);
}

ConfigResult write_config_document_file(
    const std::filesystem::path& path,
    const DataValue& document_root,
    const EngineConfig& config) {
  std::error_code error;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return ConfigResult::failure(
          ConfigError{"failed to create config directory: " + error.message()});
    }
  }

  auto encoded = encode_yaml_value(document_root);
  if (!encoded) {
    return ConfigResult::failure(
        ConfigError{"failed to encode config YAML: " + encoded.error().describe()});
  }

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return ConfigResult::failure(
        ConfigError{"failed to open config file for write: " + path.string()});
  }
  output << encoded.value();
  output.flush();
  if (!output) {
    return ConfigResult::failure(
        ConfigError{"failed to write config file: " + path.string()});
  }

  return ConfigResult::success(config);
}

ConfigResult save_default_config_file(const std::filesystem::path& path, const EngineConfig& config) {
  DataValue root = DataValue::object();
  put_full_config(root, config);
  return write_config_document_file(path, root, config);
}

ConfigResult save_default_shared_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config) {
  DataValue root = DataValue::object();
  put_runtime_shared(root, config);
  put_plugin_policy_config(root, config);
  put_gameplay_shared(root, config);
  return write_config_document_file(path, root, config);
}

ConfigResult save_default_client_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config) {
  DataValue root = DataValue::object();
  put_runtime_client(root, config);
  put_gameplay_client(root, config);
  put_network_client(root, config);
  put_render_ui_client(root, config);
  return write_config_document_file(path, root, config);
}

ConfigResult save_default_server_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config) {
  DataValue root = DataValue::object();
  put(root, "world", config.server.world);
  put_network_server(root, config);
  return write_config_document_file(path, root, config);
}

} // namespace arv::io::detail
