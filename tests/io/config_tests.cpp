#include "arv/io/config.hpp"
#include "arv/io/input_bindings.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

class ScopedCurrentPath {
public:
  explicit ScopedCurrentPath(const std::filesystem::path& next)
      : previous_(std::filesystem::current_path()) {
    std::filesystem::current_path(next);
  }

  ~ScopedCurrentPath() {
    std::error_code error;
    std::filesystem::current_path(previous_, error);
  }

private:
  std::filesystem::path previous_;
};

void write_file(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::trunc);
  output << text;
}

} // namespace

TEST(ConfigTests, ParsesYamlConfigWithDefaults) {
  const auto result = arv::io::parse_config_yaml(R"(
paths:
  arvane_home: custom_home
world: test-world
window:
  width: 1920
  title: Test Window
  vsync: false
simulation:
  base_tps: 30
  systems:
    physics:
      interval: 1
    entities:
      interval: 1
    ai:
      interval: 4
    block_updates:
      interval: 5
      budget: 1234
    fluids:
      interval: 2
      budget: 3456
    random_ticks:
      interval: 40
      samples_per_chunk: 9
  fluids:
    water:
      flow_interval_ticks: 11
    lava:
      flow_interval_ticks: 37
network:
  port: 28000
chunks:
  load_distance: 6
  simulation_distance: 2
  render_distance: 4
  stream_distance: 5
  unload_margin: 3
jobs:
  worker_threads: 6
  reserved_threads: 5
physics:
  standing_eye_height: 1.7
  sneaking_eye_height: 1.4
  swimming_eye_height: 0.5
  eye_height_lerp_per_second: 10.0
  reach_distance: 6.0
  mouse_sensitivity_radians: 0.003
  walk_speed: 4.0
  sprint_speed: 7.0
  sneak_speed: 2.0
  air_speed: 3.0
  fly_speed: 9.0
  fly_vertical_speed: 7.0
  jump_velocity: 9.0
  gravity_per_second: 30.0
  terminal_velocity: 50.0
  body_width: 0.65
  body_height: 1.9
  sneaking_body_height: 1.45
  swimming_body_width: 0.95
  swimming_body_height: 0.55
  sneak_support_probe_depth: 0.06
  collision_epsilon: 0.002
  surface_swim_speed: 2.75
  underwater_swim_speed: 2.25
  fast_surface_swim_speed: 5.75
  fast_underwater_swim_speed: 5.25
  upward_swim_acceleration: 19.0
  downward_swim_acceleration: 20.0
  liquid_drag: 0.75
  surface_float_depth: 0.9
  surface_buoyancy: 11.0
  underwater_buoyancy: 1.25
  breath_seconds: 18.0
  max_head_body_yaw_radians: 1.1
gameplay:
  block_breaking:
    base_seconds_per_hardness: 1.5
    creative_inventory_multiplier: 0.05
    hand_speed_multiplier: 0.25
    wrong_tool_speed_multiplier: 0.1
  dropped_items:
    pickup_radius: 1.75
    pickup_delay_seconds: 0.2
render:
  max_chunk_mesh_uploads_per_frame: 3
  frame_pacing_enabled: true
  frame_pacing_target_fps: 120
  frame_pacing_spin_us: 250
  show_local_player_body: false
  block_outline:
    color: [0.1, 0.2, 0.3]
    thickness: 0.75
  atlases:
    blocks:
      page_size: 1024
      max_pages: 2
    entities:
      page_size: 1536
      max_pages: 5
    ui:
      page_size: 512
      max_pages: 6
    fonts:
      page_size: 1024
      max_pages: 7
    player_skins:
      page_size: 1024
      max_pages: 3
      cleanup:
        interval_seconds: 30
        evict_after_unused_seconds: 120
      pressure_cleanup:
        enabled: false
        usage_threshold: 0.9
        target_usage: 0.5
    particles:
      page_size: 256
      max_pages: 2
chunk_pipeline:
  max_generation_jobs_per_frame: 2
  max_generation_results_per_tick: 3
  max_lighting_results_per_tick: 4
  max_client_chunk_applies_per_frame: 6
  max_client_chunk_unloads_per_frame: 7
  max_mesh_jobs_per_frame: 5
  frame_spike_log_ms: 25
  profile_log_interval_ms: 750
)");

  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().paths.arvane_home_path(), std::filesystem::path("custom_home"));
  EXPECT_EQ(result.value().server.world, "test-world");
  EXPECT_EQ(result.value().window.width, 1920U);
  EXPECT_EQ(result.value().window.height, 720U);
  EXPECT_EQ(result.value().window.title, "Test Window");
  EXPECT_FALSE(result.value().window.vsync);
  EXPECT_EQ(result.value().simulation.base_tps, 30U);
  EXPECT_EQ(result.value().simulation.systems.physics.interval, 1U);
  EXPECT_EQ(result.value().simulation.systems.entities.interval, 1U);
  EXPECT_EQ(result.value().simulation.systems.ai.interval, 4U);
  EXPECT_EQ(result.value().simulation.systems.block_updates.interval, 5U);
  EXPECT_EQ(result.value().simulation.systems.block_updates.budget, 1234U);
  EXPECT_EQ(result.value().simulation.systems.fluids.interval, 2U);
  EXPECT_EQ(result.value().simulation.systems.fluids.budget, 3456U);
  EXPECT_EQ(result.value().simulation.systems.random_ticks.interval, 40U);
  EXPECT_EQ(result.value().simulation.systems.random_ticks.samples_per_chunk, 9U);
  EXPECT_EQ(result.value().simulation.fluids.water.flow_interval_ticks, 11U);
  EXPECT_EQ(result.value().simulation.fluids.lava.flow_interval_ticks, 37U);
  EXPECT_EQ(result.value().network.port, 28000U);
  EXPECT_EQ(result.value().chunks.load_distance, 6);
  EXPECT_EQ(result.value().chunks.simulation_distance, 2);
  EXPECT_EQ(result.value().chunks.render_distance, 4);
  EXPECT_EQ(result.value().chunks.stream_distance, 5);
  EXPECT_EQ(result.value().chunks.unload_margin, 3);
  EXPECT_EQ(result.value().jobs.worker_threads, 6U);
  EXPECT_EQ(result.value().jobs.reserved_threads, 5U);
  EXPECT_FLOAT_EQ(result.value().physics.standing_eye_height, 1.7F);
  EXPECT_FLOAT_EQ(result.value().physics.sneaking_eye_height, 1.4F);
  EXPECT_FLOAT_EQ(result.value().physics.swimming_eye_height, 0.5F);
  EXPECT_FLOAT_EQ(result.value().physics.eye_height_lerp_per_second, 10.0F);
  EXPECT_FLOAT_EQ(result.value().physics.reach_distance, 6.0F);
  EXPECT_FLOAT_EQ(result.value().physics.mouse_sensitivity_radians, 0.003F);
  EXPECT_FLOAT_EQ(result.value().physics.walk_speed, 4.0F);
  EXPECT_FLOAT_EQ(result.value().physics.sprint_speed, 7.0F);
  EXPECT_FLOAT_EQ(result.value().physics.sneak_speed, 2.0F);
  EXPECT_FLOAT_EQ(result.value().physics.air_speed, 3.0F);
  EXPECT_FLOAT_EQ(result.value().physics.fly_speed, 9.0F);
  EXPECT_FLOAT_EQ(result.value().physics.fly_vertical_speed, 7.0F);
  EXPECT_FLOAT_EQ(result.value().physics.jump_velocity, 9.0F);
  EXPECT_FLOAT_EQ(result.value().physics.gravity_per_second, 30.0F);
  EXPECT_FLOAT_EQ(result.value().physics.terminal_velocity, 50.0F);
  EXPECT_FLOAT_EQ(result.value().physics.body_width, 0.65F);
  EXPECT_FLOAT_EQ(result.value().physics.body_height, 1.9F);
  EXPECT_FLOAT_EQ(result.value().physics.sneaking_body_height, 1.45F);
  EXPECT_FLOAT_EQ(result.value().physics.swimming_body_width, 0.95F);
  EXPECT_FLOAT_EQ(result.value().physics.swimming_body_height, 0.55F);
  EXPECT_FLOAT_EQ(result.value().physics.sneak_support_probe_depth, 0.06F);
  EXPECT_FLOAT_EQ(result.value().physics.collision_epsilon, 0.002F);
  EXPECT_FLOAT_EQ(result.value().physics.surface_swim_speed, 2.75F);
  EXPECT_FLOAT_EQ(result.value().physics.underwater_swim_speed, 2.25F);
  EXPECT_FLOAT_EQ(result.value().physics.fast_surface_swim_speed, 5.75F);
  EXPECT_FLOAT_EQ(result.value().physics.fast_underwater_swim_speed, 5.25F);
  EXPECT_FLOAT_EQ(result.value().physics.upward_swim_acceleration, 19.0F);
  EXPECT_FLOAT_EQ(result.value().physics.downward_swim_acceleration, 20.0F);
  EXPECT_FLOAT_EQ(result.value().physics.liquid_drag, 0.75F);
  EXPECT_FLOAT_EQ(result.value().physics.surface_float_depth, 0.9F);
  EXPECT_FLOAT_EQ(result.value().physics.surface_buoyancy, 11.0F);
  EXPECT_FLOAT_EQ(result.value().physics.underwater_buoyancy, 1.25F);
  EXPECT_FLOAT_EQ(result.value().physics.breath_seconds, 18.0F);
  EXPECT_FLOAT_EQ(result.value().physics.max_head_body_yaw_radians, 1.1F);
  EXPECT_FLOAT_EQ(result.value().gameplay.block_breaking.base_seconds_per_hardness, 1.5F);
  EXPECT_FLOAT_EQ(result.value().gameplay.block_breaking.creative_inventory_multiplier, 0.05F);
  EXPECT_FLOAT_EQ(result.value().gameplay.block_breaking.hand_speed_multiplier, 0.25F);
  EXPECT_FLOAT_EQ(result.value().gameplay.block_breaking.wrong_tool_speed_multiplier, 0.1F);
  EXPECT_FLOAT_EQ(result.value().gameplay.dropped_items.pickup_radius, 1.75F);
  EXPECT_FLOAT_EQ(result.value().gameplay.dropped_items.pickup_delay_seconds, 0.2F);
  EXPECT_EQ(result.value().render.max_chunk_mesh_uploads_per_frame, 3U);
  EXPECT_TRUE(result.value().render.frame_pacing_enabled);
  EXPECT_EQ(result.value().render.frame_pacing_target_fps, 120U);
  EXPECT_EQ(result.value().render.frame_pacing_spin_us, 250U);
  EXPECT_FALSE(result.value().render.show_local_player_body);
  EXPECT_FLOAT_EQ(result.value().render.block_outline.color_r, 0.1F);
  EXPECT_FLOAT_EQ(result.value().render.block_outline.color_g, 0.2F);
  EXPECT_FLOAT_EQ(result.value().render.block_outline.color_b, 0.3F);
  EXPECT_FLOAT_EQ(result.value().render.block_outline.thickness, 0.75F);
  EXPECT_EQ(result.value().render.atlases.blocks.page_size, 1024U);
  EXPECT_EQ(result.value().render.atlases.blocks.max_pages, 2U);
  EXPECT_EQ(result.value().render.atlases.entities.page_size, 1536U);
  EXPECT_EQ(result.value().render.atlases.entities.max_pages, 5U);
  EXPECT_EQ(result.value().render.atlases.ui.page_size, 512U);
  EXPECT_EQ(result.value().render.atlases.ui.max_pages, 6U);
  EXPECT_EQ(result.value().render.atlases.fonts.page_size, 1024U);
  EXPECT_EQ(result.value().render.atlases.fonts.max_pages, 7U);
  EXPECT_EQ(result.value().render.atlases.player_skins.page_size, 1024U);
  EXPECT_EQ(result.value().render.atlases.player_skins.max_pages, 3U);
  EXPECT_EQ(result.value().render.atlases.player_skins.cleanup.interval_seconds, 30U);
  EXPECT_EQ(
      result.value().render.atlases.player_skins.cleanup.evict_after_unused_seconds,
      120U);
  EXPECT_FALSE(result.value().render.atlases.player_skins.pressure_cleanup.enabled);
  EXPECT_FLOAT_EQ(
      result.value().render.atlases.player_skins.pressure_cleanup.usage_threshold,
      0.9F);
  EXPECT_FLOAT_EQ(
      result.value().render.atlases.player_skins.pressure_cleanup.target_usage,
      0.5F);
  EXPECT_EQ(result.value().render.atlases.particles.page_size, 256U);
  EXPECT_EQ(result.value().render.atlases.particles.max_pages, 2U);
  EXPECT_EQ(result.value().chunk_pipeline.max_generation_jobs_per_frame, 2U);
  EXPECT_EQ(result.value().chunk_pipeline.max_generation_results_per_tick, 3U);
  EXPECT_EQ(result.value().chunk_pipeline.max_lighting_results_per_tick, 4U);
  EXPECT_EQ(result.value().chunk_pipeline.max_client_chunk_applies_per_frame, 6U);
  EXPECT_EQ(result.value().chunk_pipeline.max_client_chunk_unloads_per_frame, 7U);
  EXPECT_EQ(result.value().chunk_pipeline.max_mesh_jobs_per_frame, 5U);
  EXPECT_EQ(result.value().chunk_pipeline.frame_spike_log_ms, 25U);
  EXPECT_EQ(result.value().chunk_pipeline.profile_log_interval_ms, 750U);
}

