#include "desktop_runtime_config.hpp"

#include <algorithm>

namespace arv::desktop {

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
  tuning.air_control_acceleration_per_second =
      physics.air_control_acceleration_per_second;
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
  tuning.step_height = physics.step_height;
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
  tuning.max_head_body_yaw_radians = physics.max_head_body_yaw_radians;
  return tuning;
}

physics::KinematicConfig make_local_placement_kinematic_config(
    const io::PhysicsConfig& physics) noexcept {
  physics::KinematicConfig config;
  config.body_width = physics.body_width;
  config.body_height = physics.body_height;
  config.sneaking_body_height = physics.sneaking_body_height;
  config.swimming_body_width = physics.swimming_body_width;
  config.swimming_body_height = physics.swimming_body_height;
  config.step_height = physics.step_height;
  return config;
}

server::ServerChunkStreamingConfig make_chunk_streaming_config(
    const io::EngineConfig& config) noexcept {
  server::ServerChunkStreamingConfig chunk_streaming;
  chunk_streaming.max_view_distance = config.chunks.load_distance;
  chunk_streaming.simulation_distance = config.chunks.simulation_distance;
  chunk_streaming.unload_margin = config.chunks.unload_margin;
  chunk_streaming.max_chunks_sent_per_player_tick =
      std::max<u32>(config.chunk_pipeline.max_client_chunk_applies_per_frame, 1U);
  chunk_streaming.max_unloads_sent_per_player_tick = 64;
  return chunk_streaming;
}

world::WorldTickPolicy make_world_tick_policy(
    const io::SimulationConfig& simulation) noexcept {
  world::WorldTickPolicy world_tick_policy;
  world_tick_policy.block_update_interval =
      simulation.systems.block_updates.interval;
  world_tick_policy.block_update_budget =
      simulation.systems.block_updates.budget;
  world_tick_policy.fluid_interval =
      simulation.systems.fluids.interval;
  world_tick_policy.fluid_budget =
      simulation.systems.fluids.budget;
  return world_tick_policy;
}

voxel::FluidDefinition make_water_fluid_definition(
    const io::SimulationConfig& simulation) noexcept {
  voxel::FluidDefinition water_fluid = voxel::make_water_fluid_definition();
  water_fluid.flow_interval_ticks =
      simulation.fluids.water.flow_interval_ticks;
  return water_fluid;
}

sched::RuntimeThreadBudgetPlan make_runtime_thread_budget_plan(
    const io::JobSystemConfig& jobs) noexcept {
  sched::RuntimeThreadBudgetConfig thread_budget_config;
  thread_budget_config.workers.worker_thread_count = jobs.worker_threads;
  thread_budget_config.workers.reserved_thread_count = jobs.reserved_threads;
  thread_budget_config.workers.min_worker_thread_count = jobs.min_worker_threads;
  thread_budget_config.workers.max_worker_thread_count = jobs.max_worker_threads;
  thread_budget_config.split_primary_secondary_pools = true;
  return sched::plan_runtime_thread_budget(thread_budget_config);
}

} // namespace arv::desktop
