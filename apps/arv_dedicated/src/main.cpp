#include "arv/core/fixed_timestep.hpp"
#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/server/dedicated_lifetime.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace {

std::atomic_bool g_shutdown_requested{false};

void handle_shutdown_signal(int) {
  g_shutdown_requested.store(true);
}

[[nodiscard]] arv::player::PlayerTuning make_player_tuning(
    const arv::io::PhysicsConfig& physics) noexcept {
  arv::player::PlayerTuning tuning = arv::player::default_player_tuning();
  tuning.standing_eye_height = physics.standing_eye_height;
  tuning.sneaking_eye_height = physics.sneaking_eye_height;
  tuning.swimming_eye_height = physics.swimming_eye_height;
  tuning.eye_height_lerp_per_second = physics.eye_height_lerp_per_second;
  tuning.reach_distance = physics.reach_distance;
  tuning.mouse_sensitivity_radians = physics.mouse_sensitivity_radians;
  tuning.walk_speed = physics.walk_speed;
  tuning.sprint_speed = physics.sprint_speed;
  tuning.sneak_speed = physics.sneak_speed;
  tuning.air_speed = physics.air_speed;
  tuning.fly_speed = physics.fly_speed;
  tuning.fly_vertical_speed = physics.fly_vertical_speed;
  tuning.jump_velocity = physics.jump_velocity;
  tuning.gravity_per_second = physics.gravity_per_second;
  tuning.terminal_velocity = physics.terminal_velocity;
  tuning.body_width = physics.body_width;
  tuning.body_height = physics.body_height;
  tuning.sneaking_body_height = physics.sneaking_body_height;
  tuning.swimming_body_width = physics.swimming_body_width;
  tuning.swimming_body_height = physics.swimming_body_height;
  tuning.sneak_support_probe_depth = physics.sneak_support_probe_depth;
  tuning.collision_epsilon = physics.collision_epsilon;
  tuning.surface_swim_speed = physics.surface_swim_speed;
  tuning.underwater_swim_speed = physics.underwater_swim_speed;
  tuning.fast_surface_swim_speed = physics.fast_surface_swim_speed;
  tuning.fast_underwater_swim_speed = physics.fast_underwater_swim_speed;
  tuning.upward_swim_acceleration = physics.upward_swim_acceleration;
  tuning.downward_swim_acceleration = physics.downward_swim_acceleration;
  tuning.liquid_drag = physics.liquid_drag;
  tuning.surface_float_depth = physics.surface_float_depth;
  tuning.surface_buoyancy = physics.surface_buoyancy;
  tuning.underwater_buoyancy = physics.underwater_buoyancy;
  tuning.breath_seconds = physics.breath_seconds;
  return tuning;
}

struct DedicatedLaunchOptions {
  arv::io::ConfigLoadOptions config{};
  arv::u64 max_ticks{};
};

DedicatedLaunchOptions parse_launch_options(int argc, char** argv) {
  DedicatedLaunchOptions options;
  bool explicit_config = false;

  for (int i = 1; i + 1 < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--ticks") {
      options.max_ticks = static_cast<arv::u64>(std::strtoull(argv[++i], nullptr, 10));
    } else if (arg == "--config") {
      options.config.explicit_config_path = std::filesystem::path{argv[++i]};
      explicit_config = true;
    } else if (arg == "--arvane-home") {
      options.config.explicit_arvane_home_path = std::filesystem::path{argv[++i]};
    } else if (arg == "--project-root") {
      options.config.project_root = std::filesystem::path{argv[++i]};
    }
  }

  options.config.missing_config_is_error = explicit_config;
  return options;
}

void transition(
    arv::server::DedicatedServerLifetime& lifetime,
    arv::server::DedicatedServerLifetimeState state) {
  lifetime.transition(state);
  arv::core::info("dedicated", arv::server::to_string(state));
}

} // namespace

