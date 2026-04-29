#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace arv::io {

inline constexpr std::string_view DefaultArvaneHomeDirectoryName = "arvane_home";
inline constexpr std::string_view DefaultEngineConfigFileName = "engine.yaml";
inline constexpr std::string_view DefaultInputBindingsFileName = "input_bindings.yaml";
inline constexpr std::string_view DefaultServerSaveName = "server-save-1";

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

struct JobSystemConfig {
  // 0 means automatic through the engine-owned scheduling policy.
  u32 worker_threads{};
  u32 reserved_threads{4};
  u32 min_worker_threads{1};
  u32 max_worker_threads{64};
};

struct RenderConfig {
  u32 max_chunk_mesh_uploads_per_frame{8};
};

struct ChunkPipelineConfig {
  u32 max_generation_jobs_per_frame{4};
  u32 max_generation_results_per_tick{2};
  u32 max_lighting_results_per_tick{1};
  u32 max_client_chunk_applies_per_frame{2};
  u32 max_client_chunk_unloads_per_frame{32};
  u32 max_mesh_jobs_per_frame{4};
  u32 frame_spike_log_ms{33};
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
  RenderConfig render{};
  ChunkPipelineConfig chunk_pipeline{};
};

struct ConfigError {
  std::string message{};
};

using ConfigResult = Expected<EngineConfig, ConfigError>;
using TextFileResult = Expected<std::string, ConfigError>;

struct ConfigLoadOptions {
  std::filesystem::path explicit_config_path{};
  std::filesystem::path explicit_arvane_home_path{};
  std::filesystem::path project_root{};
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