TEST(ConfigTests, ParsesSkinConfigAndDefaultsResolutionLimits) {
  const auto result = arv::io::parse_config_yaml(R"(
skins:
  enabled: true
  allow_uploads: false
  resolutions:
    - size: 64
      enabled: true
    - size: 128
      enabled: true
      permission: example.perm.128skin
  uploads:
    max_chunk_bytes: 16384
    chunk_threshold_bytes: 65536
    max_active_per_connection: 1
    max_active_global: 12
    timeout_seconds: 20
    idle_timeout_seconds: 4
    max_begin_attempts_per_minute: 5
    max_completed_uploads_per_minute: 3
    max_completed_uploads_per_hour: 11
    max_upload_bytes_per_hour: 123456
    max_failed_decodes_per_minute: 2
  downloads:
    max_requests_per_second: 7
    max_requests_per_minute: 70
    max_bytes_per_minute: 7654321
    max_in_flight_per_connection: 3
    require_advertised_hash: false
    max_batch_size: 8
  storage:
    max_total_skin_bytes: 999999
    max_skins_per_profile: 9
)");

  ASSERT_TRUE(result);
  EXPECT_TRUE(result.value().skins.enabled);
  EXPECT_FALSE(result.value().skins.allow_uploads);
  ASSERT_EQ(result.value().skins.resolutions.size(), 2U);
  EXPECT_EQ(result.value().skins.resolutions[0].max_upload_bytes, 32768U);
  EXPECT_EQ(result.value().skins.resolutions[0].max_decoded_bytes, 64U * 64U * 4U);
  EXPECT_EQ(result.value().skins.resolutions[1].max_upload_bytes, 65536U);
  EXPECT_EQ(result.value().skins.resolutions[1].permission, "example.perm.128skin");
  EXPECT_EQ(result.value().skins.uploads.max_chunk_bytes, 16384U);
  EXPECT_EQ(result.value().skins.uploads.max_active_global, 12U);
  EXPECT_EQ(result.value().skins.uploads.timeout_seconds, 20U);
  EXPECT_EQ(result.value().skins.uploads.idle_timeout_seconds, 4U);
  EXPECT_EQ(result.value().skins.uploads.max_begin_attempts_per_minute, 5U);
  EXPECT_EQ(result.value().skins.uploads.max_completed_uploads_per_minute, 3U);
  EXPECT_EQ(result.value().skins.uploads.max_completed_uploads_per_hour, 11U);
  EXPECT_EQ(result.value().skins.uploads.max_upload_bytes_per_hour, 123456U);
  EXPECT_EQ(result.value().skins.uploads.max_failed_decodes_per_minute, 2U);
  EXPECT_EQ(result.value().skins.downloads.max_requests_per_second, 7U);
  EXPECT_EQ(result.value().skins.downloads.max_requests_per_minute, 70U);
  EXPECT_EQ(result.value().skins.downloads.max_bytes_per_minute, 7654321U);
  EXPECT_EQ(result.value().skins.downloads.max_in_flight_per_connection, 3U);
  EXPECT_FALSE(result.value().skins.downloads.require_advertised_hash);
  EXPECT_EQ(result.value().skins.downloads.max_batch_size, 8U);
  EXPECT_EQ(result.value().skins.storage.max_total_skin_bytes, 999999U);
  EXPECT_EQ(result.value().skins.storage.max_skins_per_profile, 9U);
  EXPECT_EQ(arv::io::default_skin_max_upload_bytes(2048), 16U * 1024U * 1024U);
}

