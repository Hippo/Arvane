#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace arv::io {

inline constexpr std::string_view DefaultArvaneHomeDirectoryName = "arvane_home";
inline constexpr std::string_view DefaultSharedConfigFileName = "arvane.yaml";
inline constexpr std::string_view DefaultClientConfigFileName = "client.yaml";
inline constexpr std::string_view DefaultServerConfigFileName = "server.yaml";
inline constexpr std::string_view DefaultEngineConfigFileName = "engine.yaml";
inline constexpr std::string_view DefaultInputBindingsFileName = "input_bindings.yaml";
inline constexpr std::string_view DefaultServerSaveName = "server-save-1";

struct WindowConfig {
  u32 width{1280};
  u32 height{720};
  std::string title{"Arvane"};
  bool vsync{true};
};

struct SimulationConfig {
  TickRate base_tps{DefaultBaseTps};

  struct TickSystemConfig {
    u32 interval{1};
    u32 budget{};
    u32 samples_per_chunk{};
  };

  struct TickSystemsConfig {
    TickSystemConfig physics{1, 0, 0};
    TickSystemConfig entities{1, 0, 0};
    TickSystemConfig ai{2, 0, 0};
    TickSystemConfig block_updates{3, 5000, 0};
    TickSystemConfig fluids{1, 2000, 0};
    TickSystemConfig random_ticks{20, 0, 3};
  };

  struct FluidSpeedConfig {
    u32 flow_interval_ticks{10};
  };

  struct FluidsConfig {
    FluidSpeedConfig water{10};
    FluidSpeedConfig lava{30};
  };

  TickSystemsConfig systems{};
  FluidsConfig fluids{};
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

struct JobSystemConfig {
  // 0 means automatic through the engine-owned scheduling policy.
  u32 worker_threads{};
  u32 reserved_threads{2};
  u32 min_worker_threads{1};
  u32 max_worker_threads{64};
};

struct PhysicsConfig {
  f32 standing_eye_height{1.62F};
  f32 sneaking_eye_height{1.35F};
  f32 swimming_eye_height{0.45F};
  f32 eye_height_lerp_per_second{12.0F};
  f32 reach_distance{5.0F};
  f32 mouse_sensitivity_radians{0.0025F};
  f32 walk_speed{4.5F};
  f32 sprint_speed{6.0F};
  f32 sneak_speed{2.25F};
  f32 air_speed{3.5F};
  f32 fly_speed{8.0F};
  f32 fly_vertical_speed{6.0F};
  f32 jump_velocity{8.5F};
  f32 gravity_per_second{32.0F};
  f32 terminal_velocity{55.0F};
  f32 body_width{0.6F};
  f32 body_height{1.8F};
  f32 sneaking_body_height{1.5F};
  f32 swimming_body_width{0.9F};
  f32 swimming_body_height{0.6F};
  f32 sneak_support_probe_depth{0.05F};
  f32 collision_epsilon{0.001F};
  f32 surface_swim_speed{2.5F};
  f32 underwater_swim_speed{2.0F};
  f32 fast_surface_swim_speed{5.6F};
  f32 fast_underwater_swim_speed{5.0F};
  f32 upward_swim_acceleration{18.0F};
  f32 downward_swim_acceleration{18.0F};
  f32 liquid_drag{0.8F};
  f32 surface_float_depth{1.0F};
  f32 surface_buoyancy{10.0F};
  f32 underwater_buoyancy{1.0F};
  f32 breath_seconds{15.0F};
};

struct RenderConfig {
  u32 max_chunk_mesh_uploads_per_frame{8};
  bool frame_pacing_enabled{};
  u32 frame_pacing_target_fps{60};
  u32 frame_pacing_spin_us{500};
};

struct ChunkPipelineConfig {
  u32 max_generation_jobs_per_frame{4};
  u32 max_generation_results_per_tick{2};
  u32 max_lighting_results_per_tick{1};
  u32 max_client_chunk_applies_per_frame{2};
  u32 max_client_chunk_unloads_per_frame{32};
  u32 max_mesh_jobs_per_frame{4};
  u32 frame_spike_log_ms{10};
  // 0 keeps recurring profiling logs off the render path; spike warnings remain enabled.
  u32 profile_log_interval_ms{};
};

struct PathConfig {
  std::filesystem::path project_root{"."};
  std::filesystem::path arvane_home_root{};
  std::filesystem::path asset_root{};
  std::filesystem::path plugins_root{};
  std::filesystem::path config_root{};
  std::filesystem::path saves_root{};
  std::filesystem::path logs_root{};
  std::filesystem::path cache_root{};
  std::filesystem::path config_file{};
  std::filesystem::path shared_config_file{};
  std::filesystem::path client_config_file{};
  std::filesystem::path server_config_file{};
  std::filesystem::path input_bindings_file{};

  [[nodiscard]] const std::filesystem::path& arvane_home_path() const noexcept {
    return arvane_home_root;
  }

  [[nodiscard]] const std::filesystem::path& assets_path() const noexcept {
    return asset_root;
  }

  [[nodiscard]] const std::filesystem::path& plugins_path() const noexcept {
    return plugins_root;
  }

  [[nodiscard]] const std::filesystem::path& config_path() const noexcept {
    return config_root;
  }

  [[nodiscard]] const std::filesystem::path& saves_path() const noexcept {
    return saves_root;
  }

  [[nodiscard]] const std::filesystem::path& logs_path() const noexcept {
    return logs_root;
  }

  [[nodiscard]] const std::filesystem::path& cache_path() const noexcept {
    return cache_root;
  }

  [[nodiscard]] std::filesystem::path server_save_path(std::string_view save_name) const {
    return saves_root / std::filesystem::path{save_name};
  }
};

struct EngineConfig {
  PathConfig paths{};
  WindowConfig window{};
  SimulationConfig simulation{};
  NetworkConfig network{};
  ChunkDistanceConfig chunks{};
  JobSystemConfig jobs{};
  PhysicsConfig physics{};
  RenderConfig render{};
  ChunkPipelineConfig chunk_pipeline{};
};

struct ConfigError {
  std::string message{};
};

using ConfigResult = Expected<EngineConfig, ConfigError>;
using TextFileResult = Expected<std::string, ConfigError>;

enum class ConfigProfile {
  Client,
  Server,
};

struct ConfigLoadOptions {
  std::filesystem::path explicit_config_path{};
  std::filesystem::path explicit_arvane_home_path{};
  std::filesystem::path project_root{};
  ConfigProfile profile{ConfigProfile::Client};
  bool missing_config_is_error{};
};

EngineConfig default_config();

void resolve_runtime_paths(const ConfigLoadOptions& options, EngineConfig& config);

ConfigResult parse_config_yaml(std::string_view text);

ConfigResult load_config_file(const std::filesystem::path& path);

ConfigResult load_config(const ConfigLoadOptions& options);

ConfigResult load_config_file_or_defaults(const std::filesystem::path& path);

TextFileResult read_text_file(const std::filesystem::path& path, std::string_view purpose);

} // namespace arv::io
