#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <string>
#include <string_view>

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

struct JobSystemConfig {
  // 0 means automatic through the engine-owned scheduling policy.
  u32 worker_threads{};
  u32 reserved_threads{4};
  u32 min_worker_threads{1};
  u32 max_worker_threads{64};
};

struct RenderConfig {
  u32 max_chunk_mesh_uploads_per_frame{2};
};

struct ChunkPipelineConfig {
  u32 max_generation_jobs_per_frame{4};
  u32 max_mesh_jobs_per_frame{4};
  u32 frame_spike_log_ms{33};
  u32 profile_log_interval_ms{1000};
};

struct PathConfig {
  std::filesystem::path project_root{"."};
  std::filesystem::path asset_root{"assets"};
  std::filesystem::path config_file{};
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
  std::filesystem::path explicit_asset_root{};
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