TEST(ConfigTests, SkinSecurityDefaultsMatchDocumentedLimits) {
  const arv::io::EngineConfig config = arv::io::default_config();

  EXPECT_EQ(arv::io::default_skin_max_upload_bytes(64), 32U * 1024U);
  EXPECT_EQ(arv::io::default_skin_max_upload_bytes(128), 64U * 1024U);
  EXPECT_EQ(arv::io::default_skin_max_upload_bytes(256), 256U * 1024U);
  EXPECT_EQ(arv::io::default_skin_max_upload_bytes(512), 1024U * 1024U);
  EXPECT_EQ(arv::io::default_skin_max_upload_bytes(1024), 4U * 1024U * 1024U);
  EXPECT_EQ(arv::io::default_skin_max_upload_bytes(2048), 16U * 1024U * 1024U);
  EXPECT_EQ(arv::io::default_skin_max_decoded_bytes(64), 64U * 64U * 4U);
  ASSERT_FALSE(config.skins.resolutions.empty());
  for (const arv::io::SkinResolutionConfig& resolution : config.skins.resolutions) {
    EXPECT_EQ(
        resolution.max_decoded_bytes,
        arv::io::default_skin_max_decoded_bytes(resolution.size));
    EXPECT_EQ(resolution.enabled, resolution.size == 64U);
  }

  EXPECT_EQ(config.skins.uploads.max_chunk_bytes, 32768U);
  EXPECT_EQ(config.skins.uploads.max_active_per_connection, 1U);
  EXPECT_EQ(config.skins.uploads.max_active_global, 32U);
  EXPECT_EQ(config.skins.uploads.timeout_seconds, 15U);
  EXPECT_EQ(config.skins.uploads.idle_timeout_seconds, 5U);
  EXPECT_EQ(config.skins.uploads.max_begin_attempts_per_minute, 6U);
  EXPECT_EQ(config.skins.uploads.max_completed_uploads_per_minute, 2U);
  EXPECT_EQ(config.skins.uploads.max_completed_uploads_per_hour, 10U);
  EXPECT_EQ(config.skins.uploads.max_upload_bytes_per_hour, 5ULL * 1024ULL * 1024ULL);
  EXPECT_EQ(config.skins.uploads.max_failed_decodes_per_minute, 3U);
  EXPECT_EQ(config.skins.downloads.max_requests_per_second, 8U);
  EXPECT_EQ(config.skins.downloads.max_requests_per_minute, 120U);
  EXPECT_EQ(config.skins.downloads.max_bytes_per_minute, 8ULL * 1024ULL * 1024ULL);
  EXPECT_EQ(config.skins.downloads.max_in_flight_per_connection, 4U);
  EXPECT_TRUE(config.skins.downloads.require_advertised_hash);
  EXPECT_EQ(config.skins.downloads.max_batch_size, 16U);
  EXPECT_EQ(config.skins.storage.max_total_skin_bytes, 1024ULL * 1024ULL * 1024ULL);
  EXPECT_EQ(config.skins.storage.max_skins_per_profile, 16U);
  EXPECT_EQ(config.render.atlases.blocks.page_size, 4096U);
  EXPECT_EQ(config.render.atlases.blocks.max_pages, 8U);
  EXPECT_EQ(config.render.atlases.entities.page_size, 2048U);
  EXPECT_EQ(config.render.atlases.entities.max_pages, 8U);
  EXPECT_EQ(config.render.atlases.ui.page_size, 2048U);
  EXPECT_EQ(config.render.atlases.ui.max_pages, 4U);
  EXPECT_EQ(config.render.atlases.fonts.page_size, 2048U);
  EXPECT_EQ(config.render.atlases.fonts.max_pages, 4U);
  EXPECT_EQ(config.render.atlases.player_skins.page_size, 2048U);
  EXPECT_EQ(config.render.atlases.player_skins.max_pages, 8U);
  EXPECT_EQ(config.render.atlases.player_skins.cleanup.interval_seconds, 60U);
  EXPECT_EQ(
      config.render.atlases.player_skins.cleanup.evict_after_unused_seconds,
      300U);
  EXPECT_TRUE(config.render.atlases.player_skins.pressure_cleanup.enabled);
  EXPECT_FLOAT_EQ(
      config.render.atlases.player_skins.pressure_cleanup.usage_threshold,
      0.85F);
  EXPECT_FLOAT_EQ(
      config.render.atlases.player_skins.pressure_cleanup.target_usage,
      0.65F);
  EXPECT_EQ(config.render.atlases.particles.page_size, 2048U);
  EXPECT_EQ(config.render.atlases.particles.max_pages, 4U);
}

