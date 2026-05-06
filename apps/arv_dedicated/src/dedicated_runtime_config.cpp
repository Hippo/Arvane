#include "dedicated_runtime_config.hpp"

#include "dedicated_registry_loader.hpp"
#include "dedicated_save_state.hpp"

#include <algorithm>
#include <span>
#include <utility>

namespace arv::dedicated {

player::PlayerTuning make_player_tuning(const io::PhysicsConfig& physics) noexcept {
  player::PlayerTuning tuning = player::default_player_tuning();
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

DedicatedRuntimeConfig make_dedicated_runtime_config(
    const io::EngineConfig& config) {
  sched::RuntimeThreadBudgetConfig thread_budget_config;
  thread_budget_config.workers.worker_thread_count = config.jobs.worker_threads;
  thread_budget_config.workers.reserved_thread_count = config.jobs.reserved_threads;
  thread_budget_config.workers.min_worker_thread_count = config.jobs.min_worker_threads;
  thread_budget_config.workers.max_worker_thread_count = config.jobs.max_worker_threads;

  DedicatedRuntimeConfig runtime_config;
  runtime_config.thread_budget =
      sched::plan_runtime_thread_budget(thread_budget_config);

  runtime_config.chunk_streaming.max_view_distance = config.chunks.load_distance;
  runtime_config.chunk_streaming.simulation_distance = config.chunks.simulation_distance;
  runtime_config.chunk_streaming.unload_margin = config.chunks.unload_margin;
  runtime_config.chunk_streaming.max_chunks_sent_per_player_tick =
      std::max<u32>(config.chunk_pipeline.max_client_chunk_applies_per_frame, 1U);
  runtime_config.chunk_streaming.max_unloads_sent_per_player_tick = 64U;

  runtime_config.world_tick_policy.block_update_interval =
      config.simulation.systems.block_updates.interval;
  runtime_config.world_tick_policy.block_update_budget =
      config.simulation.systems.block_updates.budget;
  runtime_config.world_tick_policy.fluid_interval =
      config.simulation.systems.fluids.interval;
  runtime_config.world_tick_policy.fluid_budget =
      config.simulation.systems.fluids.budget;

  runtime_config.water_fluid = voxel::make_water_fluid_definition();
  runtime_config.water_fluid.flow_interval_ticks =
      config.simulation.fluids.water.flow_interval_ticks;
  runtime_config.player_tuning = make_player_tuning(config.physics);
  return runtime_config;
}

Expected<server::ServerStartInfo, DedicatedError> make_server_start_info(
    const io::EngineConfig& config,
    const DedicatedRuntimeConfig& runtime_config,
    const DedicatedLoadedWorld& loaded_world,
    const DedicatedRegistries& registries,
    world::WorldGenerationPalette generation_palette) {
  server::ServerChunkGenerationSettings chunk_generation;
  chunk_generation.enabled = true;
  chunk_generation.seed = loaded_world.world_seed;
  chunk_generation.config = loaded_world.active_generation_config;
  chunk_generation.palette = std::move(generation_palette);

  server::ServerStartInfo server_start_info;
  server_start_info.mode = server::ServerMode::Dedicated;
  server_start_info.base_tps = config.simulation.base_tps;
  server_start_info.initial_tick_index = loaded_world.initial_tick_index;
  server_start_info.world_name = config.server.world;
  server_start_info.server_save_root = loaded_world.server_save_root;
  server_start_info.listen_address = {config.network.bind_address, config.network.port};
  server_start_info.block_registry = &registries.block_registry;
  server_start_info.item_registry = &registries.item_registry;
  server_start_info.player_tuning = runtime_config.player_tuning;
  server_start_info.fluid_definition = runtime_config.water_fluid;
  server_start_info.chunk_streaming = runtime_config.chunk_streaming;
  server_start_info.chunk_generation = std::move(chunk_generation);
  server_start_info.world_height = loaded_world.world_height;
  server_start_info.max_generation_results_per_tick =
      config.chunk_pipeline.max_generation_results_per_tick;
  server_start_info.max_lighting_results_per_tick =
      config.chunk_pipeline.max_lighting_results_per_tick;
  server_start_info.max_pending_generation_jobs =
      config.chunk_pipeline.max_generation_jobs_per_frame;
  server_start_info.worker_threads = runtime_config.thread_budget.primary_workers;
  server_start_info.world_tick_policy = runtime_config.world_tick_policy;
  server_start_info.entity_registry = &registries.entity_registry;
  server_start_info.block_breaking.base_seconds_per_hardness =
      config.gameplay.block_breaking.base_seconds_per_hardness;
  server_start_info.block_breaking.creative_inventory_multiplier =
      config.gameplay.block_breaking.creative_inventory_multiplier;
  server_start_info.block_breaking.hand_speed_multiplier =
      config.gameplay.block_breaking.hand_speed_multiplier;
  server_start_info.block_breaking.wrong_tool_speed_multiplier =
      config.gameplay.block_breaking.wrong_tool_speed_multiplier;
  server_start_info.dropped_items.pickup_radius =
      config.gameplay.dropped_items.pickup_radius;
  server_start_info.dropped_items.pickup_delay_seconds =
      config.gameplay.dropped_items.pickup_delay_seconds;
  server_start_info.saved_players = std::span<const server::ServerPlayerSaveState>{
      loaded_world.saved_players.data(),
      loaded_world.saved_players.size()};

  return Expected<server::ServerStartInfo, DedicatedError>::success(
      std::move(server_start_info));
}

} // namespace arv::dedicated
