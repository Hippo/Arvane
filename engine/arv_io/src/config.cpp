#include "arv/io/config.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include <yaml-cpp/yaml.h>

#include <arv/core/log.hpp>
#include <nlohmann/json.hpp>

#ifndef ARVANE_PROJECT_ROOT
#define ARVANE_PROJECT_ROOT ""
#endif

namespace arv::io {
namespace {

constexpr std::string_view kAssetsDirectoryName = "assets";
constexpr std::string_view kPluginsDirectoryName = "plugins";
constexpr std::string_view kConfigDirectoryName = "config";
constexpr std::string_view kSavesDirectoryName = "saves";
constexpr std::string_view kLogsDirectoryName = "logs";
constexpr std::string_view kCacheDirectoryName = "cache";

void emit_render_atlases_config(
    YAML::Emitter& emitter,
    const RenderConfig::AtlasesConfig& atlases);

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

void read_tick_system_config(
    const YAML::Node& systems,
    const char* key,
    SimulationConfig::TickSystemConfig& config) {
  const YAML::Node system = systems[key];
  if (!system) {
    return;
  }
  read_scalar(system, "interval", config.interval);
  read_scalar(system, "budget", config.budget);
  read_scalar(system, "samples_per_chunk", config.samples_per_chunk);
}

void read_fluid_speed_config(
    const YAML::Node& fluids,
    const char* key,
    SimulationConfig::FluidSpeedConfig& config) {
  const YAML::Node fluid = fluids[key];
  if (!fluid) {
    return;
  }
  read_scalar(fluid, "flow_interval_ticks", config.flow_interval_ticks);
}

void read_block_outline_config(
    const YAML::Node& render,
    RenderConfig::BlockOutlineConfig& config) {
  const YAML::Node outline = render["block_outline"];
  if (!outline) {
    return;
  }
  const YAML::Node color = outline["color"];
  if (color && color.IsSequence() && color.size() == 3U) {
    config.color_r = color[0].as<f32>();
    config.color_g = color[1].as<f32>();
    config.color_b = color[2].as<f32>();
  }
  read_scalar(outline, "thickness", config.thickness);
}

void read_render_atlases_config(
    const YAML::Node& render,
    RenderConfig::AtlasesConfig& config) {
  const YAML::Node atlases = render["atlases"];
  if (!atlases) {
    return;
  }
  const auto read_static_atlas = [&](const char* key, RenderConfig::StaticAtlasConfig& atlas_config) {
    const YAML::Node atlas = atlases[key];
    if (!atlas) {
      return;
    }
    read_scalar(atlas, "page_size", atlas_config.page_size);
    read_scalar(atlas, "max_pages", atlas_config.max_pages);
  };
  read_static_atlas("blocks", config.blocks);
  read_static_atlas("entities", config.entities);
  read_static_atlas("ui", config.ui);
  read_static_atlas("fonts", config.fonts);
  read_static_atlas("particles", config.particles);

  const YAML::Node player_skins = atlases["player_skins"];
  if (!player_skins) {
    return;
  }
  read_scalar(player_skins, "page_size", config.player_skins.page_size);
  read_scalar(player_skins, "max_pages", config.player_skins.max_pages);

  const YAML::Node cleanup = player_skins["cleanup"];
  if (cleanup) {
    read_scalar(cleanup, "interval_seconds", config.player_skins.cleanup.interval_seconds);
    read_scalar(
        cleanup,
        "evict_after_unused_seconds",
        config.player_skins.cleanup.evict_after_unused_seconds);
  }

  const YAML::Node pressure = player_skins["pressure_cleanup"];
  if (pressure) {
    read_scalar(pressure, "enabled", config.player_skins.pressure_cleanup.enabled);
    read_scalar(pressure, "usage_threshold", config.player_skins.pressure_cleanup.usage_threshold);
    read_scalar(pressure, "target_usage", config.player_skins.pressure_cleanup.target_usage);
  }
}

void read_physics_config(const YAML::Node& physics, PhysicsConfig& config) {
  read_scalar(physics, "standing_eye_height", config.standing_eye_height);
  read_scalar(physics, "sneaking_eye_height", config.sneaking_eye_height);
  read_scalar(physics, "swimming_eye_height", config.swimming_eye_height);
  read_scalar(physics, "eye_height_lerp_per_second", config.eye_height_lerp_per_second);
  read_scalar(physics, "reach_distance", config.reach_distance);
  read_scalar(physics, "mouse_sensitivity_radians", config.mouse_sensitivity_radians);
  read_scalar(physics, "walk_speed", config.walk_speed);
  read_scalar(physics, "sprint_speed", config.sprint_speed);
  read_scalar(physics, "sneak_speed", config.sneak_speed);
  read_scalar(physics, "air_speed", config.air_speed);
  read_scalar(physics, "fly_speed", config.fly_speed);
  read_scalar(physics, "fly_vertical_speed", config.fly_vertical_speed);
  read_scalar(physics, "jump_velocity", config.jump_velocity);
  read_scalar(physics, "gravity_per_second", config.gravity_per_second);
  read_scalar(physics, "terminal_velocity", config.terminal_velocity);
  read_scalar(physics, "body_width", config.body_width);
  read_scalar(physics, "body_height", config.body_height);
  read_scalar(physics, "sneaking_body_height", config.sneaking_body_height);
  read_scalar(physics, "swimming_body_width", config.swimming_body_width);
  read_scalar(physics, "swimming_body_height", config.swimming_body_height);
  read_scalar(physics, "sneak_support_probe_depth", config.sneak_support_probe_depth);
  read_scalar(physics, "collision_epsilon", config.collision_epsilon);
  read_scalar(physics, "surface_swim_speed", config.surface_swim_speed);
  read_scalar(physics, "underwater_swim_speed", config.underwater_swim_speed);
  read_scalar(physics, "fast_surface_swim_speed", config.fast_surface_swim_speed);
  read_scalar(physics, "fast_underwater_swim_speed", config.fast_underwater_swim_speed);
  read_scalar(physics, "upward_swim_acceleration", config.upward_swim_acceleration);
  read_scalar(physics, "downward_swim_acceleration", config.downward_swim_acceleration);
  read_scalar(physics, "liquid_drag", config.liquid_drag);
  read_scalar(physics, "surface_float_depth", config.surface_float_depth);
  read_scalar(physics, "surface_buoyancy", config.surface_buoyancy);
  read_scalar(physics, "underwater_buoyancy", config.underwater_buoyancy);
  read_scalar(physics, "breath_seconds", config.breath_seconds);
  read_scalar(physics, "max_head_body_yaw_radians", config.max_head_body_yaw_radians);
}

void read_gameplay_config(const YAML::Node& gameplay, GameplayConfig& config) {
  const YAML::Node block_breaking = gameplay["block_breaking"];
  if (block_breaking) {
    read_scalar(
        block_breaking,
        "base_seconds_per_hardness",
        config.block_breaking.base_seconds_per_hardness);
    read_scalar(
        block_breaking,
        "creative_inventory_multiplier",
        config.block_breaking.creative_inventory_multiplier);
    read_scalar(
        block_breaking,
        "hand_speed_multiplier",
        config.block_breaking.hand_speed_multiplier);
    read_scalar(
        block_breaking,
        "wrong_tool_speed_multiplier",
        config.block_breaking.wrong_tool_speed_multiplier);
  }

  const YAML::Node dropped_items = gameplay["dropped_items"];
  if (dropped_items) {
    read_scalar(dropped_items, "pickup_radius", config.dropped_items.pickup_radius);
    read_scalar(
        dropped_items,
        "pickup_delay_seconds",
        config.dropped_items.pickup_delay_seconds);
  }
}

[[nodiscard]] bool is_supported_skin_resolution(u16 size) noexcept {
  return size == 64U || size == 128U || size == 256U || size == 512U ||
         size == 1024U || size == 2048U;
}

void read_skins_config(const YAML::Node& skins, SkinConfig& config) {
  read_scalar(skins, "enabled", config.enabled);
  read_scalar(skins, "allow_uploads", config.allow_uploads);

  const YAML::Node resolutions = skins["resolutions"];
  if (resolutions) {
    config.resolutions.clear();
    if (resolutions.IsSequence()) {
      config.resolutions.reserve(resolutions.size());
      for (const YAML::Node& node : resolutions) {
        SkinResolutionConfig resolution;
        resolution.size = node["size"].as<u16>();
        resolution.enabled = true;
        read_scalar(node, "enabled", resolution.enabled);
        read_scalar(node, "permission", resolution.permission);
        resolution.max_upload_bytes = default_skin_max_upload_bytes(resolution.size);
        resolution.max_decoded_bytes = default_skin_max_decoded_bytes(resolution.size);
        read_scalar(node, "max_upload_bytes", resolution.max_upload_bytes);
        read_scalar(node, "max_decoded_bytes", resolution.max_decoded_bytes);
        config.resolutions.push_back(std::move(resolution));
      }
    }
  }

  const YAML::Node uploads = skins["uploads"];
  if (uploads) {
    read_scalar(uploads, "max_chunk_bytes", config.uploads.max_chunk_bytes);
    read_scalar(uploads, "chunk_threshold_bytes", config.uploads.chunk_threshold_bytes);
    read_scalar(uploads, "max_active_per_connection", config.uploads.max_active_per_connection);
    read_scalar(uploads, "max_active_global", config.uploads.max_active_global);
    read_scalar(uploads, "timeout_seconds", config.uploads.timeout_seconds);
    read_scalar(uploads, "idle_timeout_seconds", config.uploads.idle_timeout_seconds);
    read_scalar(
        uploads,
        "max_begin_attempts_per_minute",
        config.uploads.max_begin_attempts_per_minute);
    read_scalar(
        uploads,
        "max_completed_uploads_per_minute",
        config.uploads.max_completed_uploads_per_minute);
    read_scalar(
        uploads,
        "max_completed_uploads_per_hour",
        config.uploads.max_completed_uploads_per_hour);
    read_scalar(
        uploads,
        "max_upload_bytes_per_hour",
        config.uploads.max_upload_bytes_per_hour);
    read_scalar(
        uploads,
        "max_failed_decodes_per_minute",
        config.uploads.max_failed_decodes_per_minute);
  }

  const YAML::Node downloads = skins["downloads"];
  if (downloads) {
    read_scalar(
        downloads,
        "max_requests_per_second",
        config.downloads.max_requests_per_second);
    read_scalar(
        downloads,
        "max_requests_per_minute",
        config.downloads.max_requests_per_minute);
    read_scalar(
        downloads,
        "max_bytes_per_minute",
        config.downloads.max_bytes_per_minute);
    read_scalar(
        downloads,
        "max_in_flight_per_connection",
        config.downloads.max_in_flight_per_connection);
    read_scalar(
        downloads,
        "require_advertised_hash",
        config.downloads.require_advertised_hash);
    read_scalar(downloads, "max_batch_size", config.downloads.max_batch_size);
  }

  const YAML::Node storage = skins["storage"];
  if (storage) {
    read_scalar(storage, "max_total_skin_bytes", config.storage.max_total_skin_bytes);
    read_scalar(storage, "max_skins_per_profile", config.storage.max_skins_per_profile);
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

ConfigResult save_default_config_file(const std::filesystem::path& path, const EngineConfig& config) {
  std::error_code error;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return ConfigResult::failure(
          ConfigError{"failed to create config directory: " + error.message()});
    }
  }

  YAML::Emitter emitter;
  emitter << YAML::BeginMap;
  emitter << YAML::Key << "paths" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "arvane_home" << YAML::Value
          << config_path_string(config.paths.project_root, config.paths.arvane_home_path());
  emitter << YAML::Key << "input_bindings_file" << YAML::Value
          << config_path_string(config.paths.project_root, config.paths.input_bindings_file);
  emitter << YAML::Key << "server_list_file" << YAML::Value
          << config_path_string(config.paths.project_root, config.paths.server_list_file);
  emitter << YAML::EndMap;

  emitter << YAML::Key << "window" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "width" << YAML::Value << config.window.width;
  emitter << YAML::Key << "height" << YAML::Value << config.window.height;
  emitter << YAML::Key << "title" << YAML::Value << config.window.title;
  emitter << YAML::Key << "vsync" << YAML::Value << config.window.vsync;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "simulation" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "base_tps" << YAML::Value << config.simulation.base_tps;
  emitter << YAML::Key << "systems" << YAML::Value << YAML::BeginMap;
  auto emit_system = [&emitter](
                         std::string_view name,
                         const SimulationConfig::TickSystemConfig& system) {
    emitter << YAML::Key << std::string{name} << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "interval" << YAML::Value << system.interval;
    if (system.budget != 0U) {
      emitter << YAML::Key << "budget" << YAML::Value << system.budget;
    }
    if (system.samples_per_chunk != 0U) {
      emitter << YAML::Key << "samples_per_chunk" << YAML::Value << system.samples_per_chunk;
    }
    emitter << YAML::EndMap;
  };
  emit_system("physics", config.simulation.systems.physics);
  emit_system("entities", config.simulation.systems.entities);
  emit_system("ai", config.simulation.systems.ai);
  emit_system("block_updates", config.simulation.systems.block_updates);
  emit_system("fluids", config.simulation.systems.fluids);
  emit_system("random_ticks", config.simulation.systems.random_ticks);
  emitter << YAML::EndMap;
  emitter << YAML::Key << "fluids" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "water" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "flow_interval_ticks" << YAML::Value
          << config.simulation.fluids.water.flow_interval_ticks;
  emitter << YAML::EndMap;
  emitter << YAML::Key << "lava" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "flow_interval_ticks" << YAML::Value
          << config.simulation.fluids.lava.flow_interval_ticks;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "network" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "bind_address" << YAML::Value << config.network.bind_address;
  emitter << YAML::Key << "port" << YAML::Value << config.network.port;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "chunks" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "load_distance" << YAML::Value << config.chunks.load_distance;
  emitter << YAML::Key << "simulation_distance" << YAML::Value
          << config.chunks.simulation_distance;
  emitter << YAML::Key << "render_distance" << YAML::Value << config.chunks.render_distance;
  emitter << YAML::Key << "stream_distance" << YAML::Value << config.chunks.stream_distance;
  emitter << YAML::Key << "unload_margin" << YAML::Value << config.chunks.unload_margin;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "jobs" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "worker_threads" << YAML::Value << config.jobs.worker_threads;
  emitter << YAML::Key << "reserved_threads" << YAML::Value << config.jobs.reserved_threads;
  emitter << YAML::Key << "min_worker_threads" << YAML::Value << config.jobs.min_worker_threads;
  emitter << YAML::Key << "max_worker_threads" << YAML::Value << config.jobs.max_worker_threads;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "physics" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "standing_eye_height" << YAML::Value
          << config.physics.standing_eye_height;
  emitter << YAML::Key << "sneaking_eye_height" << YAML::Value
          << config.physics.sneaking_eye_height;
  emitter << YAML::Key << "swimming_eye_height" << YAML::Value
          << config.physics.swimming_eye_height;
  emitter << YAML::Key << "eye_height_lerp_per_second" << YAML::Value
          << config.physics.eye_height_lerp_per_second;
  emitter << YAML::Key << "reach_distance" << YAML::Value << config.physics.reach_distance;
  emitter << YAML::Key << "mouse_sensitivity_radians" << YAML::Value
          << config.physics.mouse_sensitivity_radians;
  emitter << YAML::Key << "walk_speed" << YAML::Value << config.physics.walk_speed;
  emitter << YAML::Key << "sprint_speed" << YAML::Value << config.physics.sprint_speed;
  emitter << YAML::Key << "sneak_speed" << YAML::Value << config.physics.sneak_speed;
  emitter << YAML::Key << "air_speed" << YAML::Value << config.physics.air_speed;
  emitter << YAML::Key << "fly_speed" << YAML::Value << config.physics.fly_speed;
  emitter << YAML::Key << "fly_vertical_speed" << YAML::Value
          << config.physics.fly_vertical_speed;
  emitter << YAML::Key << "jump_velocity" << YAML::Value << config.physics.jump_velocity;
  emitter << YAML::Key << "gravity_per_second" << YAML::Value
          << config.physics.gravity_per_second;
  emitter << YAML::Key << "terminal_velocity" << YAML::Value
          << config.physics.terminal_velocity;
  emitter << YAML::Key << "body_width" << YAML::Value << config.physics.body_width;
  emitter << YAML::Key << "body_height" << YAML::Value << config.physics.body_height;
  emitter << YAML::Key << "sneaking_body_height" << YAML::Value
          << config.physics.sneaking_body_height;
  emitter << YAML::Key << "swimming_body_width" << YAML::Value
          << config.physics.swimming_body_width;
  emitter << YAML::Key << "swimming_body_height" << YAML::Value
          << config.physics.swimming_body_height;
  emitter << YAML::Key << "sneak_support_probe_depth" << YAML::Value
          << config.physics.sneak_support_probe_depth;
  emitter << YAML::Key << "collision_epsilon" << YAML::Value
          << config.physics.collision_epsilon;
  emitter << YAML::Key << "surface_swim_speed" << YAML::Value
          << config.physics.surface_swim_speed;
  emitter << YAML::Key << "underwater_swim_speed" << YAML::Value
          << config.physics.underwater_swim_speed;
  emitter << YAML::Key << "fast_surface_swim_speed" << YAML::Value
          << config.physics.fast_surface_swim_speed;
  emitter << YAML::Key << "fast_underwater_swim_speed" << YAML::Value
          << config.physics.fast_underwater_swim_speed;
  emitter << YAML::Key << "upward_swim_acceleration" << YAML::Value
          << config.physics.upward_swim_acceleration;
  emitter << YAML::Key << "downward_swim_acceleration" << YAML::Value
          << config.physics.downward_swim_acceleration;
  emitter << YAML::Key << "liquid_drag" << YAML::Value << config.physics.liquid_drag;
  emitter << YAML::Key << "surface_float_depth" << YAML::Value
          << config.physics.surface_float_depth;
  emitter << YAML::Key << "surface_buoyancy" << YAML::Value
          << config.physics.surface_buoyancy;
  emitter << YAML::Key << "underwater_buoyancy" << YAML::Value
          << config.physics.underwater_buoyancy;
  emitter << YAML::Key << "breath_seconds" << YAML::Value << config.physics.breath_seconds;
  emitter << YAML::Key << "max_head_body_yaw_radians" << YAML::Value
          << config.physics.max_head_body_yaw_radians;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "gameplay" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "block_breaking" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "base_seconds_per_hardness" << YAML::Value
          << config.gameplay.block_breaking.base_seconds_per_hardness;
  emitter << YAML::Key << "creative_inventory_multiplier" << YAML::Value
          << config.gameplay.block_breaking.creative_inventory_multiplier;
  emitter << YAML::Key << "hand_speed_multiplier" << YAML::Value
          << config.gameplay.block_breaking.hand_speed_multiplier;
  emitter << YAML::Key << "wrong_tool_speed_multiplier" << YAML::Value
          << config.gameplay.block_breaking.wrong_tool_speed_multiplier;
  emitter << YAML::EndMap;
  emitter << YAML::Key << "dropped_items" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "pickup_radius" << YAML::Value
          << config.gameplay.dropped_items.pickup_radius;
  emitter << YAML::Key << "pickup_delay_seconds" << YAML::Value
          << config.gameplay.dropped_items.pickup_delay_seconds;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "render" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "max_chunk_mesh_uploads_per_frame" << YAML::Value
          << config.render.max_chunk_mesh_uploads_per_frame;
  emitter << YAML::Key << "frame_pacing_enabled" << YAML::Value
          << config.render.frame_pacing_enabled;
  emitter << YAML::Key << "frame_pacing_target_fps" << YAML::Value
          << config.render.frame_pacing_target_fps;
  emitter << YAML::Key << "frame_pacing_spin_us" << YAML::Value
          << config.render.frame_pacing_spin_us;
  emitter << YAML::Key << "show_local_player_body" << YAML::Value
          << config.render.show_local_player_body;
  emitter << YAML::Key << "block_outline" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "color" << YAML::Value << YAML::Flow
          << YAML::BeginSeq
          << config.render.block_outline.color_r
          << config.render.block_outline.color_g
          << config.render.block_outline.color_b
          << YAML::EndSeq;
  emitter << YAML::Key << "thickness" << YAML::Value
          << config.render.block_outline.thickness;
  emitter << YAML::EndMap;
  emit_render_atlases_config(emitter, config.render.atlases);
  emitter << YAML::EndMap;

  emitter << YAML::Key << "chunk_pipeline" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "max_generation_jobs_per_frame" << YAML::Value
          << config.chunk_pipeline.max_generation_jobs_per_frame;
  emitter << YAML::Key << "max_generation_results_per_tick" << YAML::Value
          << config.chunk_pipeline.max_generation_results_per_tick;
  emitter << YAML::Key << "max_lighting_results_per_tick" << YAML::Value
          << config.chunk_pipeline.max_lighting_results_per_tick;
  emitter << YAML::Key << "max_client_chunk_applies_per_frame" << YAML::Value
          << config.chunk_pipeline.max_client_chunk_applies_per_frame;
  emitter << YAML::Key << "max_client_chunk_unloads_per_frame" << YAML::Value
          << config.chunk_pipeline.max_client_chunk_unloads_per_frame;
  emitter << YAML::Key << "max_mesh_jobs_per_frame" << YAML::Value
          << config.chunk_pipeline.max_mesh_jobs_per_frame;
  emitter << YAML::Key << "frame_spike_log_ms" << YAML::Value
          << config.chunk_pipeline.frame_spike_log_ms;
  emitter << YAML::Key << "profile_log_interval_ms" << YAML::Value
          << config.chunk_pipeline.profile_log_interval_ms;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return ConfigResult::failure(
        ConfigError{"failed to open config file for write: " + path.string()});
  }
  output << emitter.c_str();
  output << '\n';
  output.flush();
  if (!output) {
    return ConfigResult::failure(
        ConfigError{"failed to write config file: " + path.string()});
  }

