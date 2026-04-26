#include "arv/io/config.hpp"

#include <gtest/gtest.h>

#include <filesystem>

TEST(ConfigTests, ParsesYamlConfigWithDefaults) {
  const auto result = arv::io::parse_config_yaml(R"(
paths:
  asset_root: custom_assets
window:
  width: 1920
  title: Test Window
simulation:
  base_tps: 30
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
render:
  max_chunk_mesh_uploads_per_frame: 3
chunk_pipeline:
  max_generation_jobs_per_frame: 2
  max_mesh_jobs_per_frame: 5
  frame_spike_log_ms: 25
)");

  ASSERT_TRUE(result);
  EXPECT_EQ(result.value().paths.asset_root, std::filesystem::path("custom_assets"));
  EXPECT_EQ(result.value().window.width, 1920U);
  EXPECT_EQ(result.value().window.height, 720U);
  EXPECT_EQ(result.value().window.title, "Test Window");
  EXPECT_EQ(result.value().simulation.base_tps, 30U);
  EXPECT_EQ(result.value().network.port, 28000U);
  EXPECT_EQ(result.value().chunks.load_distance, 6);
  EXPECT_EQ(result.value().chunks.simulation_distance, 2);
  EXPECT_EQ(result.value().chunks.render_distance, 4);
  EXPECT_EQ(result.value().chunks.stream_distance, 5);
  EXPECT_EQ(result.value().chunks.unload_margin, 3);
  EXPECT_EQ(result.value().jobs.worker_threads, 6U);
  EXPECT_EQ(result.value().jobs.reserved_threads, 5U);
  EXPECT_EQ(result.value().render.max_chunk_mesh_uploads_per_frame, 3U);
  EXPECT_EQ(result.value().chunk_pipeline.max_generation_jobs_per_frame, 2U);
  EXPECT_EQ(result.value().chunk_pipeline.max_mesh_jobs_per_frame, 5U);
  EXPECT_EQ(result.value().chunk_pipeline.frame_spike_log_ms, 25U);
}

TEST(ConfigTests, RejectsUnsupportedBaseTps) {
  const auto result = arv::io::parse_config_yaml(R"(
simulation:
  base_tps: 57
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

TEST(ConfigTests, ResolvesDefaultAssetRootFromProjectRoot) {
  arv::io::EngineConfig config = arv::io::default_config();
  arv::io::ConfigLoadOptions options;
  options.project_root = "/tmp/arvane_project";

  arv::io::resolve_runtime_paths(options, config);

  EXPECT_EQ(config.paths.asset_root, std::filesystem::path("/tmp/arvane_project/assets"));
  EXPECT_TRUE(config.paths.config_file.empty());
}

TEST(ConfigTests, ExplicitConfigPathIsResolvedFromProjectRoot) {
  arv::io::EngineConfig config = arv::io::default_config();
  arv::io::ConfigLoadOptions options;
  options.project_root = "/tmp/arvane_project";
  options.explicit_config_path = "config/dev.yaml";

  arv::io::resolve_runtime_paths(options, config);

  EXPECT_EQ(config.paths.config_file, std::filesystem::path("/tmp/arvane_project/config/dev.yaml"));
}

TEST(ConfigTests, ExplicitAssetRootOverridesConfigAssetRoot) {
  auto result = arv::io::parse_config_yaml(R"(
paths:
  asset_root: config_assets
)");
  ASSERT_TRUE(result);

  arv::io::ConfigLoadOptions options;
  options.project_root = "/tmp/arvane_project";
  options.explicit_asset_root = "/opt/arvane/assets";

  arv::io::EngineConfig config = result.value();
  arv::io::resolve_runtime_paths(options, config);

  EXPECT_EQ(config.paths.asset_root, std::filesystem::path("/opt/arvane/assets"));
}

TEST(ConfigTests, LoadConfigDefaultsDiscoverProjectAssets) {
  auto result = arv::io::load_config({});
  ASSERT_TRUE(result);

  EXPECT_TRUE(std::filesystem::exists(result.value().paths.asset_root / "config/blocks.json"));
  EXPECT_TRUE(std::filesystem::exists(result.value().paths.asset_root / "shaders/world/chunk.vert"));
  EXPECT_TRUE(result.value().paths.config_file.empty());
}