TEST(ConfigTests, RejectsInvalidSkinResolution) {
  const auto result = arv::io::parse_config_yaml(R"(
skins:
  resolutions:
    - size: 96
)");

  EXPECT_FALSE(result);
}

TEST(ConfigTests, RejectsUnsupportedBaseTps) {
  const auto result = arv::io::parse_config_yaml(R"(
simulation:
  base_tps: 57
)");

  EXPECT_FALSE(result);
}

TEST(ConfigTests, RejectsInvalidTickSystemConfig) {
  const auto result = arv::io::parse_config_yaml(R"(
simulation:
  systems:
    fluids:
      interval: 0
      budget: 2000
)");

  EXPECT_FALSE(result);
}

TEST(ConfigTests, RejectsInvalidFluidSpeedConfig) {
  const auto result = arv::io::parse_config_yaml(R"(
simulation:
  fluids:
    water:
      flow_interval_ticks: 0
)");

  EXPECT_FALSE(result);
}

TEST(ConfigTests, RejectsInvalidPhysicsConfig) {
  const auto result = arv::io::parse_config_yaml(R"(
physics:
  body_width: 0
)");

  EXPECT_FALSE(result);
}

TEST(ConfigTests, RejectsChunkDistancesBeyondLoadDistance) {
  const auto result = arv::io::parse_config_yaml(R"(
chunks:
  load_distance: 2
  simulation_distance: 3
)");

  EXPECT_FALSE(result);
}

