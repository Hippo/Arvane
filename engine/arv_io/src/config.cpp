#include "config_internal.hpp"

#include <filesystem>
#include <optional>

#include <arv/core/log.hpp>

namespace arv::io {
namespace {

ConfigResult decode_engine_config_yaml(std::string_view text) {
  EngineConfig config = default_config();

  auto decoded = decode_yaml_value(text);
  if (!decoded) {
    return ConfigResult::failure(
        ConfigError{"failed to parse config YAML: " + decoded.error().describe()});
  }
  if (decoded.value().root.kind() != DataKind::Null) {
    detail::read_config_node(decoded.value().root, config);
  }

  if (std::optional<ConfigError> error = detail::validate_config(config); error.has_value()) {
    return ConfigResult::failure(std::move(error.value()));
  }

  return ConfigResult::success(std::move(config));
}

ConfigResult apply_config_overlay_file(
    const std::filesystem::path& path,
    EngineConfig& config) {
  auto text = read_text_file(path, "config file");
  if (!text) {
    return ConfigResult::failure(text.error());
  }

  auto decoded = decode_yaml_value(text.value());
  if (!decoded) {
    return ConfigResult::failure(
        ConfigError{"failed to parse config YAML: " + decoded.error().describe()});
  }
  if (decoded.value().root.kind() != DataKind::Null) {
    detail::read_config_node(decoded.value().root, config);
  }

  return ConfigResult::success(config);
}

} // namespace

ConfigResult parse_config_yaml(std::string_view text) {
  return decode_engine_config_yaml(text);
}

ConfigResult load_config_file(const std::filesystem::path& path) {
  auto text = read_text_file(path, "config file");
  if (!text) {
    return ConfigResult::failure(text.error());
  }
  return parse_config_yaml(text.value());
}

ConfigResult load_config(const ConfigLoadOptions& options) {
  EngineConfig config = default_config();
  resolve_runtime_paths(options, config);

  if (config.paths.config_file.empty()) {
    return ConfigResult::success(std::move(config));
  }

  if (options.explicit_config_path.empty()) {
    const bool shared_exists = std::filesystem::exists(config.paths.shared_config_file);
    const bool profile_exists = std::filesystem::exists(config.paths.config_file);
    const bool client_exists = std::filesystem::exists(config.paths.client_config_file);
    const bool server_exists = std::filesystem::exists(config.paths.server_config_file);
    const std::filesystem::path legacy_engine_config =
        detail::default_config_path_from(config.paths.config_path());
    const std::filesystem::path resolved_config_file = config.paths.config_file;
    const std::filesystem::path resolved_shared_config_file = config.paths.shared_config_file;
    const std::filesystem::path resolved_client_config_file = config.paths.client_config_file;
    const std::filesystem::path resolved_server_config_file = config.paths.server_config_file;

    if (!shared_exists && !profile_exists && std::filesystem::exists(legacy_engine_config)) {
      auto parsed = load_config_file(legacy_engine_config);
      if (!parsed) {
        return parsed;
      }

      EngineConfig loaded = std::move(parsed.value());
      loaded.paths.project_root = config.paths.project_root;
      loaded.paths.config_file = config.paths.config_file;
      loaded.paths.shared_config_file = config.paths.shared_config_file;
      loaded.paths.client_config_file = config.paths.client_config_file;
      loaded.paths.server_config_file = config.paths.server_config_file;
      resolve_runtime_paths(options, loaded);
      return ConfigResult::success(std::move(loaded));
    }

    if (!shared_exists || !profile_exists || !client_exists || !server_exists) {
      if (!shared_exists) {
        auto saved_shared =
            detail::save_default_shared_config_file(config.paths.shared_config_file, config);
        if (!saved_shared) {
          core::warn("config", saved_shared.error().message);
        }
      }
      if (!client_exists) {
        auto saved_client =
            detail::save_default_client_config_file(config.paths.client_config_file, config);
        if (!saved_client) {
          core::warn("config", saved_client.error().message);
        }
      }
      if (!server_exists) {
        auto saved_server =
            detail::save_default_server_config_file(config.paths.server_config_file, config);
        if (!saved_server) {
          core::warn("config", saved_server.error().message);
        }
      }
    }

    auto shared = apply_config_overlay_file(config.paths.shared_config_file, config);
    if (!shared) {
      return shared;
    }
    auto profile = apply_config_overlay_file(config.paths.config_file, config);
    if (!profile) {
      return profile;
    }
    if (std::optional<ConfigError> error = detail::validate_config(config); error.has_value()) {
      return ConfigResult::failure(std::move(error.value()));
    }

    EngineConfig loaded = std::move(config);
    loaded.paths.config_file = resolved_config_file;
    loaded.paths.shared_config_file = resolved_shared_config_file;
    loaded.paths.client_config_file = resolved_client_config_file;
    loaded.paths.server_config_file = resolved_server_config_file;
    resolve_runtime_paths(options, loaded);
    return ConfigResult::success(std::move(loaded));
  }

  if (!std::filesystem::exists(config.paths.config_file)) {
    if (options.missing_config_is_error) {
      return ConfigResult::failure(
          ConfigError{"failed to open config file: " + config.paths.config_file.string()});
    }
    auto saved = detail::save_default_config_file(config.paths.config_file, config);
    if (!saved) {
      core::warn("config", saved.error().message);
    }
    return ConfigResult::success(std::move(config));
  }

  auto parsed = load_config_file(config.paths.config_file);
  if (!parsed) {
    return parsed;
  }

  EngineConfig loaded = std::move(parsed.value());
  loaded.paths.project_root = config.paths.project_root;
  loaded.paths.config_file = config.paths.config_file;
  resolve_runtime_paths(options, loaded);
  return ConfigResult::success(std::move(loaded));
}

ConfigResult load_config_file_or_defaults(const std::filesystem::path& path) {
  ConfigLoadOptions options;
  options.explicit_config_path = path;
  options.missing_config_is_error = false;
  if (path.empty()) {
    options.explicit_config_path.clear();
  }
  return load_config(options);
}

ConfigResult save_config_file(const std::filesystem::path& path, const EngineConfig& config) {
  return detail::save_default_config_file(path, config);
}

ConfigResult save_config_files(const EngineConfig& config, ConfigProfile active_profile) {
  if (config.paths.shared_config_file.empty() ||
      config.paths.client_config_file.empty() ||
      config.paths.server_config_file.empty()) {
    return ConfigResult::failure(
        ConfigError{"cannot save split config files without resolved config paths"});
  }

  auto shared = detail::save_default_shared_config_file(config.paths.shared_config_file, config);
  if (!shared) {
    return shared;
  }

  EngineConfig client_config = config;
  if (active_profile != ConfigProfile::Client &&
      std::filesystem::exists(config.paths.client_config_file)) {
    auto client_overlay =
        apply_config_overlay_file(config.paths.client_config_file, client_config);
    if (!client_overlay) {
      return client_overlay;
    }
  }

  EngineConfig server_config = config;
  if (active_profile != ConfigProfile::Server &&
      std::filesystem::exists(config.paths.server_config_file)) {
    auto server_overlay =
        apply_config_overlay_file(config.paths.server_config_file, server_config);
    if (!server_overlay) {
      return server_overlay;
    }
  }

  auto client =
      detail::save_default_client_config_file(config.paths.client_config_file, client_config);
  if (!client) {
    return client;
  }
  auto server =
      detail::save_default_server_config_file(config.paths.server_config_file, server_config);
  if (!server) {
    return server;
  }
  return ConfigResult::success(config);
}

ConfigResult save_client_config_file(const std::filesystem::path& path, const EngineConfig& config) {
  return detail::save_default_client_config_file(path, config);
}

} // namespace arv::io
