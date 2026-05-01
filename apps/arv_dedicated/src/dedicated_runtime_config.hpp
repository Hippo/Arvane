#pragma once

#include "dedicated_error.hpp"

#include "arv/core/expected.hpp"
#include "arv/io/config.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

namespace arv::dedicated {

struct DedicatedLoadedWorld;
struct DedicatedRegistries;

[[nodiscard]] player::PlayerTuning make_player_tuning(
    const io::PhysicsConfig& physics) noexcept;

struct DedicatedRuntimeConfig {
  sched::RuntimeThreadBudgetPlan thread_budget;
  server::ServerChunkStreamingConfig chunk_streaming;
  world::WorldTickPolicy world_tick_policy;
  voxel::FluidDefinition water_fluid;
  player::PlayerTuning player_tuning;
};

[[nodiscard]] DedicatedRuntimeConfig make_dedicated_runtime_config(
    const io::EngineConfig& config);

[[nodiscard]] Expected<server::ServerStartInfo, DedicatedError> make_server_start_info(
    const io::EngineConfig& config,
    const DedicatedRuntimeConfig& runtime_config,
    const DedicatedLoadedWorld& loaded_world,
    const DedicatedRegistries& registries,
    world::WorldGenerationPalette generation_palette);

} // namespace arv::dedicated