  return ConfigResult::success(config);
}

void emit_skins_config(YAML::Emitter& emitter, const SkinConfig& skins) {
  emitter << YAML::Key << "skins" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "enabled" << YAML::Value << skins.enabled;
  emitter << YAML::Key << "allow_uploads" << YAML::Value << skins.allow_uploads;
  emitter << YAML::Key << "resolutions" << YAML::Value << YAML::BeginSeq;
  for (const SkinResolutionConfig& resolution : skins.resolutions) {
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "size" << YAML::Value << resolution.size;
    emitter << YAML::Key << "enabled" << YAML::Value << resolution.enabled;
    if (!resolution.permission.empty()) {
      emitter << YAML::Key << "permission" << YAML::Value << resolution.permission;
    }
    if (resolution.max_upload_bytes != default_skin_max_upload_bytes(resolution.size)) {
      emitter << YAML::Key << "max_upload_bytes" << YAML::Value
              << resolution.max_upload_bytes;
    } else if (resolution.size == 64U) {
      emitter << YAML::Key << "max_upload_bytes" << YAML::Value
              << resolution.max_upload_bytes;
    }
    if (resolution.max_decoded_bytes != default_skin_max_decoded_bytes(resolution.size)) {
      emitter << YAML::Key << "max_decoded_bytes" << YAML::Value
              << resolution.max_decoded_bytes;
    } else if (resolution.size == 64U) {
      emitter << YAML::Key << "max_decoded_bytes" << YAML::Value
              << resolution.max_decoded_bytes;
    }
    emitter << YAML::EndMap;
  }
  emitter << YAML::EndSeq;
  emitter << YAML::Key << "uploads" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "max_chunk_bytes" << YAML::Value
          << skins.uploads.max_chunk_bytes;
  emitter << YAML::Key << "chunk_threshold_bytes" << YAML::Value
          << skins.uploads.chunk_threshold_bytes;
  emitter << YAML::Key << "max_active_per_connection" << YAML::Value
          << skins.uploads.max_active_per_connection;
  emitter << YAML::Key << "max_active_global" << YAML::Value
          << skins.uploads.max_active_global;
  emitter << YAML::Key << "timeout_seconds" << YAML::Value
          << skins.uploads.timeout_seconds;
  emitter << YAML::Key << "idle_timeout_seconds" << YAML::Value
          << skins.uploads.idle_timeout_seconds;
  emitter << YAML::Key << "max_begin_attempts_per_minute" << YAML::Value
          << skins.uploads.max_begin_attempts_per_minute;
  emitter << YAML::Key << "max_completed_uploads_per_minute" << YAML::Value
          << skins.uploads.max_completed_uploads_per_minute;
  emitter << YAML::Key << "max_completed_uploads_per_hour" << YAML::Value
          << skins.uploads.max_completed_uploads_per_hour;
  emitter << YAML::Key << "max_upload_bytes_per_hour" << YAML::Value
          << skins.uploads.max_upload_bytes_per_hour;
  emitter << YAML::Key << "max_failed_decodes_per_minute" << YAML::Value
          << skins.uploads.max_failed_decodes_per_minute;
  emitter << YAML::EndMap;
  emitter << YAML::Key << "downloads" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "max_requests_per_second" << YAML::Value
          << skins.downloads.max_requests_per_second;
  emitter << YAML::Key << "max_requests_per_minute" << YAML::Value
          << skins.downloads.max_requests_per_minute;
  emitter << YAML::Key << "max_bytes_per_minute" << YAML::Value
          << skins.downloads.max_bytes_per_minute;
  emitter << YAML::Key << "max_in_flight_per_connection" << YAML::Value
          << skins.downloads.max_in_flight_per_connection;
  emitter << YAML::Key << "require_advertised_hash" << YAML::Value
          << skins.downloads.require_advertised_hash;
  emitter << YAML::Key << "max_batch_size" << YAML::Value
          << skins.downloads.max_batch_size;
  emitter << YAML::EndMap;
  emitter << YAML::Key << "storage" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "max_total_skin_bytes" << YAML::Value
          << skins.storage.max_total_skin_bytes;
  emitter << YAML::Key << "max_skins_per_profile" << YAML::Value
          << skins.storage.max_skins_per_profile;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;
}

void emit_render_atlases_config(
    YAML::Emitter& emitter,
    const RenderConfig::AtlasesConfig& atlases) {
  emitter << YAML::Key << "atlases" << YAML::Value << YAML::BeginMap;
  const auto emit_static_atlas = [&emitter](
                                     std::string_view name,
                                     const RenderConfig::StaticAtlasConfig& atlas) {
    emitter << YAML::Key << std::string{name} << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "page_size" << YAML::Value << atlas.page_size;
    emitter << YAML::Key << "max_pages" << YAML::Value << atlas.max_pages;
    emitter << YAML::EndMap;
  };
  emit_static_atlas("blocks", atlases.blocks);
  emit_static_atlas("entities", atlases.entities);
  emit_static_atlas("ui", atlases.ui);
  emit_static_atlas("fonts", atlases.fonts);
  emitter << YAML::Key << "player_skins" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "page_size" << YAML::Value << atlases.player_skins.page_size;
  emitter << YAML::Key << "max_pages" << YAML::Value << atlases.player_skins.max_pages;
  emitter << YAML::Key << "cleanup" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "interval_seconds" << YAML::Value
          << atlases.player_skins.cleanup.interval_seconds;
  emitter << YAML::Key << "evict_after_unused_seconds" << YAML::Value
          << atlases.player_skins.cleanup.evict_after_unused_seconds;
  emitter << YAML::EndMap;
  emitter << YAML::Key << "pressure_cleanup" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "enabled" << YAML::Value
          << atlases.player_skins.pressure_cleanup.enabled;
  emitter << YAML::Key << "usage_threshold" << YAML::Value
          << atlases.player_skins.pressure_cleanup.usage_threshold;
  emitter << YAML::Key << "target_usage" << YAML::Value
          << atlases.player_skins.pressure_cleanup.target_usage;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;
  emit_static_atlas("particles", atlases.particles);
  emitter << YAML::EndMap;
}

ConfigResult write_emitted_config_file(
    const std::filesystem::path& path,
    const YAML::Emitter& emitter,
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

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return ConfigResult::failure(
        ConfigError{"failed to open config file for write: " + path.string()});
  }
  output << emitter.c_str();
  output << '\n';
  output.flush();
  if (!output) {
    return ConfigResult::failure(
        ConfigError{"failed to write config file: " + path.string()});
  }

