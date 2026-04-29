#include "arv/io/config.hpp"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace arv::io {
namespace {

constexpr std::string_view kAssetsDirectoryName = "assets";
constexpr std::string_view kPluginsDirectoryName = "plugins";
constexpr std::string_view kConfigDirectoryName = "config";
constexpr std::string_view kSavesDirectoryName = "saves";
constexpr std::string_view kLogsDirectoryName = "logs";
constexpr std::string_view kCacheDirectoryName = "cache";

template<typename T>
void read_scalar(const YAML::Node& parent, const char* key, T& value) {
  const YAML::Node child = parent[key];
  if (child) {
    value = child.as<T>();
  }
}

void read_path_scalar(const YAML::Node& parent, const char* key, std::filesystem::path& value) {
  const YAML::Node child = parent[key];
  if (child) {
    value = child.as<std::string>();
  }
}

std::filesystem::path normalize_lexical(std::filesystem::path path) {
  return path.lexically_normal();
}

bool has_project_assets(const std::filesystem::path& path) {
  return std::filesystem::exists(
             path / std::filesystem::path{DefaultArvaneHomeDirectoryName} /
             std::filesystem::path{kConfigDirectoryName} / "blocks.json") &&
         std::filesystem::exists(
             path / std::filesystem::path{DefaultArvaneHomeDirectoryName} /
             std::filesystem::path{kAssetsDirectoryName} / "shaders" / "world" / "chunk.vert");
}

std::filesystem::path discover_project_root(std::filesystem::path start) {
  std::error_code error;
  if (start.empty()) {
    start = std::filesystem::current_path(error);
    if (error) {
      return std::filesystem::path{"."};
    }
  }

  if (!std::filesystem::is_directory(start, error)) {
    start = start.parent_path();
  }

  start = std::filesystem::absolute(start, error).lexically_normal();
  if (error) {
    start = std::filesystem::path{"."};
  }

  for (std::filesystem::path candidate = start; !candidate.empty(); candidate = candidate.parent_path()) {
    if (has_project_assets(candidate)) {
      return normalize_lexical(candidate);
    }

    if (candidate == candidate.root_path()) {
      break;
    }
  }

  return normalize_lexical(start);
}

std::filesystem::path resolve_project_relative(
    const std::filesystem::path& project_root,
    const std::filesystem::path& candidate) {
  if (candidate.empty() || candidate.is_absolute()) {
    return normalize_lexical(candidate);
  }
  return normalize_lexical(project_root / candidate);
}

std::filesystem::path default_config_path_from(const std::filesystem::path& config_root) {
  return normalize_lexical(config_root / std::filesystem::path{DefaultEngineConfigFileName});
}

std::filesystem::path default_input_bindings_path_from(const std::filesystem::path& config_root) {
  return normalize_lexical(config_root / std::filesystem::path{DefaultInputBindingsFileName});
}

} // namespace

EngineConfig default_config() {
  return {};
}

void resolve_runtime_paths(const ConfigLoadOptions& options, EngineConfig& config) {
  const std::filesystem::path project_root =
      options.project_root.empty() ? discover_project_root({}) : options.project_root;

  config.paths.project_root = normalize_lexical(project_root);

  const std::filesystem::path arvane_home_root =
      !options.explicit_arvane_home_path.empty()
          ? options.explicit_arvane_home_path
          : (config.paths.arvane_home_root.empty()
                 ? std::filesystem::path{DefaultArvaneHomeDirectoryName}
                 : config.paths.arvane_home_root);
  config.paths.arvane_home_root =
      resolve_project_relative(config.paths.project_root, arvane_home_root);

  config.paths.asset_root = normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{kAssetsDirectoryName});
  config.paths.plugins_root = normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{kPluginsDirectoryName});
  config.paths.config_root = normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{kConfigDirectoryName});
  config.paths.saves_root = normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{kSavesDirectoryName});
  config.paths.logs_root = normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{kLogsDirectoryName});
  config.paths.cache_root = normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{kCacheDirectoryName});

  const std::filesystem::path config_file =
      !options.explicit_config_path.empty()
          ? options.explicit_config_path
          : (config.paths.config_file.empty()
                 ? default_config_path_from(config.paths.config_path())
                 : config.paths.config_file);
  config.paths.config_file = resolve_project_relative(config.paths.project_root, config_file);

  const std::filesystem::path input_bindings_file =
      config.paths.input_bindings_file.empty()
          ? default_input_bindings_path_from(config.paths.config_path())
          : config.paths.input_bindings_file;
  config.paths.input_bindings_file =
      resolve_project_relative(config.paths.project_root, input_bindings_file);
}