TEST(ConfigTests, RejectsWorldNamePathTraversal) {
  const auto result = arv::io::parse_config_yaml(R"(
world: ../default
)");

  EXPECT_FALSE(result);
}

TEST(ConfigTests, ResolvesDefaultRuntimePathsFromProjectRoot) {
  arv::io::EngineConfig config = arv::io::default_config();
  arv::io::ConfigLoadOptions options;
  options.project_root = "/tmp/arvane_project";

  arv::io::resolve_runtime_paths(options, config);

  EXPECT_EQ(config.paths.arvane_home_path(), std::filesystem::path("/tmp/arvane_project/arvane_home"));
  EXPECT_EQ(config.paths.assets_path(), std::filesystem::path("/tmp/arvane_project/arvane_home/assets"));
  EXPECT_EQ(config.paths.plugins_path(), std::filesystem::path("/tmp/arvane_project/arvane_home/plugins"));
  EXPECT_EQ(config.paths.config_path(), std::filesystem::path("/tmp/arvane_project/arvane_home/config"));
  EXPECT_EQ(config.paths.saves_path(), std::filesystem::path("/tmp/arvane_project/arvane_home/saves"));
  EXPECT_EQ(config.paths.config_file, std::filesystem::path("/tmp/arvane_project/arvane_home/config/client.yaml"));
  EXPECT_EQ(
      config.paths.shared_config_file,
      std::filesystem::path("/tmp/arvane_project/arvane_home/config/arvane.yaml"));
  EXPECT_EQ(
      config.paths.client_config_file,
      std::filesystem::path("/tmp/arvane_project/arvane_home/config/client.yaml"));
  EXPECT_EQ(
      config.paths.server_config_file,
      std::filesystem::path("/tmp/arvane_project/arvane_home/config/server.yaml"));
  EXPECT_EQ(
      config.paths.input_bindings_file,
      std::filesystem::path("/tmp/arvane_project/arvane_home/config/input_bindings.yaml"));
  EXPECT_EQ(
      config.paths.server_list_file,
      std::filesystem::path("/tmp/arvane_project/arvane_home/config/server_list.json"));
  EXPECT_EQ(
      config.paths.dedicated_server_save_path(),
      std::filesystem::path("/tmp/arvane_project/arvane_home/server-save"));
}