  return ConfigResult::success(config);
}

ConfigResult save_default_shared_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config) {
  YAML::Emitter emitter;
  emitter << YAML::BeginMap;
  emitter << YAML::Key << "paths" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "arvane_home" << YAML::Value
          << config_path_string(config.paths.project_root, config.paths.arvane_home_path());
  emitter << YAML::EndMap;

  emitter << YAML::Key << "simulation" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "base_tps" << YAML::Value << config.simulation.base_tps;
  emitter << YAML::Key << "systems" << YAML::Value << YAML::BeginMap;
  auto emit_system = [&emitter](
                         std::string_view name,
                         const SimulationConfig::TickSystemConfig& system) {
    emitter << YAML::Key << std::string{name} << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "interval" << YAML::Value << system.interval;
    if (system.budget != 0U) {
      emitter << YAML::Key << "budget" << YAML::Value << system.budget;
    }
    if (system.samples_per_chunk != 0U) {
      emitter << YAML::Key << "samples_per_chunk" << YAML::Value << system.samples_per_chunk;
    }
    emitter << YAML::EndMap;
  };
  emit_system("physics", config.simulation.systems.physics);
  emit_system("entities", config.simulation.systems.entities);
  emit_system("ai", config.simulation.systems.ai);
  emit_system("block_updates", config.simulation.systems.block_updates);
  emit_system("fluids", config.simulation.systems.fluids);
  emit_system("random_ticks", config.simulation.systems.random_ticks);
  emitter << YAML::EndMap;
  emitter << YAML::Key << "fluids" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "water" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "flow_interval_ticks" << YAML::Value
          << config.simulation.fluids.water.flow_interval_ticks;
  emitter << YAML::EndMap;
  emitter << YAML::Key << "lava" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "flow_interval_ticks" << YAML::Value
          << config.simulation.fluids.lava.flow_interval_ticks;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "chunks" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "load_distance" << YAML::Value << config.chunks.load_distance;
  emitter << YAML::Key << "simulation_distance" << YAML::Value
          << config.chunks.simulation_distance;
  emitter << YAML::Key << "unload_margin" << YAML::Value << config.chunks.unload_margin;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "jobs" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "worker_threads" << YAML::Value << config.jobs.worker_threads;
  emitter << YAML::Key << "reserved_threads" << YAML::Value << config.jobs.reserved_threads;
  emitter << YAML::Key << "min_worker_threads" << YAML::Value << config.jobs.min_worker_threads;
  emitter << YAML::Key << "max_worker_threads" << YAML::Value << config.jobs.max_worker_threads;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "physics" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "standing_eye_height" << YAML::Value
          << config.physics.standing_eye_height;
  emitter << YAML::Key << "sneaking_eye_height" << YAML::Value
          << config.physics.sneaking_eye_height;
  emitter << YAML::Key << "swimming_eye_height" << YAML::Value
          << config.physics.swimming_eye_height;
  emitter << YAML::Key << "eye_height_lerp_per_second" << YAML::Value
          << config.physics.eye_height_lerp_per_second;
  emitter << YAML::Key << "reach_distance" << YAML::Value << config.physics.reach_distance;
  emitter << YAML::Key << "mouse_sensitivity_radians" << YAML::Value
          << config.physics.mouse_sensitivity_radians;
  emitter << YAML::Key << "walk_speed" << YAML::Value << config.physics.walk_speed;
  emitter << YAML::Key << "sprint_speed" << YAML::Value << config.physics.sprint_speed;
  emitter << YAML::Key << "sneak_speed" << YAML::Value << config.physics.sneak_speed;
  emitter << YAML::Key << "air_speed" << YAML::Value << config.physics.air_speed;
  emitter << YAML::Key << "fly_speed" << YAML::Value << config.physics.fly_speed;
  emitter << YAML::Key << "fly_vertical_speed" << YAML::Value
          << config.physics.fly_vertical_speed;
  emitter << YAML::Key << "jump_velocity" << YAML::Value << config.physics.jump_velocity;
  emitter << YAML::Key << "gravity_per_second" << YAML::Value
          << config.physics.gravity_per_second;
  emitter << YAML::Key << "terminal_velocity" << YAML::Value
          << config.physics.terminal_velocity;
  emitter << YAML::Key << "body_width" << YAML::Value << config.physics.body_width;
  emitter << YAML::Key << "body_height" << YAML::Value << config.physics.body_height;
  emitter << YAML::Key << "sneaking_body_height" << YAML::Value
          << config.physics.sneaking_body_height;
  emitter << YAML::Key << "swimming_body_width" << YAML::Value
          << config.physics.swimming_body_width;
  emitter << YAML::Key << "swimming_body_height" << YAML::Value
          << config.physics.swimming_body_height;
  emitter << YAML::Key << "sneak_support_probe_depth" << YAML::Value
          << config.physics.sneak_support_probe_depth;
  emitter << YAML::Key << "collision_epsilon" << YAML::Value
          << config.physics.collision_epsilon;
  emitter << YAML::Key << "surface_swim_speed" << YAML::Value
          << config.physics.surface_swim_speed;
  emitter << YAML::Key << "underwater_swim_speed" << YAML::Value
          << config.physics.underwater_swim_speed;
  emitter << YAML::Key << "fast_surface_swim_speed" << YAML::Value
          << config.physics.fast_surface_swim_speed;
  emitter << YAML::Key << "fast_underwater_swim_speed" << YAML::Value
          << config.physics.fast_underwater_swim_speed;
  emitter << YAML::Key << "upward_swim_acceleration" << YAML::Value
          << config.physics.upward_swim_acceleration;
  emitter << YAML::Key << "downward_swim_acceleration" << YAML::Value
          << config.physics.downward_swim_acceleration;
  emitter << YAML::Key << "liquid_drag" << YAML::Value << config.physics.liquid_drag;
  emitter << YAML::Key << "surface_float_depth" << YAML::Value
          << config.physics.surface_float_depth;
  emitter << YAML::Key << "surface_buoyancy" << YAML::Value
          << config.physics.surface_buoyancy;
  emitter << YAML::Key << "underwater_buoyancy" << YAML::Value
          << config.physics.underwater_buoyancy;
  emitter << YAML::Key << "breath_seconds" << YAML::Value << config.physics.breath_seconds;
  emitter << YAML::Key << "max_head_body_yaw_radians" << YAML::Value
          << config.physics.max_head_body_yaw_radians;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "gameplay" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "block_breaking" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "base_seconds_per_hardness" << YAML::Value
          << config.gameplay.block_breaking.base_seconds_per_hardness;
  emitter << YAML::Key << "creative_inventory_multiplier" << YAML::Value
          << config.gameplay.block_breaking.creative_inventory_multiplier;
  emitter << YAML::Key << "hand_speed_multiplier" << YAML::Value
          << config.gameplay.block_breaking.hand_speed_multiplier;
  emitter << YAML::Key << "wrong_tool_speed_multiplier" << YAML::Value
          << config.gameplay.block_breaking.wrong_tool_speed_multiplier;
  emitter << YAML::EndMap;
  emitter << YAML::Key << "dropped_items" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "pickup_radius" << YAML::Value
          << config.gameplay.dropped_items.pickup_radius;
  emitter << YAML::Key << "pickup_delay_seconds" << YAML::Value
          << config.gameplay.dropped_items.pickup_delay_seconds;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "chunk_pipeline" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "max_generation_jobs_per_frame" << YAML::Value
          << config.chunk_pipeline.max_generation_jobs_per_frame;
  emitter << YAML::Key << "max_generation_results_per_tick" << YAML::Value
          << config.chunk_pipeline.max_generation_results_per_tick;
  emitter << YAML::Key << "max_lighting_results_per_tick" << YAML::Value
          << config.chunk_pipeline.max_lighting_results_per_tick;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;

  return write_emitted_config_file(path, emitter, config);
}