ConfigResult parse_config_yaml(std::string_view text) {
  EngineConfig config = default_config();

  try {
    const YAML::Node root = YAML::Load(std::string{text});
    if (!root || root.IsNull()) {
      return ConfigResult::success(std::move(config));
    }

    const YAML::Node paths = root["paths"];
    if (paths) {
      read_path_scalar(paths, "arvane_home", config.paths.arvane_home_root);
      read_path_scalar(paths, "config_file", config.paths.config_file);
      read_path_scalar(paths, "input_bindings_file", config.paths.input_bindings_file);
    }

    const YAML::Node window = root["window"];
    if (window) {
      read_scalar(window, "width", config.window.width);
      read_scalar(window, "height", config.window.height);
      read_scalar(window, "title", config.window.title);
    }

    const YAML::Node simulation = root["simulation"];
    if (simulation) {
      read_scalar(simulation, "base_tps", config.simulation.base_tps);
    }

    const YAML::Node network = root["network"];
    if (network) {
      read_scalar(network, "bind_address", config.network.bind_address);
      read_scalar(network, "port", config.network.port);
    }

    const YAML::Node chunks = root["chunks"];
    if (chunks) {
      read_scalar(chunks, "load_distance", config.chunks.load_distance);
      read_scalar(chunks, "simulation_distance", config.chunks.simulation_distance);
      read_scalar(chunks, "render_distance", config.chunks.render_distance);
      read_scalar(chunks, "stream_distance", config.chunks.stream_distance);
      read_scalar(chunks, "unload_margin", config.chunks.unload_margin);
    }

    const YAML::Node jobs = root["jobs"];
    if (jobs) {
      read_scalar(jobs, "worker_threads", config.jobs.worker_threads);
      read_scalar(jobs, "reserved_threads", config.jobs.reserved_threads);
      read_scalar(jobs, "min_worker_threads", config.jobs.min_worker_threads);
      read_scalar(jobs, "max_worker_threads", config.jobs.max_worker_threads);
    }

    const YAML::Node render = root["render"];
    if (render) {
      read_scalar(
          render,
          "max_chunk_mesh_uploads_per_frame",
          config.render.max_chunk_mesh_uploads_per_frame);
    }

    const YAML::Node chunk_pipeline = root["chunk_pipeline"];
    if (chunk_pipeline) {
      read_scalar(
          chunk_pipeline,
          "max_generation_jobs_per_frame",
          config.chunk_pipeline.max_generation_jobs_per_frame);
      read_scalar(
          chunk_pipeline,
          "max_mesh_jobs_per_frame",
          config.chunk_pipeline.max_mesh_jobs_per_frame);
      read_scalar(
          chunk_pipeline,
          "frame_spike_log_ms",
          config.chunk_pipeline.frame_spike_log_ms);
      read_scalar(
          chunk_pipeline,
          "profile_log_interval_ms",
          config.chunk_pipeline.profile_log_interval_ms);
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
  if (config.jobs.min_worker_threads == 0U) {
    return ConfigResult::failure(ConfigError{"jobs.min_worker_threads must be non-zero"});
  }
  if (config.jobs.max_worker_threads < config.jobs.min_worker_threads) {
    return ConfigResult::failure(
        ConfigError{"jobs.max_worker_threads must be >= jobs.min_worker_threads"});
  }
  if (config.render.max_chunk_mesh_uploads_per_frame == 0U) {
    return ConfigResult::failure(
        ConfigError{"render.max_chunk_mesh_uploads_per_frame must be non-zero"});
  }
  if (config.chunk_pipeline.max_generation_jobs_per_frame == 0U ||
      config.chunk_pipeline.max_mesh_jobs_per_frame == 0U) {
    return ConfigResult::failure(
        ConfigError{"chunk_pipeline job budgets must be non-zero"});
  }

  return ConfigResult::success(std::move(config));
}

ConfigResult load_config_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return ConfigResult::failure(ConfigError{"failed to open config file: " + path.string()});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parse_config_yaml(buffer.str());
}

ConfigResult load_config(const ConfigLoadOptions& options) {
  EngineConfig config = default_config();
  resolve_runtime_paths(options, config);

  if (config.paths.config_file.empty()) {
    return ConfigResult::success(std::move(config));
  }

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

ConfigResult load_config_file_or_defaults(const std::filesystem::path& path) {
  ConfigLoadOptions options;
  options.explicit_config_path = path;
  options.missing_config_is_error = false;
  if (path.empty()) {
    options.explicit_config_path.clear();
  }
  return load_config(options);
}

TextFileResult read_text_file(const std::filesystem::path& path, std::string_view purpose) {
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
