#pragma once

#include "arv/io/config.hpp"
#include "arv/physics/collision.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/server/chunk_streamer.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_runtime.hpp"

namespace arv::desktop {

[[nodiscard]] player::PlayerTuning make_player_tuning(
    const io::PhysicsConfig& physics) noexcept;

[[nodiscard]] physics::KinematicConfig make_local_placement_kinematic_config(
    const io::PhysicsConfig& physics) noexcept;

[[nodiscard]] server::ServerChunkStreamingConfig make_chunk_streaming_config(
    const io::EngineConfig& config) noexcept;

[[nodiscard]] world::WorldTickPolicy make_world_tick_policy(
    const io::SimulationConfig& simulation) noexcept;

[[nodiscard]] voxel::FluidDefinition make_water_fluid_definition(
    const io::SimulationConfig& simulation) noexcept;

[[nodiscard]] sched::RuntimeThreadBudgetPlan make_runtime_thread_budget_plan(
    const io::JobSystemConfig& jobs) noexcept;

[[nodiscard]] sched::JobSystemConfig make_client_job_system_config(
    const io::JobSystemConfig& jobs,
    const sched::RuntimeThreadBudgetPlan& thread_budget) noexcept;

[[nodiscard]] sched::JobSystemConfig make_server_job_system_config(
    const io::JobSystemConfig& jobs) noexcept;

} // namespace arv::desktop