ConfigResult save_default_client_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config) {
  YAML::Emitter emitter;
  emitter << YAML::BeginMap;
  emitter << YAML::Key << "paths" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "input_bindings_file" << YAML::Value
          << config_path_string(config.paths.project_root, config.paths.input_bindings_file);
  emitter << YAML::Key << "server_list_file" << YAML::Value
          << config_path_string(config.paths.project_root, config.paths.server_list_file);
  emitter << YAML::EndMap;

  emitter << YAML::Key << "window" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "width" << YAML::Value << config.window.width;
  emitter << YAML::Key << "height" << YAML::Value << config.window.height;
  emitter << YAML::Key << "title" << YAML::Value << config.window.title;
  emitter << YAML::Key << "vsync" << YAML::Value << config.window.vsync;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "chunks" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "render_distance" << YAML::Value << config.chunks.render_distance;
  emitter << YAML::Key << "stream_distance" << YAML::Value << config.chunks.stream_distance;
  emitter << YAML::EndMap;

  emitter << YAML::Key << "render" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "max_chunk_mesh_uploads_per_frame" << YAML::Value
          << config.render.max_chunk_mesh_uploads_per_frame;
  emitter << YAML::Key << "frame_pacing_enabled" << YAML::Value
          << config.render.frame_pacing_enabled;
  emitter << YAML::Key << "frame_pacing_target_fps" << YAML::Value
          << config.render.frame_pacing_target_fps;
  emitter << YAML::Key << "frame_pacing_spin_us" << YAML::Value
          << config.render.frame_pacing_spin_us;
  emitter << YAML::Key << "block_outline" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "color" << YAML::Value << YAML::Flow
          << YAML::BeginSeq
          << config.render.block_outline.color_r
          << config.render.block_outline.color_g
          << config.render.block_outline.color_b
          << YAML::EndSeq;
  emitter << YAML::Key << "thickness" << YAML::Value
          << config.render.block_outline.thickness;
  emitter << YAML::EndMap;
  emit_render_atlases_config(emitter, config.render.atlases);
  emitter << YAML::EndMap;

  emitter << YAML::Key << "chunk_pipeline" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "max_client_chunk_applies_per_frame" << YAML::Value
          << config.chunk_pipeline.max_client_chunk_applies_per_frame;
  emitter << YAML::Key << "max_client_chunk_unloads_per_frame" << YAML::Value
          << config.chunk_pipeline.max_client_chunk_unloads_per_frame;
  emitter << YAML::Key << "max_mesh_jobs_per_frame" << YAML::Value
          << config.chunk_pipeline.max_mesh_jobs_per_frame;
  emitter << YAML::Key << "frame_spike_log_ms" << YAML::Value
          << config.chunk_pipeline.frame_spike_log_ms;
  emitter << YAML::Key << "profile_log_interval_ms" << YAML::Value
          << config.chunk_pipeline.profile_log_interval_ms;
  emitter << YAML::EndMap;
  emitter << YAML::EndMap;

  return write_emitted_config_file(path, emitter, config);
}