TEST(ConfigTests, ResolvesServerRuntimeConfigPathFromProjectRoot) {
  arv::io::EngineConfig config = arv::io::default_config();
  arv::io::ConfigLoadOptions options;
  options.project_root = "/tmp/arvane_project";
  options.profile = arv::io::ConfigProfile::Server;

  arv::io::resolve_runtime_paths(options, config);

  EXPECT_EQ(config.paths.config_file, std::filesystem::path("/tmp/arvane_project/arvane_home/config/server.yaml"));
}

TEST(ConfigTests, ConstructsServerSavePathsUnderArvaneHomeSaves) {
  arv::io::EngineConfig config = arv::io::default_config();
  arv::io::ConfigLoadOptions options;
  options.project_root = "/tmp/arvane_project";

  arv::io::resolve_runtime_paths(options, config);

  EXPECT_EQ(
      config.paths.server_save_path("server-save-2"),
      std::filesystem::path("/tmp/arvane_project/arvane_home/saves/server-save-2"));
}

TEST(ConfigTests, SavesAndLoadsMultiplayerServerList) {
  const std::filesystem::path temp_root =
      std::filesystem::temp_directory_path() /
      ("arvane_server_list_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code error;
  std::filesystem::remove_all(temp_root, error);

  const std::filesystem::path list_file = temp_root / "arvane_home/config/server_list.json";
  const std::vector<arv::io::SavedServerListEntry> servers{
      {"Local", "localhost"},
      {"LAN", "192.168.1.20:42069"}};

  const auto save_result = arv::io::save_server_list_file(list_file, servers);
  ASSERT_TRUE(save_result);

  const auto load_result = arv::io::load_server_list_file(list_file);
  ASSERT_TRUE(load_result);
  ASSERT_EQ(load_result.value().size(), 2U);
  EXPECT_EQ(load_result.value()[0].name, "Local");
  EXPECT_EQ(load_result.value()[0].address, "localhost");
  EXPECT_EQ(load_result.value()[1].name, "LAN");
  EXPECT_EQ(load_result.value()[1].address, "192.168.1.20:42069");

  std::filesystem::remove_all(temp_root, error);
}

TEST(ConfigTests, ExplicitConfigPathIsResolvedFromProjectRoot) {
  arv::io::EngineConfig config = arv::io::default_config();
  arv::io::ConfigLoadOptions options;
  options.project_root = "/tmp/arvane_project";
  options.explicit_config_path = "config/dev.yaml";

  arv::io::resolve_runtime_paths(options, config);

  EXPECT_EQ(config.paths.config_file, std::filesystem::path("/tmp/arvane_project/config/dev.yaml"));
}

TEST(ConfigTests, ExplicitArvaneHomeOverridesConfigArvaneHome) {
  auto result = arv::io::parse_config_yaml(R"(
paths:
  arvane_home: config_home
)");
  ASSERT_TRUE(result);

  arv::io::ConfigLoadOptions options;
  options.project_root = "/tmp/arvane_project";
  options.explicit_arvane_home_path = "/opt/arvane/runtime";

  arv::io::EngineConfig config = result.value();
  arv::io::resolve_runtime_paths(options, config);

  EXPECT_EQ(config.paths.arvane_home_path(), std::filesystem::path("/opt/arvane/runtime"));
  EXPECT_EQ(config.paths.assets_path(), std::filesystem::path("/opt/arvane/runtime/assets"));
}

TEST(ConfigTests, LoadConfigDefaultsDiscoverProjectArvaneHomeAssets) {
  auto result = arv::io::load_config({});
  ASSERT_TRUE(result);

  EXPECT_EQ(
      result.value().paths.arvane_home_path(),
      result.value().paths.project_root / "arvane_home");
  EXPECT_EQ(
      result.value().paths.assets_path(),
      result.value().paths.project_root / "arvane_home" / "assets");
  EXPECT_EQ(
      result.value().paths.assets_path().string().find("/.local/share/Trash/"),
      std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(result.value().paths.config_path() / "blocks.json"));
  EXPECT_TRUE(std::filesystem::exists(result.value().paths.assets_path() / "shaders/world/chunk.vert"));
  EXPECT_EQ(
      result.value().paths.config_file,
      result.value().paths.config_path() / "client.yaml");
  EXPECT_EQ(
      result.value().paths.shared_config_file,
      result.value().paths.config_path() / "arvane.yaml");
  EXPECT_EQ(
      result.value().paths.client_config_file,
      result.value().paths.config_path() / "client.yaml");
  EXPECT_EQ(
      result.value().paths.server_config_file,
      result.value().paths.config_path() / "server.yaml");
  EXPECT_EQ(
      result.value().paths.input_bindings_file,
      result.value().paths.config_path() / "input_bindings.yaml");
  EXPECT_EQ(
      result.value().paths.server_list_file,
      result.value().paths.config_path() / "server_list.json");
}

TEST(ConfigTests, LoadConfigDefaultsPreferProjectRootOverWorkingDirectoryRuntimeHome) {
  const std::filesystem::path temp_root =
      std::filesystem::temp_directory_path() /
      ("arvane_stale_runtime_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code error;
  std::filesystem::remove_all(temp_root, error);

  write_file(temp_root / "arvane_home/config/blocks.json", "{}\n");
  write_file(temp_root / "arvane_home/assets/shaders/world/chunk.vert", "#version 330 core\n");

  {
    const ScopedCurrentPath scoped_current_path{temp_root};
    auto result = arv::io::load_config({});
    ASSERT_TRUE(result);

    EXPECT_NE(result.value().paths.project_root, temp_root);
    EXPECT_NE(
        result.value().paths.assets_path(),
        temp_root / "arvane_home" / "assets");
    EXPECT_TRUE(std::filesystem::exists(
        result.value().paths.assets_path() / "shaders/world/chunk.vert"));
  }

  std::filesystem::remove_all(temp_root, error);
}

TEST(ConfigTests, MissingSplitConfigReturnsDefaultsAndWritesConfigFiles) {
  const std::filesystem::path temp_root =
      std::filesystem::temp_directory_path() /
      ("arvane_config_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code error;
  std::filesystem::remove_all(temp_root, error);

  arv::io::ConfigLoadOptions options;
  options.project_root = temp_root;

  const auto result = arv::io::load_config(options);

  ASSERT_TRUE(result);
  const std::filesystem::path shared_config_file =
      temp_root / "arvane_home/config/arvane.yaml";
  const std::filesystem::path client_config_file =
      temp_root / "arvane_home/config/client.yaml";
  const std::filesystem::path server_config_file =
      temp_root / "arvane_home/config/server.yaml";
  EXPECT_EQ(result.value().paths.config_file, client_config_file);
  EXPECT_TRUE(std::filesystem::exists(shared_config_file));
  EXPECT_TRUE(std::filesystem::exists(client_config_file));
  EXPECT_TRUE(std::filesystem::exists(server_config_file));

  const auto written_shared =
      arv::io::read_text_file(shared_config_file, "generated shared config");
  ASSERT_TRUE(written_shared);
  EXPECT_NE(written_shared.value().find("arvane_home: arvane_home"), std::string::npos);
  EXPECT_NE(written_shared.value().find("simulation:"), std::string::npos);
  EXPECT_NE(written_shared.value().find("gameplay:"), std::string::npos);
  EXPECT_EQ(written_shared.value().find(temp_root.string()), std::string::npos);

  const auto written_client =
      arv::io::read_text_file(client_config_file, "generated client config");
  ASSERT_TRUE(written_client);
  EXPECT_NE(written_client.value().find("window:"), std::string::npos);
  EXPECT_NE(written_client.value().find("input_bindings_file:"), std::string::npos);
  EXPECT_NE(written_client.value().find("server_list_file:"), std::string::npos);

  const auto written_server =
      arv::io::read_text_file(server_config_file, "generated server config");
  ASSERT_TRUE(written_server);
  EXPECT_NE(written_server.value().find("network:"), std::string::npos);
  EXPECT_NE(written_server.value().find("world: default"), std::string::npos);

  std::filesystem::remove_all(temp_root, error);
}

TEST(ConfigTests, ParsesAndFallsBackInputBindings) {
  auto result = arv::io::parse_input_bindings_yaml(R"yaml(
version: 1
bindings:
  MoveForward:
    - device: keyboard
      key: F
  Sneak:
    - device: keyboard
      key: LeftShift
  UnknownAction:
    - device: keyboard
      key: Q
)yaml");

  ASSERT_TRUE(result);
  const auto& move_forward =
      arv::platform::bindings_for_action(result.value(), arv::platform::LogicalAction::MoveForward);
  const auto& move_backward =
      arv::platform::bindings_for_action(result.value(), arv::platform::LogicalAction::MoveBackward);
  const auto& sneak =
      arv::platform::bindings_for_action(result.value(), arv::platform::LogicalAction::Sneak);
  const auto& open_inventory =
      arv::platform::bindings_for_action(result.value(), arv::platform::LogicalAction::OpenInventory);
  const auto& debug_menu =
      arv::platform::bindings_for_action(result.value(), arv::platform::LogicalAction::ToggleDebugMenu);

  ASSERT_EQ(move_forward.size(), 1U);
  EXPECT_EQ(move_forward[0].input_code, "F");
  ASSERT_EQ(move_backward.size(), 1U);
  EXPECT_EQ(move_backward[0].input_code, "S");
  ASSERT_EQ(sneak.size(), 1U);
  EXPECT_EQ(sneak[0].input_code, "LShift");
  ASSERT_EQ(open_inventory.size(), 1U);
  EXPECT_EQ(open_inventory[0].input_code, "E");
  ASSERT_EQ(debug_menu.size(), 1U);
  EXPECT_EQ(debug_menu[0].input_code, "Grave");
}

TEST(ConfigTests, MissingInputBindingsFileReturnsDefaultsAndWritesConfig) {
  const std::filesystem::path temp_root =
      std::filesystem::temp_directory_path() /
      ("arvane_input_bindings_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".yaml");
  std::error_code error;
  std::filesystem::remove(temp_root, error);

  const auto result = arv::io::load_input_bindings_file_or_defaults(temp_root);

  ASSERT_TRUE(result);
  EXPECT_TRUE(std::filesystem::exists(temp_root));
  const auto& jump =
      arv::platform::bindings_for_action(result.value(), arv::platform::LogicalAction::Jump);
  const auto& debug_menu =
      arv::platform::bindings_for_action(result.value(), arv::platform::LogicalAction::ToggleDebugMenu);
  ASSERT_EQ(jump.size(), 1U);
  EXPECT_EQ(jump[0].input_code, "Space");
  ASSERT_EQ(debug_menu.size(), 1U);
  EXPECT_EQ(debug_menu[0].input_code, "Grave");

  std::filesystem::remove(temp_root, error);
}
