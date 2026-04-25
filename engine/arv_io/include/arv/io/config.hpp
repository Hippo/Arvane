#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace arv::io {

struct WindowConfig {
  u32 width{1280};
  u32 height{720};
  std::string title{"Arvane"};
};

struct SimulationConfig {
  TickRate base_tps{DefaultBaseTps};
};

struct NetworkConfig {
  std::string bind_address{"0.0.0.0"};
  u16 port{27020};
};

struct ChunkDistanceConfig {
  i32 load_distance{3};
  i32 simulation_distance{1};
  i32 render_distance{2};
  i32 stream_distance{2};
  i32 unload_margin{1};
};

struct PathConfig {
  std::filesystem::path project_root{"."};
  std::filesystem::path asset_root{"assets"};
  std::filesystem::path config_file{"config/arvane.yaml"};
};

struct EngineConfig {
  PathConfig paths{};
  WindowConfig window{};
  SimulationConfig simulation{};
  NetworkConfig network{};
  ChunkDistanceConfig chunks{};
};

struct ConfigError {
  std::string message{};
};

using ConfigResult = Expected<EngineConfig, ConfigError>;
using TextFileResult = Expected<std::string, ConfigError>;

struct ConfigLoadOptions {
  std::filesystem::path explicit_config_path{};
  std::filesystem::path explicit_asset_root{};
  std::filesystem::path project_root{"."};
  bool missing_config_is_error{};
};

namespace detail {

template<typename T>
void read_scalar(const YAML::Node& parent, const char* key, T& value) {
  const YAML::Node child = parent[key];
  if (child) {
    value = child.as<T>();
  }
}

inline void read_path_scalar(const YAML::Node& parent, const char* key, std::filesystem::path& value) {
  const YAML::Node child = parent[key];
  if (child) {
    value = child.as<std::string>();
  }
}

inline std::filesystem::path normalize_lexical(std::filesystem::path path) {
  return path.lexically_normal();
}

inline std::filesystem::path resolve_project_relative(
    const std::filesystem::path& project_root,
    const std::filesystem::path& candidate) {
  if (candidate.empty() || candidate.is_absolute()) {
    return normalize_lexical(candidate);
  }
  return normalize_lexical(project_root / candidate);
}

} // namespace detail

inline EngineConfig default_config() {
  return {};
}

inline void resolve_runtime_paths(const ConfigLoadOptions& options, EngineConfig& config) {
  const std::filesystem::path project_root =
      options.project_root.empty() ? std::filesystem::path{"."} : options.project_root;

  config.paths.project_root = detail::normalize_lexical(project_root);

  const std::filesystem::path config_file =
      !options.explicit_config_path.empty()
          ? options.explicit_config_path
          : (config.paths.config_file.empty() ? std::filesystem::path{"config/arvane.yaml"}
                                              : config.paths.config_file);
  config.paths.config_file = detail::resolve_project_relative(config.paths.project_root, config_file);

  const std::filesystem::path asset_root =
      !options.explicit_asset_root.empty()
          ? options.explicit_asset_root
          : (config.paths.asset_root.empty() ? std::filesystem::path{"assets"} : config.paths.asset_root);
  config.paths.asset_root = detail::resolve_project_relative(config.paths.project_root, asset_root);
}

inline ConfigResult parse_config_yaml(std::string_view text) {
  EngineConfig config = default_config();

  try {
    const YAML::Node root = YAML::Load(std::string{text});
    if (!root || root.IsNull()) {
      return ConfigResult::success(std::move(config));
    }

    const YAML::Node paths = root["paths"];
    if (paths) {
      detail::read_path_scalar(paths, "asset_root", config.paths.asset_root);
      detail::read_path_scalar(paths, "config_file", config.paths.config_file);
    }

    const YAML::Node window = root["window"];
    if (window) {
      detail::read_scalar(window, "width", config.window.width);
      detail::read_scalar(window, "height", config.window.height);
      detail::read_scalar(window, "title", config.window.title);
    }

    const YAML::Node simulation = root["simulation"];
    if (simulation) {
      detail::read_scalar(simulation, "base_tps", config.simulation.base_tps);
    }

    const YAML::Node network = root["network"];
    if (network) {
      detail::read_scalar(network, "bind_address", config.network.bind_address);
      detail::read_scalar(network, "port", config.network.port);
    }

    const YAML::Node chunks = root["chunks"];
    if (chunks) {
      detail::read_scalar(chunks, "load_distance", config.chunks.load_distance);
      detail::read_scalar(chunks, "simulation_distance", config.chunks.simulation_distance);
      detail::read_scalar(chunks, "render_distance", config.chunks.render_distance);
      detail::read_scalar(chunks, "stream_distance", config.chunks.stream_distance);
      detail::read_scalar(chunks, "unload_margin", config.chunks.unload_margin);
    }
  } catch (const YAML::Exception& ex) {
    return ConfigResult::failure(ConfigError{ex.what()});
  }

  if (config.simulation.base_tps != 20 && config.simulation.base_tps != 30 &&
      config.simulation.base_tps != 60) {
    return ConfigResult::failure(ConfigError{"simulation.base_tps must be one of 20, 30, or 60"});
  }

  if (config.window.width == 0 || config.window.height == 0) {
    return ConfigResult::failure(ConfigError{"window dimensions must be non-zero"});
  }

  if (config.chunks.load_distance < 0 || config.chunks.simulation_distance < 0 ||
      config.chunks.render_distance < 0 || config.chunks.stream_distance < 0) {
    return ConfigResult::failure(ConfigError{"chunk distances must be non-negative"});
  }
  if (config.chunks.unload_margin < 0) {
    return ConfigResult::failure(ConfigError{"chunks.unload_margin must be non-negative"});
  }
  if (config.chunks.simulation_distance > config.chunks.load_distance) {
    return ConfigResult::failure(ConfigError{"chunks.simulation_distance must be <= chunks.load_distance"});
  }
  if (config.chunks.render_distance > config.chunks.load_distance) {
    return ConfigResult::failure(ConfigError{"chunks.render_distance must be <= chunks.load_distance"});
  }
  if (config.chunks.stream_distance > config.chunks.load_distance) {
    return ConfigResult::failure(ConfigError{"chunks.stream_distance must be <= chunks.load_distance"});
  }

  return ConfigResult::success(std::move(config));
}

inline ConfigResult load_config_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return ConfigResult::failure(ConfigError{"failed to open config file: " + path.string()});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parse_config_yaml(buffer.str());
}

inline ConfigResult load_config(const ConfigLoadOptions& options) {
  EngineConfig config = default_config();
  resolve_runtime_paths(options, config);

  if (!std::filesystem::exists(config.paths.config_file)) {
    if (options.missing_config_is_error) {
      return ConfigResult::failure(ConfigError{"failed to open config file: " + config.paths.config_file.string()});
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

inline ConfigResult load_config_file_or_defaults(const std::filesystem::path& path) {
  ConfigLoadOptions options;
  options.explicit_config_path = path;
  options.missing_config_is_error = false;
  if (path.empty()) {
    options.explicit_config_path.clear();
  }
  return load_config(options);
}

inline TextFileResult read_text_file(const std::filesystem::path& path, std::string_view purpose) {
  std::ifstream input(path);
  if (!input) {
    std::string message = "failed to open ";
    message += purpose;
    message += ": ";
    message += path.string();
    return TextFileResult::failure(ConfigError{std::move(message)});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return TextFileResult::success(buffer.str());
}

} // namespace arv::io
