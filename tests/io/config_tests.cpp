#include "arv/io/config.hpp"
#include "arv/io/input_bindings.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

TEST(ConfigTests, ParsesYamlConfigWithDefaults) {
  const auto result = arv::io::parse_config_yaml(R"(
paths:
  arvane_home: custom_home
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
  EXPECT_EQ(result.value().paths.arvane_home_path(), std::filesystem::path("custom_home"));
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
  EXPECT_EQ(config.paths.config_file, std::filesystem::path("/tmp/arvane_project/arvane_home/config/engine.yaml"));
  EXPECT_EQ(
      config.paths.input_bindings_file,
      std::filesystem::path("/tmp/arvane_project/arvane_home/config/input_bindings.yaml"));
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

  EXPECT_TRUE(std::filesystem::exists(result.value().paths.assets_path() / "config/blocks.json"));
  EXPECT_TRUE(std::filesystem::exists(result.value().paths.assets_path() / "shaders/world/chunk.vert"));
  EXPECT_EQ(
      result.value().paths.config_file,
      result.value().paths.config_path() / "engine.yaml");
  EXPECT_EQ(
      result.value().paths.input_bindings_file,
      result.value().paths.config_path() / "input_bindings.yaml");
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

  ASSERT_EQ(move_forward.size(), 1U);
  EXPECT_EQ(move_forward[0].input_code, "F");
  ASSERT_EQ(move_backward.size(), 1U);
  EXPECT_EQ(move_backward[0].input_code, "S");
  ASSERT_EQ(sneak.size(), 1U);
  EXPECT_EQ(sneak[0].input_code, "LShift");
  ASSERT_EQ(open_inventory.size(), 1U);
  EXPECT_EQ(open_inventory[0].input_code, "E");
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
  ASSERT_EQ(jump.size(), 1U);
  EXPECT_EQ(jump[0].input_code, "Space");

  std::filesystem::remove(temp_root, error);
}