ConfigResult save_default_server_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config) {
  YAML::Emitter emitter;
  emitter << YAML::BeginMap;
  emitter << YAML::Key << "world" << YAML::Value << config.server.world;
  emitter << YAML::Key << "network" << YAML::Value << YAML::BeginMap;
  emitter << YAML::Key << "bind_address" << YAML::Value << config.network.bind_address;
  emitter << YAML::Key << "port" << YAML::Value << config.network.port;
  emitter << YAML::EndMap;
  emit_skins_config(emitter, config.skins);
  emitter << YAML::EndMap;

  return write_emitted_config_file(path, emitter, config);
}

void read_config_node(const YAML::Node& root, EngineConfig& config) {
  read_scalar(root, "world", config.server.world);

  const YAML::Node paths = root["paths"];
  if (paths) {
    read_path_scalar(paths, "arvane_home", config.paths.arvane_home_root);
    read_path_scalar(paths, "config_file", config.paths.config_file);
    read_path_scalar(paths, "input_bindings_file", config.paths.input_bindings_file);
    read_path_scalar(paths, "server_list_file", config.paths.server_list_file);
  }

  const YAML::Node window = root["window"];
  if (window) {
    read_scalar(window, "width", config.window.width);
    read_scalar(window, "height", config.window.height);
    read_scalar(window, "title", config.window.title);
    read_scalar(window, "vsync", config.window.vsync);
  }

  const YAML::Node simulation = root["simulation"];
  if (simulation) {
    read_scalar(simulation, "base_tps", config.simulation.base_tps);
    const YAML::Node systems = simulation["systems"];
    if (systems) {
      read_tick_system_config(systems, "physics", config.simulation.systems.physics);
      read_tick_system_config(systems, "entities", config.simulation.systems.entities);
      read_tick_system_config(systems, "ai", config.simulation.systems.ai);
      read_tick_system_config(systems, "block_updates", config.simulation.systems.block_updates);
      read_tick_system_config(systems, "fluids", config.simulation.systems.fluids);
      read_tick_system_config(systems, "random_ticks", config.simulation.systems.random_ticks);
    }
    const YAML::Node fluids = simulation["fluids"];
    if (fluids) {
      read_fluid_speed_config(fluids, "water", config.simulation.fluids.water);
      read_fluid_speed_config(fluids, "lava", config.simulation.fluids.lava);
    }
  }

  const YAML::Node network = root["network"];
  if (network) {
    read_scalar(network, "bind_address", config.network.bind_address);
    read_scalar(network, "port", config.network.port);
  }

  const YAML::Node skins = root["skins"];
  if (skins) {
    read_skins_config(skins, config.skins);
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

  const YAML::Node physics = root["physics"];
  if (physics) {
    read_physics_config(physics, config.physics);
  }

  const YAML::Node gameplay = root["gameplay"];
  if (gameplay) {
    read_gameplay_config(gameplay, config.gameplay);
  }

  const YAML::Node render = root["render"];
  if (render) {
    read_scalar(
        render,
        "max_chunk_mesh_uploads_per_frame",
        config.render.max_chunk_mesh_uploads_per_frame);
    read_scalar(render, "frame_pacing_enabled", config.render.frame_pacing_enabled);
    read_scalar(render, "frame_pacing_target_fps", config.render.frame_pacing_target_fps);
    read_scalar(render, "frame_pacing_spin_us", config.render.frame_pacing_spin_us);
    read_scalar(render, "show_local_player_body", config.render.show_local_player_body);
    read_block_outline_config(render, config.render.block_outline);
    read_render_atlases_config(render, config.render.atlases);
  }

  const YAML::Node chunk_pipeline = root["chunk_pipeline"];
  if (chunk_pipeline) {
    read_scalar(
        chunk_pipeline,
        "max_generation_jobs_per_frame",
        config.chunk_pipeline.max_generation_jobs_per_frame);
    read_scalar(
        chunk_pipeline,
        "max_generation_results_per_tick",
        config.chunk_pipeline.max_generation_results_per_tick);
    read_scalar(
        chunk_pipeline,
        "max_lighting_results_per_tick",
        config.chunk_pipeline.max_lighting_results_per_tick);
    read_scalar(
        chunk_pipeline,
        "max_client_chunk_applies_per_frame",
        config.chunk_pipeline.max_client_chunk_applies_per_frame);
    read_scalar(
        chunk_pipeline,
        "max_client_chunk_unloads_per_frame",
        config.chunk_pipeline.max_client_chunk_unloads_per_frame);
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
}

std::optional<ConfigError> validate_config(const EngineConfig& config) {
  if (config.server.world.empty() || config.server.world == "." ||
      config.server.world == ".." ||
      config.server.world.find('/') != std::string::npos ||
      config.server.world.find('\\') != std::string::npos) {
    return ConfigError{"world must be a non-empty world name, not a path"};
  }

  if (config.simulation.base_tps != 20 && config.simulation.base_tps != 30 &&
      config.simulation.base_tps != 60) {
    return ConfigError{"simulation.base_tps must be one of 20, 30, or 60"};
  }
  auto validate_interval = [](u32 interval, std::string_view name) -> std::optional<ConfigError> {
    if (interval == 0U) {
      return ConfigError{std::string{name} + ".interval must be non-zero"};
    }
    return std::nullopt;
  };
  for (const auto [name, interval] : {
           std::pair{std::string_view{"simulation.systems.physics"}, config.simulation.systems.physics.interval},
           std::pair{std::string_view{"simulation.systems.entities"}, config.simulation.systems.entities.interval},
           std::pair{std::string_view{"simulation.systems.ai"}, config.simulation.systems.ai.interval},
           std::pair{std::string_view{"simulation.systems.block_updates"}, config.simulation.systems.block_updates.interval},
           std::pair{std::string_view{"simulation.systems.fluids"}, config.simulation.systems.fluids.interval},
           std::pair{std::string_view{"simulation.systems.random_ticks"}, config.simulation.systems.random_ticks.interval},
       }) {
    if (std::optional<ConfigError> error = validate_interval(interval, name);
        error.has_value()) {
      return error;
    }
  }
  if (config.simulation.systems.block_updates.budget == 0U ||
      config.simulation.systems.fluids.budget == 0U) {
    return ConfigError{"simulation system budgets for block_updates and fluids must be non-zero"};
  }
  if (config.simulation.systems.random_ticks.samples_per_chunk == 0U) {
    return ConfigError{"simulation.systems.random_ticks.samples_per_chunk must be non-zero"};
  }
  if (config.simulation.fluids.water.flow_interval_ticks == 0U ||
      config.simulation.fluids.lava.flow_interval_ticks == 0U) {
    return ConfigError{"simulation fluid flow_interval_ticks values must be non-zero"};
  }

  if (config.window.width == 0 || config.window.height == 0) {
    return ConfigError{"window dimensions must be non-zero"};
  }

  if (config.skins.uploads.max_chunk_bytes == 0U ||
      config.skins.uploads.chunk_threshold_bytes == 0U ||
      config.skins.uploads.max_active_per_connection == 0U ||
      config.skins.uploads.max_active_global == 0U ||
      config.skins.uploads.timeout_seconds == 0U ||
      config.skins.uploads.idle_timeout_seconds == 0U ||
      config.skins.uploads.max_begin_attempts_per_minute == 0U ||
      config.skins.uploads.max_completed_uploads_per_minute == 0U ||
      config.skins.uploads.max_completed_uploads_per_hour == 0U ||
      config.skins.uploads.max_upload_bytes_per_hour == 0U ||
      config.skins.uploads.max_failed_decodes_per_minute == 0U) {
    return ConfigError{"skins.uploads limits must be non-zero"};
  }
  if (config.skins.uploads.idle_timeout_seconds > config.skins.uploads.timeout_seconds) {
    return ConfigError{"skins.uploads.idle_timeout_seconds must be <= timeout_seconds"};
  }
  if (config.skins.downloads.max_requests_per_second == 0U ||
      config.skins.downloads.max_requests_per_minute == 0U ||
      config.skins.downloads.max_bytes_per_minute == 0U ||
      config.skins.downloads.max_in_flight_per_connection == 0U ||
      config.skins.downloads.max_batch_size == 0U) {
    return ConfigError{"skins.downloads limits must be non-zero"};
  }
  if (config.skins.storage.max_total_skin_bytes == 0U ||
      config.skins.storage.max_skins_per_profile == 0U) {
    return ConfigError{"skins.storage limits must be non-zero"};
  }
  if (config.skins.resolutions.empty()) {
    return ConfigError{"skins.resolutions must list at least one resolution"};
  }
  std::vector<u16> seen_skin_sizes;
  seen_skin_sizes.reserve(config.skins.resolutions.size());
  for (const SkinResolutionConfig& resolution : config.skins.resolutions) {
    if (!is_supported_skin_resolution(resolution.size)) {
      return ConfigError{"skins.resolutions.size must be one of 64, 128, 256, 512, 1024, or 2048"};
    }
    if (std::find(seen_skin_sizes.begin(), seen_skin_sizes.end(), resolution.size) !=
        seen_skin_sizes.end()) {
      return ConfigError{"skins.resolutions may not contain duplicate sizes"};
    }
    seen_skin_sizes.push_back(resolution.size);
    if (resolution.max_upload_bytes == 0U) {
      return ConfigError{"skins.resolutions.max_upload_bytes must be non-zero"};
    }
    if (resolution.max_decoded_bytes == 0U) {
      return ConfigError{"skins.resolutions.max_decoded_bytes must be non-zero"};
    }
  }

  if (config.chunks.load_distance < 0 || config.chunks.simulation_distance < 0 ||
      config.chunks.render_distance < 0 || config.chunks.stream_distance < 0) {
    return ConfigError{"chunk distances must be non-negative"};
  }
  if (config.chunks.unload_margin < 0) {
    return ConfigError{"chunks.unload_margin must be non-negative"};
  }
  if (config.chunks.simulation_distance > config.chunks.load_distance) {
    return ConfigError{"chunks.simulation_distance must be <= chunks.load_distance"};
  }
  if (config.chunks.render_distance > config.chunks.load_distance) {
    return ConfigError{"chunks.render_distance must be <= chunks.load_distance"};
  }
  if (config.chunks.stream_distance > config.chunks.load_distance) {
    return ConfigError{"chunks.stream_distance must be <= chunks.load_distance"};
  }
  if (config.jobs.min_worker_threads == 0U) {
    return ConfigError{"jobs.min_worker_threads must be non-zero"};
  }
  if (config.jobs.max_worker_threads < config.jobs.min_worker_threads) {
    return ConfigError{"jobs.max_worker_threads must be >= jobs.min_worker_threads"};
  }
  auto validate_positive_f32 = [](f32 value, std::string_view name) -> std::optional<ConfigError> {
    if (!std::isfinite(value) || value <= 0.0F) {
      return ConfigError{std::string{name} + " must be finite and > 0"};
    }
    return std::nullopt;
  };
  for (const auto [name, value] : {
           std::pair{std::string_view{"physics.standing_eye_height"}, config.physics.standing_eye_height},
           std::pair{std::string_view{"physics.sneaking_eye_height"}, config.physics.sneaking_eye_height},
           std::pair{std::string_view{"physics.swimming_eye_height"}, config.physics.swimming_eye_height},
           std::pair{std::string_view{"physics.eye_height_lerp_per_second"}, config.physics.eye_height_lerp_per_second},
           std::pair{std::string_view{"physics.reach_distance"}, config.physics.reach_distance},
           std::pair{std::string_view{"physics.mouse_sensitivity_radians"}, config.physics.mouse_sensitivity_radians},
           std::pair{std::string_view{"physics.walk_speed"}, config.physics.walk_speed},
           std::pair{std::string_view{"physics.sprint_speed"}, config.physics.sprint_speed},
           std::pair{std::string_view{"physics.sneak_speed"}, config.physics.sneak_speed},
           std::pair{std::string_view{"physics.air_speed"}, config.physics.air_speed},
           std::pair{std::string_view{"physics.fly_speed"}, config.physics.fly_speed},
           std::pair{std::string_view{"physics.fly_vertical_speed"}, config.physics.fly_vertical_speed},
           std::pair{std::string_view{"physics.jump_velocity"}, config.physics.jump_velocity},
           std::pair{std::string_view{"physics.gravity_per_second"}, config.physics.gravity_per_second},
           std::pair{std::string_view{"physics.terminal_velocity"}, config.physics.terminal_velocity},
           std::pair{std::string_view{"physics.body_width"}, config.physics.body_width},
           std::pair{std::string_view{"physics.body_height"}, config.physics.body_height},
           std::pair{std::string_view{"physics.sneaking_body_height"}, config.physics.sneaking_body_height},
           std::pair{std::string_view{"physics.swimming_body_width"}, config.physics.swimming_body_width},
           std::pair{std::string_view{"physics.swimming_body_height"}, config.physics.swimming_body_height},
           std::pair{std::string_view{"physics.sneak_support_probe_depth"}, config.physics.sneak_support_probe_depth},
           std::pair{std::string_view{"physics.collision_epsilon"}, config.physics.collision_epsilon},
           std::pair{std::string_view{"physics.surface_swim_speed"}, config.physics.surface_swim_speed},
           std::pair{std::string_view{"physics.underwater_swim_speed"}, config.physics.underwater_swim_speed},
           std::pair{std::string_view{"physics.fast_surface_swim_speed"}, config.physics.fast_surface_swim_speed},
           std::pair{std::string_view{"physics.fast_underwater_swim_speed"}, config.physics.fast_underwater_swim_speed},
           std::pair{std::string_view{"physics.upward_swim_acceleration"}, config.physics.upward_swim_acceleration},
           std::pair{std::string_view{"physics.downward_swim_acceleration"}, config.physics.downward_swim_acceleration},
           std::pair{std::string_view{"physics.surface_float_depth"}, config.physics.surface_float_depth},
           std::pair{std::string_view{"physics.surface_buoyancy"}, config.physics.surface_buoyancy},
           std::pair{std::string_view{"physics.breath_seconds"}, config.physics.breath_seconds},
           std::pair{std::string_view{"physics.max_head_body_yaw_radians"}, config.physics.max_head_body_yaw_radians},
       }) {
    if (std::optional<ConfigError> error = validate_positive_f32(value, name);
        error.has_value()) {
      return error;
    }
  }
  if (!std::isfinite(config.physics.underwater_buoyancy)) {
    return ConfigError{"physics.underwater_buoyancy must be finite"};
  }
  if (!std::isfinite(config.physics.liquid_drag) ||
      config.physics.liquid_drag < 0.0F ||
      config.physics.liquid_drag > 1.0F) {
    return ConfigError{"physics.liquid_drag must be finite and between 0 and 1"};
  }
  if (config.physics.swimming_body_height > config.physics.body_height ||
      config.physics.sneaking_body_height > config.physics.body_height) {
    return ConfigError{"physics swimming/sneaking body heights must be <= body_height"};
  }
  if (!std::isfinite(config.gameplay.block_breaking.base_seconds_per_hardness) ||
      config.gameplay.block_breaking.base_seconds_per_hardness < 0.0F) {
    return ConfigError{"gameplay.block_breaking.base_seconds_per_hardness must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.block_breaking.creative_inventory_multiplier) ||
      config.gameplay.block_breaking.creative_inventory_multiplier < 0.0F) {
    return ConfigError{"gameplay.block_breaking.creative_inventory_multiplier must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.block_breaking.hand_speed_multiplier) ||
      config.gameplay.block_breaking.hand_speed_multiplier < 0.0F) {
    return ConfigError{"gameplay.block_breaking.hand_speed_multiplier must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.block_breaking.wrong_tool_speed_multiplier) ||
      config.gameplay.block_breaking.wrong_tool_speed_multiplier < 0.0F) {
    return ConfigError{"gameplay.block_breaking.wrong_tool_speed_multiplier must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.dropped_items.pickup_radius) ||
      config.gameplay.dropped_items.pickup_radius < 0.0F) {
    return ConfigError{"gameplay.dropped_items.pickup_radius must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.dropped_items.pickup_delay_seconds) ||
      config.gameplay.dropped_items.pickup_delay_seconds < 0.0F) {
    return ConfigError{"gameplay.dropped_items.pickup_delay_seconds must be finite and >= 0"};
  }
  if (config.render.max_chunk_mesh_uploads_per_frame == 0U) {
    return ConfigError{"render.max_chunk_mesh_uploads_per_frame must be non-zero"};
  }
  if (config.render.frame_pacing_enabled &&
      config.render.frame_pacing_target_fps == 0U) {
    return ConfigError{"render.frame_pacing_target_fps must be non-zero when frame pacing is enabled"};
  }
  if (!std::isfinite(config.render.block_outline.color_r) ||
      !std::isfinite(config.render.block_outline.color_g) ||
      !std::isfinite(config.render.block_outline.color_b) ||
      config.render.block_outline.color_r < 0.0F ||
      config.render.block_outline.color_g < 0.0F ||
      config.render.block_outline.color_b < 0.0F ||
      config.render.block_outline.color_r > 1.0F ||
      config.render.block_outline.color_g > 1.0F ||
      config.render.block_outline.color_b > 1.0F) {
    return ConfigError{"render.block_outline.color must contain finite values between 0 and 1"};
  }
  if (!std::isfinite(config.render.block_outline.thickness) ||
      config.render.block_outline.thickness <= 0.0F) {
    return ConfigError{"render.block_outline.thickness must be finite and > 0"};
  }
  if (config.render.atlases.player_skins.page_size == 0U ||
      config.render.atlases.player_skins.max_pages == 0U) {
    return ConfigError{"render.atlases.player_skins page_size and max_pages must be non-zero"};
  }
  if (config.render.atlases.player_skins.cleanup.interval_seconds == 0U ||
      config.render.atlases.player_skins.cleanup.evict_after_unused_seconds == 0U) {
    return ConfigError{"render.atlases.player_skins cleanup intervals must be non-zero"};
  }
  if (!std::isfinite(config.render.atlases.player_skins.pressure_cleanup.usage_threshold) ||
      !std::isfinite(config.render.atlases.player_skins.pressure_cleanup.target_usage) ||
      config.render.atlases.player_skins.pressure_cleanup.usage_threshold <= 0.0F ||
      config.render.atlases.player_skins.pressure_cleanup.usage_threshold > 1.0F ||
      config.render.atlases.player_skins.pressure_cleanup.target_usage <= 0.0F ||
      config.render.atlases.player_skins.pressure_cleanup.target_usage > 1.0F ||
      config.render.atlases.player_skins.pressure_cleanup.target_usage >
          config.render.atlases.player_skins.pressure_cleanup.usage_threshold) {
    return ConfigError{
        "render.atlases.player_skins pressure thresholds must be finite in (0, 1] and target <= usage"};
  }
  if (config.chunk_pipeline.max_generation_jobs_per_frame == 0U ||
      config.chunk_pipeline.max_generation_results_per_tick == 0U ||
      config.chunk_pipeline.max_lighting_results_per_tick == 0U ||
      config.chunk_pipeline.max_client_chunk_applies_per_frame == 0U ||
      config.chunk_pipeline.max_client_chunk_unloads_per_frame == 0U ||
      config.chunk_pipeline.max_mesh_jobs_per_frame == 0U) {
    return ConfigError{"chunk_pipeline job budgets must be non-zero"};
  }

  return std::nullopt;
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
                 ? default_profile_config_path_from(config.paths.config_path(), options.profile)
                 : config.paths.config_file);
  config.paths.config_file = resolve_project_relative(config.paths.project_root, config_file);

  const std::filesystem::path shared_config_file =
      config.paths.shared_config_file.empty()
          ? default_shared_config_path_from(config.paths.config_path())
          : config.paths.shared_config_file;
  config.paths.shared_config_file =
      resolve_project_relative(config.paths.project_root, shared_config_file);

  const std::filesystem::path client_config_file =
      config.paths.client_config_file.empty()
          ? default_client_config_path_from(config.paths.config_path())
          : config.paths.client_config_file;
  config.paths.client_config_file =
      resolve_project_relative(config.paths.project_root, client_config_file);

  const std::filesystem::path server_config_file =
      config.paths.server_config_file.empty()
          ? default_server_config_path_from(config.paths.config_path())
          : config.paths.server_config_file;
  config.paths.server_config_file =
      resolve_project_relative(config.paths.project_root, server_config_file);

  const std::filesystem::path input_bindings_file =
      config.paths.input_bindings_file.empty()
          ? default_input_bindings_path_from(config.paths.config_path())
          : config.paths.input_bindings_file;
  config.paths.input_bindings_file =
      resolve_project_relative(config.paths.project_root, input_bindings_file);

  const std::filesystem::path server_list_file =
      config.paths.server_list_file.empty()
          ? default_server_list_path_from(config.paths.config_path())
          : config.paths.server_list_file;
  config.paths.server_list_file =
      resolve_project_relative(config.paths.project_root, server_list_file);
}

ConfigResult parse_config_yaml(std::string_view text) {
  EngineConfig config = default_config();

  try {
    const YAML::Node root = YAML::Load(std::string{text});
    if (!root || root.IsNull()) {
      return ConfigResult::success(std::move(config));
    }

    read_config_node(root, config);
  } catch (const YAML::Exception& ex) {
    return ConfigResult::failure(ConfigError{ex.what()});
  }

  if (std::optional<ConfigError> error = validate_config(config); error.has_value()) {
    return ConfigResult::failure(std::move(error.value()));
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

ConfigResult apply_config_overlay_file(
    const std::filesystem::path& path,
    EngineConfig& config) {
  std::ifstream input(path);
  if (!input) {
    return ConfigResult::failure(ConfigError{"failed to open config file: " + path.string()});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  try {
    const YAML::Node root = YAML::Load(buffer.str());
    if (root && !root.IsNull()) {
      read_config_node(root, config);
    }
  } catch (const YAML::Exception& ex) {
    return ConfigResult::failure(ConfigError{ex.what()});
  }

  return ConfigResult::success(config);
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
        default_config_path_from(config.paths.config_path());
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
        auto saved_shared = save_default_shared_config_file(config.paths.shared_config_file, config);
        if (!saved_shared) {
          core::warn("config", saved_shared.error().message);
        }
      }
      if (!client_exists) {
        auto saved_client = save_default_client_config_file(config.paths.client_config_file, config);
        if (!saved_client) {
          core::warn("config", saved_client.error().message);
        }
      }
      if (!server_exists) {
        auto saved_server = save_default_server_config_file(config.paths.server_config_file, config);
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
    if (std::optional<ConfigError> error = validate_config(config); error.has_value()) {
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
      return ConfigResult::failure(ConfigError{"failed to open config file: " + config.paths.config_file.string()});
    }
    auto saved = save_default_config_file(config.paths.config_file, config);
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

ServerListResult load_server_list_file(const std::filesystem::path& path) {
  std::error_code file_error;
  if (!std::filesystem::exists(path, file_error)) {
    if (file_error) {
      return ServerListResult::failure(
          ConfigError{"failed to inspect server list file: " + file_error.message()});
    }
    return ServerListResult::success({});
  }

  std::ifstream input(path);
  if (!input) {
    return ServerListResult::failure(
        ConfigError{"failed to open server list file: " + path.string()});
  }

  try {
    const nlohmann::json root = nlohmann::json::parse(input);
    const nlohmann::json* servers = &root;
    if (root.is_object()) {
      const auto found = root.find("servers");
      if (found == root.end()) {
        return ServerListResult::success({});
      }
      servers = &*found;
    }
    if (!servers->is_array()) {
      return ServerListResult::failure(
          ConfigError{"server list must be an array or object with a servers array"});
    }

    std::vector<SavedServerListEntry> entries;
    entries.reserve(servers->size());
    for (const nlohmann::json& entry : *servers) {
      if (!entry.is_object() ||
          !entry.contains("name") ||
          !entry.contains("address") ||
          !entry.at("name").is_string() ||
          !entry.at("address").is_string()) {
        return ServerListResult::failure(
            ConfigError{"server list entries must contain string name and address fields"});
      }

      entries.push_back({
          entry.at("name").get<std::string>(),
          entry.at("address").get<std::string>()});
    }
    return ServerListResult::success(std::move(entries));
  } catch (const nlohmann::json::exception& ex) {
    return ServerListResult::failure(
        ConfigError{"failed to parse server list file: " + std::string{ex.what()}});
  }
}

ServerListSaveResult save_server_list_file(
    const std::filesystem::path& path,
    std::span<const SavedServerListEntry> servers) {
  std::error_code error;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return ServerListSaveResult::failure(
          ConfigError{"failed to create server list directory: " + error.message()});
    }
  }

  nlohmann::json server_array = nlohmann::json::array();
  for (const SavedServerListEntry& server : servers) {
    server_array.push_back({{"name", server.name}, {"address", server.address}});
  }
  const nlohmann::json root{{"version", 1}, {"servers", std::move(server_array)}};

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return ServerListSaveResult::failure(
        ConfigError{"failed to open server list file for write: " + path.string()});
  }
  output << root.dump(2) << '\n';
  output.flush();
  if (!output) {
    return ServerListSaveResult::failure(
        ConfigError{"failed to write server list file: " + path.string()});
  }
  return ServerListSaveResult::success();
}

} // namespace arv::io