int main(int argc, char** argv) {
  arv::server::DedicatedServerLifetime lifetime;
  std::signal(SIGINT, handle_shutdown_signal);
  std::signal(SIGTERM, handle_shutdown_signal);

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Starting);

  const DedicatedLaunchOptions launch_options = parse_launch_options(argc, argv);

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingConfig);
  auto config_result = arv::io::load_config(launch_options.config);
  if (!config_result) {
    arv::core::error("config", config_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  const arv::io::EngineConfig config = config_result.value();

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingRegistries);
  auto block_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "blocks.json",
      "dedicated block definitions");
  if (!block_definition_text) {
    arv::core::error("blocks", block_definition_text.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto block_definitions =
      arv::voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    arv::core::error("blocks", block_definitions.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto block_registry_result =
      arv::voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!block_registry_result) {
    arv::core::error("blocks", block_registry_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  const arv::voxel::BlockRegistry block_registry =
      std::move(block_registry_result.value());

  auto item_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "items.json",
      "dedicated item definitions");
  if (!item_definition_text) {
    arv::core::error("items", item_definition_text.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto item_definitions =
      arv::player::parse_item_definitions_json(item_definition_text.value());
  if (!item_definitions) {
    arv::core::error("items", item_definitions.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto item_registry_result =
      arv::player::ItemRegistry::build_from_blocks_and_items(
          block_registry,
          std::move(item_definitions.value()));
  if (!item_registry_result) {
    arv::core::error("items", item_registry_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  const arv::player::ItemRegistry item_registry =
      std::move(item_registry_result.value());

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingWorlds);

  arv::net::GameNetworkingSocketsEnvironment gns;
  if (!gns.initialized()) {
    arv::core::error("net", gns.error_message());
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  arv::sched::RuntimeThreadBudgetConfig thread_budget_config;
  thread_budget_config.workers.worker_thread_count = config.jobs.worker_threads;
  thread_budget_config.workers.reserved_thread_count = config.jobs.reserved_threads;
  thread_budget_config.workers.min_worker_thread_count = config.jobs.min_worker_threads;
  thread_budget_config.workers.max_worker_thread_count = config.jobs.max_worker_threads;
  const arv::sched::RuntimeThreadBudgetPlan thread_budget =
      arv::sched::plan_runtime_thread_budget(thread_budget_config);

  arv::server::ServerChunkStreamingConfig chunk_streaming;
  chunk_streaming.max_view_distance = config.chunks.load_distance;
  chunk_streaming.simulation_distance = config.chunks.simulation_distance;
  chunk_streaming.unload_margin = config.chunks.unload_margin;

  arv::world::WorldTickPolicy world_tick_policy;
  world_tick_policy.block_update_interval =
      config.simulation.systems.block_updates.interval;
  world_tick_policy.block_update_budget =
      config.simulation.systems.block_updates.budget;
  world_tick_policy.fluid_interval =
      config.simulation.systems.fluids.interval;
  world_tick_policy.fluid_budget =
      config.simulation.systems.fluids.budget;

  arv::voxel::FluidDefinition water_fluid =
      arv::voxel::make_water_fluid_definition();
  water_fluid.flow_interval_ticks =
      config.simulation.fluids.water.flow_interval_ticks;
  const arv::player::PlayerTuning player_tuning = make_player_tuning(config.physics);

  arv::server::ServerRuntime server(std::make_unique<arv::net::GameNetworkingSocketsTransport>());
  auto start_result = server.start({
      arv::server::ServerMode::Dedicated,
      config.simulation.base_tps,
      {config.network.bind_address, config.network.port},
      &block_registry,
      &item_registry,
      player_tuning,
      water_fluid,
      chunk_streaming,
      std::nullopt,
      config.chunk_pipeline.max_generation_results_per_tick,
      config.chunk_pipeline.max_lighting_results_per_tick,
      config.chunk_pipeline.max_generation_jobs_per_frame,
      thread_budget.primary_workers,
      world_tick_policy});
  if (!start_result) {
    arv::core::error("server", start_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Running);
  const auto sleep_duration = std::chrono::duration<arv::Seconds>(
      arv::tick_duration_seconds(config.simulation.base_tps));

  arv::u64 ticks_run = 0;
  while (!g_shutdown_requested.load() &&
         (launch_options.max_ticks == 0 || ticks_run < launch_options.max_ticks)) {
    server.tick();
    ++ticks_run;
    std::this_thread::sleep_for(sleep_duration);
  }

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Stopping);
  server.shutdown();
  transition(lifetime, arv::server::DedicatedServerLifetimeState::Saving);
  transition(lifetime, arv::server::DedicatedServerLifetimeState::Stopped);
  return 0;
}
