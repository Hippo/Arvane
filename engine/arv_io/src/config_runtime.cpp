#include "config_internal.hpp"

#include <optional>
#include <system_error>

#ifndef ARVANE_PROJECT_ROOT
#define ARVANE_PROJECT_ROOT ""
#endif

namespace arv::io::detail {

constexpr std::string_view kAssetsDirectoryName = "assets";
constexpr std::string_view kPacksDirectoryName = "packs";
constexpr std::string_view kPluginsDirectoryName = "plugins";
constexpr std::string_view kConfigDirectoryName = "config";
constexpr std::string_view kSavesDirectoryName = "saves";
constexpr std::string_view kLogsDirectoryName = "logs";
constexpr std::string_view kCacheDirectoryName = "cache";
constexpr std::string_view kLangDirectoryName = "lang";

void read_job_runtime_config(
    const DataValue& jobs,
    const char* key,
    JobSystemConfig::RuntimeConfig& config) {
  const DataValue* runtime = jobs.get(key);
  if (runtime == nullptr) {
    return;
  }

  read_scalar(*runtime, "critical_cpu_workers", config.critical_cpu_workers);
  read_scalar(*runtime, "background_cpu_workers", config.background_cpu_workers);
  read_scalar(*runtime, "blocking_io_workers", config.blocking_io_workers);
  read_scalar(*runtime, "max_chunk_gen_in_flight", config.max_chunk_gen_in_flight);
  read_scalar(*runtime, "max_mesh_jobs_in_flight", config.max_mesh_jobs_in_flight);
  read_scalar(*runtime, "max_lighting_jobs_in_flight", config.max_lighting_jobs_in_flight);
  read_scalar(*runtime, "max_liquid_jobs_in_flight", config.max_liquid_jobs_in_flight);
  read_scalar(*runtime, "max_ai_jobs_in_flight", config.max_ai_jobs_in_flight);
  read_scalar(*runtime, "max_pathfinding_jobs_in_flight", config.max_pathfinding_jobs_in_flight);
}

void read_tick_system_config(
    const DataValue& systems,
    const char* key,
    SimulationConfig::TickSystemConfig& config) {
  const DataValue* system = systems.get(key);
  if (system == nullptr) {
    return;
  }
  read_scalar(*system, "interval", config.interval);
  read_scalar(*system, "budget", config.budget);
  read_scalar(*system, "samples_per_chunk", config.samples_per_chunk);
}

void read_fluid_speed_config(
    const DataValue& fluids,
    const char* key,
    SimulationConfig::FluidSpeedConfig& config) {
  const DataValue* fluid = fluids.get(key);
  if (fluid == nullptr) {
    return;
  }
  read_scalar(*fluid, "flow_interval_ticks", config.flow_interval_ticks);
}

void read_natural_spawning_cap_config(
    const DataValue& caps,
    const char* key,
    NaturalSpawningCategoryCapConfig& config) {
  const DataValue* category = caps.get(key);
  if (category == nullptr) {
    return;
  }
  read_scalar(*category, "per_player", config.per_player);
  read_scalar(*category, "per_world_soft", config.per_world_soft);
  read_scalar(*category, "local_max", config.local_max);
}

void read_natural_spawning_despawn_config(
    const DataValue& despawning,
    const char* key,
    NaturalSpawningDespawnCategoryConfig& config) {
  const DataValue* category = despawning.get(key);
  if (category == nullptr) {
    return;
  }
  read_scalar(*category, "natural_despawn", config.natural_despawn);
  read_scalar(*category, "soft_distance_blocks", config.soft_distance_blocks);
  read_scalar(*category, "hard_distance_blocks", config.hard_distance_blocks);
  read_scalar(*category, "min_lifetime_ticks", config.min_lifetime_ticks);
  read_scalar(*category, "save_transient", config.save_transient);
}

void read_natural_spawning_config(
    const DataValue& root,
    NaturalSpawningConfig& config) {
  const DataValue* natural = root.get("natural_spawning");
  if (natural == nullptr) {
    return;
  }
  read_scalar(*natural, "enabled", config.enabled);
  read_scalar(*natural, "interval_ticks", config.interval_ticks);
  read_scalar(
      *natural,
      "min_distance_from_player_blocks",
      config.min_distance_from_player_blocks);
  read_scalar(
      *natural,
      "max_distance_from_player_blocks",
      config.max_distance_from_player_blocks);
  read_scalar(
      *natural,
      "attempts_per_player_per_category",
      config.attempts_per_player_per_category);
  read_scalar(
      *natural,
      "max_successes_per_player_per_tick",
      config.max_successes_per_player_per_tick);
  read_scalar(
      *natural,
      "max_successes_per_world_per_tick",
      config.max_successes_per_world_per_tick);
  read_scalar(
      *natural,
      "local_density_radius_blocks",
      config.local_density_radius_blocks);
  if (const DataValue* caps = natural->get("caps"); caps != nullptr) {
    read_natural_spawning_cap_config(*caps, "passive", config.caps.passive);
    read_natural_spawning_cap_config(*caps, "hostile", config.caps.hostile);
    read_natural_spawning_cap_config(*caps, "ambient", config.caps.ambient);
    read_natural_spawning_cap_config(*caps, "water", config.caps.water);
    read_natural_spawning_cap_config(*caps, "underground", config.caps.underground);
    read_natural_spawning_cap_config(*caps, "special", config.caps.special);
  }
  if (const DataValue* despawning = natural->get("despawning"); despawning != nullptr) {
    read_natural_spawning_despawn_config(
        *despawning,
        "passive",
        config.despawning.passive);
    read_natural_spawning_despawn_config(
        *despawning,
        "hostile",
        config.despawning.hostile);
    read_natural_spawning_despawn_config(
        *despawning,
        "ambient",
        config.despawning.ambient);
    read_natural_spawning_despawn_config(
        *despawning,
        "water",
        config.despawning.water);
    read_natural_spawning_despawn_config(
        *despawning,
        "underground",
        config.despawning.underground);
    read_natural_spawning_despawn_config(
        *despawning,
        "special",
        config.despawning.special);
  }
}

std::filesystem::path normalize_lexical(std::filesystem::path path) {
  return path.lexically_normal();
}

bool has_project_assets(const std::filesystem::path& path) {
  return std::filesystem::exists(
             path / std::filesystem::path{DefaultArvaneHomeDirectoryName} /
             std::filesystem::path{kPacksDirectoryName} / "enabled.yaml") &&
         std::filesystem::exists(
             path / std::filesystem::path{DefaultArvaneHomeDirectoryName} /
             std::filesystem::path{kPacksDirectoryName} / "arvane_base" / "pack.yaml");
}

std::filesystem::path normalize_project_search_start(std::filesystem::path start) {
  std::error_code error;
  if (start.empty()) {
    start = std::filesystem::current_path(error);
    if (error) {
      return std::filesystem::path{"."};
    }
  }

  error.clear();
  if (!std::filesystem::is_directory(start, error)) {
    const std::filesystem::path parent = start.parent_path();
    if (!parent.empty()) {
      start = parent;
    }
  }

  error.clear();
  start = std::filesystem::absolute(start, error).lexically_normal();
  if (error) {
    return std::filesystem::path{"."};
  }

  return start;
}

std::optional<std::filesystem::path> find_project_root_from(std::filesystem::path start) {
  start = normalize_project_search_start(std::move(start));

  for (std::filesystem::path candidate = start; !candidate.empty(); candidate = candidate.parent_path()) {
    if (has_project_assets(candidate)) {
      return normalize_lexical(candidate);
    }

    if (candidate == candidate.root_path()) {
      break;
    }
  }

  return std::nullopt;
}

std::filesystem::path discover_project_root(std::filesystem::path start) {
  constexpr std::string_view compiled_project_root = ARVANE_PROJECT_ROOT;
  if (!compiled_project_root.empty()) {
    if (std::optional<std::filesystem::path> project_root =
            find_project_root_from(std::filesystem::path{compiled_project_root});
        project_root.has_value()) {
      return normalize_lexical(std::move(project_root.value()));
    }
  }

  start = normalize_project_search_start(std::move(start));
  if (std::optional<std::filesystem::path> project_root = find_project_root_from(start);
      project_root.has_value()) {
    return normalize_lexical(std::move(project_root.value()));
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

std::filesystem::path default_shared_config_path_from(const std::filesystem::path& config_root) {
  return normalize_lexical(config_root / std::filesystem::path{DefaultSharedConfigFileName});
}

std::filesystem::path default_client_config_path_from(const std::filesystem::path& config_root) {
  return normalize_lexical(config_root / std::filesystem::path{DefaultClientConfigFileName});
}

std::filesystem::path default_server_config_path_from(const std::filesystem::path& config_root) {
  return normalize_lexical(config_root / std::filesystem::path{DefaultServerConfigFileName});
}

std::filesystem::path default_input_bindings_path_from(const std::filesystem::path& config_root) {
  return normalize_lexical(config_root / std::filesystem::path{DefaultInputBindingsFileName});
}

std::filesystem::path default_server_list_path_from(const std::filesystem::path& config_root) {
  return normalize_lexical(config_root / std::filesystem::path{DefaultServerListFileName});
}

std::filesystem::path default_profile_config_path_from(
    const std::filesystem::path& config_root,
    ConfigProfile profile) {
  switch (profile) {
    case ConfigProfile::Client:
      return default_client_config_path_from(config_root);
    case ConfigProfile::Server:
      return default_server_config_path_from(config_root);
  }
  return default_client_config_path_from(config_root);
}

bool path_starts_with_parent_reference(const std::filesystem::path& path) {
  auto part = path.begin();
  return part != path.end() && *part == "..";
}

std::string config_path_string(
    const std::filesystem::path& project_root,
    const std::filesystem::path& path) {
  if (path.empty()) {
    return {};
  }

  const std::filesystem::path relative = path.lexically_relative(project_root);
  if (!relative.empty() && !path_starts_with_parent_reference(relative)) {
    return relative.string();
  }

  return path.string();
}

void put_tick_system(
    DataValue& root,
    std::string_view name,
    const SimulationConfig::TickSystemConfig& system) {
  const std::string prefix = "simulation.systems." + std::string{name};
  put(root, prefix + ".interval", system.interval);
  if (system.budget != 0U) {
    put(root, prefix + ".budget", system.budget);
  }
  if (system.samples_per_chunk != 0U) {
    put(root, prefix + ".samples_per_chunk", system.samples_per_chunk);
  }
}

void put_simulation_config(DataValue& root, const SimulationConfig& simulation) {
  put(root, "simulation.base_tps", simulation.base_tps);
  put_tick_system(root, "physics", simulation.systems.physics);
  put_tick_system(root, "entities", simulation.systems.entities);
  put_tick_system(root, "ai", simulation.systems.ai);
  put_tick_system(root, "block_updates", simulation.systems.block_updates);
  put_tick_system(root, "fluids", simulation.systems.fluids);
  put_tick_system(root, "random_ticks", simulation.systems.random_ticks);
  put(root, "simulation.fluids.water.flow_interval_ticks", simulation.fluids.water.flow_interval_ticks);
  put(root, "simulation.fluids.lava.flow_interval_ticks", simulation.fluids.lava.flow_interval_ticks);
}

void put_jobs_runtime_config(
    DataValue& root,
    std::string_view prefix,
    const JobSystemConfig::RuntimeConfig& runtime) {
  put_under(root, prefix, "critical_cpu_workers", runtime.critical_cpu_workers);
  put_under(root, prefix, "background_cpu_workers", runtime.background_cpu_workers);
  put_under(root, prefix, "blocking_io_workers", runtime.blocking_io_workers);
  put_under(root, prefix, "max_chunk_gen_in_flight", runtime.max_chunk_gen_in_flight);
  put_under(root, prefix, "max_mesh_jobs_in_flight", runtime.max_mesh_jobs_in_flight);
  put_under(root, prefix, "max_lighting_jobs_in_flight", runtime.max_lighting_jobs_in_flight);
  put_under(root, prefix, "max_liquid_jobs_in_flight", runtime.max_liquid_jobs_in_flight);
  put_under(root, prefix, "max_ai_jobs_in_flight", runtime.max_ai_jobs_in_flight);
  put_under(root, prefix, "max_pathfinding_jobs_in_flight", runtime.max_pathfinding_jobs_in_flight);
}

void put_jobs_config(DataValue& root, const JobSystemConfig& jobs) {
  put(root, "jobs.worker_threads", jobs.worker_threads);
  put(root, "jobs.reserved_threads", jobs.reserved_threads);
  put(root, "jobs.min_worker_threads", jobs.min_worker_threads);
  put(root, "jobs.max_worker_threads", jobs.max_worker_threads);
  put_jobs_runtime_config(root, "jobs.client", jobs.client);
  put_jobs_runtime_config(root, "jobs.server", jobs.server);
}

void read_runtime_sections(const DataValue& root, EngineConfig& config) {
  const DataValue* paths = root.get("paths");
  if (paths != nullptr) {
    read_path_scalar(*paths, "arvane_home", config.paths.arvane_home_root);
    read_path_scalar(*paths, "config_file", config.paths.config_file);
    read_path_scalar(*paths, "input_bindings_file", config.paths.input_bindings_file);
    read_path_scalar(*paths, "server_list_file", config.paths.server_list_file);
  }

  const DataValue* language = root.get("language");
  if (language != nullptr) {
    read_scalar(*language, "locale", config.language.locale);
    read_scalar(*language, "fallback_locale", config.language.fallback_locale);
  }

  const DataValue* window = root.get("window");
  if (window != nullptr) {
    read_scalar(*window, "width", config.window.width);
    read_scalar(*window, "height", config.window.height);
    read_scalar(*window, "title", config.window.title);
    read_scalar(*window, "vsync", config.window.vsync);
  }

  const DataValue* simulation = root.get("simulation");
  if (simulation != nullptr) {
    read_scalar(*simulation, "base_tps", config.simulation.base_tps);
    const DataValue* systems = simulation->get("systems");
    if (systems != nullptr) {
      read_tick_system_config(*systems, "physics", config.simulation.systems.physics);
      read_tick_system_config(*systems, "entities", config.simulation.systems.entities);
      read_tick_system_config(*systems, "ai", config.simulation.systems.ai);
      read_tick_system_config(*systems, "block_updates", config.simulation.systems.block_updates);
      read_tick_system_config(*systems, "fluids", config.simulation.systems.fluids);
      read_tick_system_config(*systems, "random_ticks", config.simulation.systems.random_ticks);
    }
    const DataValue* fluids = simulation->get("fluids");
    if (fluids != nullptr) {
      read_fluid_speed_config(*fluids, "water", config.simulation.fluids.water);
      read_fluid_speed_config(*fluids, "lava", config.simulation.fluids.lava);
    }
  }

  const DataValue* chunks = root.get("chunks");
  if (chunks != nullptr) {
    read_scalar(*chunks, "load_distance", config.chunks.load_distance);
    read_scalar(*chunks, "simulation_distance", config.chunks.simulation_distance);
    read_scalar(*chunks, "render_distance", config.chunks.render_distance);
    read_scalar(*chunks, "stream_distance", config.chunks.stream_distance);
    read_scalar(*chunks, "unload_margin", config.chunks.unload_margin);
  }

  read_natural_spawning_config(root, config.natural_spawning);

  const DataValue* jobs = root.get("jobs");
  if (jobs != nullptr) {
    read_scalar(*jobs, "worker_threads", config.jobs.worker_threads);
    read_scalar(*jobs, "reserved_threads", config.jobs.reserved_threads);
    read_scalar(*jobs, "min_worker_threads", config.jobs.min_worker_threads);
    read_scalar(*jobs, "max_worker_threads", config.jobs.max_worker_threads);
    read_job_runtime_config(*jobs, "client", config.jobs.client);
    read_job_runtime_config(*jobs, "server", config.jobs.server);
  }

  const DataValue* chunk_pipeline = root.get("chunk_pipeline");
  if (chunk_pipeline != nullptr) {
    read_scalar(
        *chunk_pipeline,
        "max_generation_jobs_per_frame",
        config.chunk_pipeline.max_generation_jobs_per_frame);
    read_scalar(
        *chunk_pipeline,
        "max_generation_results_per_tick",
        config.chunk_pipeline.max_generation_results_per_tick);
    read_scalar(
        *chunk_pipeline,
        "max_lighting_results_per_tick",
        config.chunk_pipeline.max_lighting_results_per_tick);
    read_scalar(
        *chunk_pipeline,
        "max_lighting_sections_per_job",
        config.chunk_pipeline.max_lighting_sections_per_job);
    read_scalar(
        *chunk_pipeline,
        "max_client_chunk_applies_per_frame",
        config.chunk_pipeline.max_client_chunk_applies_per_frame);
    read_scalar(
        *chunk_pipeline,
        "max_client_chunk_unloads_per_frame",
        config.chunk_pipeline.max_client_chunk_unloads_per_frame);
    read_scalar(
        *chunk_pipeline,
        "max_mesh_jobs_per_frame",
        config.chunk_pipeline.max_mesh_jobs_per_frame);
    read_scalar(
        *chunk_pipeline,
        "max_mesh_sections_per_job",
        config.chunk_pipeline.max_mesh_sections_per_job);
    read_scalar(
        *chunk_pipeline,
        "max_mesh_sections_prepared_per_frame",
        config.chunk_pipeline.max_mesh_sections_prepared_per_frame);
    read_scalar(*chunk_pipeline, "frame_spike_log_ms", config.chunk_pipeline.frame_spike_log_ms);
    read_scalar(
        *chunk_pipeline,
        "profile_log_interval_ms",
        config.chunk_pipeline.profile_log_interval_ms);
  }
}

void put_natural_spawning_cap_config(
    DataValue& root,
    std::string_view prefix,
    const NaturalSpawningCategoryCapConfig& config) {
  put_under(root, prefix, "per_player", config.per_player);
  put_under(root, prefix, "per_world_soft", config.per_world_soft);
  put_under(root, prefix, "local_max", config.local_max);
}

void put_natural_spawning_despawn_config(
    DataValue& root,
    std::string_view prefix,
    const NaturalSpawningDespawnCategoryConfig& config) {
  put_under(root, prefix, "natural_despawn", config.natural_despawn);
  put_under(root, prefix, "soft_distance_blocks", config.soft_distance_blocks);
  put_under(root, prefix, "hard_distance_blocks", config.hard_distance_blocks);
  put_under(root, prefix, "min_lifetime_ticks", config.min_lifetime_ticks);
  put_under(root, prefix, "save_transient", config.save_transient);
}

void put_natural_spawning_config(
    DataValue& root,
    const NaturalSpawningConfig& config) {
  put(root, "natural_spawning.enabled", config.enabled);
  put(root, "natural_spawning.interval_ticks", config.interval_ticks);
  put(
      root,
      "natural_spawning.min_distance_from_player_blocks",
      config.min_distance_from_player_blocks);
  put(
      root,
      "natural_spawning.max_distance_from_player_blocks",
      config.max_distance_from_player_blocks);
  put(
      root,
      "natural_spawning.attempts_per_player_per_category",
      config.attempts_per_player_per_category);
  put(
      root,
      "natural_spawning.max_successes_per_player_per_tick",
      config.max_successes_per_player_per_tick);
  put(
      root,
      "natural_spawning.max_successes_per_world_per_tick",
      config.max_successes_per_world_per_tick);
  put(
      root,
      "natural_spawning.local_density_radius_blocks",
      config.local_density_radius_blocks);
  put_natural_spawning_cap_config(
      root,
      "natural_spawning.caps.passive",
      config.caps.passive);
  put_natural_spawning_cap_config(
      root,
      "natural_spawning.caps.hostile",
      config.caps.hostile);
  put_natural_spawning_cap_config(
      root,
      "natural_spawning.caps.ambient",
      config.caps.ambient);
  put_natural_spawning_cap_config(
      root,
      "natural_spawning.caps.water",
      config.caps.water);
  put_natural_spawning_cap_config(
      root,
      "natural_spawning.caps.underground",
      config.caps.underground);
  put_natural_spawning_cap_config(
      root,
      "natural_spawning.caps.special",
      config.caps.special);
  put_natural_spawning_despawn_config(
      root,
      "natural_spawning.despawning.passive",
      config.despawning.passive);
  put_natural_spawning_despawn_config(
      root,
      "natural_spawning.despawning.hostile",
      config.despawning.hostile);
  put_natural_spawning_despawn_config(
      root,
      "natural_spawning.despawning.ambient",
      config.despawning.ambient);
  put_natural_spawning_despawn_config(
      root,
      "natural_spawning.despawning.water",
      config.despawning.water);
  put_natural_spawning_despawn_config(
      root,
      "natural_spawning.despawning.underground",
      config.despawning.underground);
  put_natural_spawning_despawn_config(
      root,
      "natural_spawning.despawning.special",
      config.despawning.special);
}

void put_runtime_full(DataValue& root, const EngineConfig& config) {
  put(root, "paths.arvane_home", config_path_string(config.paths.project_root, config.paths.arvane_home_path()));
  put(root, "paths.input_bindings_file", config_path_string(config.paths.project_root, config.paths.input_bindings_file));
  put(root, "paths.server_list_file", config_path_string(config.paths.project_root, config.paths.server_list_file));
  put(root, "language.locale", config.language.locale);
  put(root, "language.fallback_locale", config.language.fallback_locale);
  put(root, "window.width", config.window.width);
  put(root, "window.height", config.window.height);
  put(root, "window.title", config.window.title);
  put(root, "window.vsync", config.window.vsync);
  put_simulation_config(root, config.simulation);
  put(root, "chunks.load_distance", config.chunks.load_distance);
  put(root, "chunks.simulation_distance", config.chunks.simulation_distance);
  put(root, "chunks.render_distance", config.chunks.render_distance);
  put(root, "chunks.stream_distance", config.chunks.stream_distance);
  put(root, "chunks.unload_margin", config.chunks.unload_margin);
  put_natural_spawning_config(root, config.natural_spawning);
  put_jobs_config(root, config.jobs);
  put(root, "chunk_pipeline.max_generation_jobs_per_frame", config.chunk_pipeline.max_generation_jobs_per_frame);
  put(root, "chunk_pipeline.max_generation_results_per_tick", config.chunk_pipeline.max_generation_results_per_tick);
  put(root, "chunk_pipeline.max_lighting_results_per_tick", config.chunk_pipeline.max_lighting_results_per_tick);
  put(root, "chunk_pipeline.max_lighting_sections_per_job", config.chunk_pipeline.max_lighting_sections_per_job);
  put(root, "chunk_pipeline.max_client_chunk_applies_per_frame", config.chunk_pipeline.max_client_chunk_applies_per_frame);
  put(root, "chunk_pipeline.max_client_chunk_unloads_per_frame", config.chunk_pipeline.max_client_chunk_unloads_per_frame);
  put(root, "chunk_pipeline.max_mesh_jobs_per_frame", config.chunk_pipeline.max_mesh_jobs_per_frame);
  put(root, "chunk_pipeline.max_mesh_sections_per_job", config.chunk_pipeline.max_mesh_sections_per_job);
  put(root, "chunk_pipeline.max_mesh_sections_prepared_per_frame", config.chunk_pipeline.max_mesh_sections_prepared_per_frame);
  put(root, "chunk_pipeline.frame_spike_log_ms", config.chunk_pipeline.frame_spike_log_ms);
  put(root, "chunk_pipeline.profile_log_interval_ms", config.chunk_pipeline.profile_log_interval_ms);
}

void put_runtime_shared(DataValue& root, const EngineConfig& config) {
  put(root, "paths.arvane_home", config_path_string(config.paths.project_root, config.paths.arvane_home_path()));
  put(root, "language.locale", config.language.locale);
  put(root, "language.fallback_locale", config.language.fallback_locale);
  put_simulation_config(root, config.simulation);
  put(root, "chunks.load_distance", config.chunks.load_distance);
  put(root, "chunks.simulation_distance", config.chunks.simulation_distance);
  put(root, "chunks.unload_margin", config.chunks.unload_margin);
  put_natural_spawning_config(root, config.natural_spawning);
  put_jobs_config(root, config.jobs);
  put(root, "chunk_pipeline.max_generation_jobs_per_frame", config.chunk_pipeline.max_generation_jobs_per_frame);
  put(root, "chunk_pipeline.max_generation_results_per_tick", config.chunk_pipeline.max_generation_results_per_tick);
  put(root, "chunk_pipeline.max_lighting_results_per_tick", config.chunk_pipeline.max_lighting_results_per_tick);
  put(root, "chunk_pipeline.max_lighting_sections_per_job", config.chunk_pipeline.max_lighting_sections_per_job);
}

void put_runtime_client(DataValue& root, const EngineConfig& config) {
  put(root, "paths.input_bindings_file", config_path_string(config.paths.project_root, config.paths.input_bindings_file));
  put(root, "paths.server_list_file", config_path_string(config.paths.project_root, config.paths.server_list_file));
  put(root, "language.locale", config.language.locale);
  put(root, "window.width", config.window.width);
  put(root, "window.height", config.window.height);
  put(root, "window.title", config.window.title);
  put(root, "window.vsync", config.window.vsync);
  put(root, "chunks.load_distance", config.chunks.load_distance);
  put(root, "chunks.simulation_distance", config.chunks.simulation_distance);
  put(root, "chunks.render_distance", config.chunks.render_distance);
  put(root, "chunks.stream_distance", config.chunks.stream_distance);
  put(root, "chunk_pipeline.max_client_chunk_applies_per_frame", config.chunk_pipeline.max_client_chunk_applies_per_frame);
  put(root, "chunk_pipeline.max_client_chunk_unloads_per_frame", config.chunk_pipeline.max_client_chunk_unloads_per_frame);
  put(root, "chunk_pipeline.max_mesh_jobs_per_frame", config.chunk_pipeline.max_mesh_jobs_per_frame);
  put(root, "chunk_pipeline.max_mesh_sections_per_job", config.chunk_pipeline.max_mesh_sections_per_job);
  put(root, "chunk_pipeline.max_mesh_sections_prepared_per_frame", config.chunk_pipeline.max_mesh_sections_prepared_per_frame);
  put(root, "chunk_pipeline.frame_spike_log_ms", config.chunk_pipeline.frame_spike_log_ms);
  put(root, "chunk_pipeline.profile_log_interval_ms", config.chunk_pipeline.profile_log_interval_ms);
}

} // namespace arv::io::detail

namespace arv::io {

EngineConfig default_config() {
  return {};
}

void resolve_runtime_paths(const ConfigLoadOptions& options, EngineConfig& config) {
  const std::filesystem::path project_root =
      options.project_root.empty() ? detail::discover_project_root({}) : options.project_root;

  config.paths.project_root = detail::normalize_lexical(project_root);

  const std::filesystem::path arvane_home_root =
      !options.explicit_arvane_home_path.empty()
          ? options.explicit_arvane_home_path
          : (config.paths.arvane_home_root.empty()
                 ? std::filesystem::path{DefaultArvaneHomeDirectoryName}
                 : config.paths.arvane_home_root);
  config.paths.arvane_home_root =
      detail::resolve_project_relative(config.paths.project_root, arvane_home_root);

  config.paths.asset_root = detail::normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{detail::kAssetsDirectoryName});
  config.paths.pack_root = detail::normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{detail::kPacksDirectoryName});
  config.paths.plugins_root = detail::normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{detail::kPluginsDirectoryName});
  config.paths.config_root = detail::normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{detail::kConfigDirectoryName});
  config.paths.saves_root = detail::normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{detail::kSavesDirectoryName});
  config.paths.logs_root = detail::normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{detail::kLogsDirectoryName});
  config.paths.cache_root = detail::normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{detail::kCacheDirectoryName});
  config.paths.lang_root = detail::normalize_lexical(
      config.paths.arvane_home_path() / std::filesystem::path{detail::kLangDirectoryName});

  const std::filesystem::path config_file =
      !options.explicit_config_path.empty()
          ? options.explicit_config_path
          : (config.paths.config_file.empty()
                 ? detail::default_profile_config_path_from(config.paths.config_path(), options.profile)
                 : config.paths.config_file);
  config.paths.config_file = detail::resolve_project_relative(config.paths.project_root, config_file);

  const std::filesystem::path shared_config_file =
      config.paths.shared_config_file.empty()
          ? detail::default_shared_config_path_from(config.paths.config_path())
          : config.paths.shared_config_file;
  config.paths.shared_config_file =
      detail::resolve_project_relative(config.paths.project_root, shared_config_file);

  const std::filesystem::path client_config_file =
      config.paths.client_config_file.empty()
          ? detail::default_client_config_path_from(config.paths.config_path())
          : config.paths.client_config_file;
  config.paths.client_config_file =
      detail::resolve_project_relative(config.paths.project_root, client_config_file);

  const std::filesystem::path server_config_file =
      config.paths.server_config_file.empty()
          ? detail::default_server_config_path_from(config.paths.config_path())
          : config.paths.server_config_file;
  config.paths.server_config_file =
      detail::resolve_project_relative(config.paths.project_root, server_config_file);

  const std::filesystem::path input_bindings_file =
      config.paths.input_bindings_file.empty()
          ? detail::default_input_bindings_path_from(config.paths.config_path())
          : config.paths.input_bindings_file;
  config.paths.input_bindings_file =
      detail::resolve_project_relative(config.paths.project_root, input_bindings_file);

  const std::filesystem::path server_list_file =
      config.paths.server_list_file.empty()
          ? detail::default_server_list_path_from(config.paths.config_path())
          : config.paths.server_list_file;
  config.paths.server_list_file =
      detail::resolve_project_relative(config.paths.project_root, server_list_file);
}

} // namespace arv::io
