#pragma once

#include "dedicated_error.hpp"
#include "dedicated_worldgen_loader.hpp"

#include "arv/core/expected.hpp"
#include "arv/io/config.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/player/permissions.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <vector>

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
  player::GameModeRegistry game_modes;
  voxel::BlockBehaviorRegistry block_behaviors;
  player::ItemBehaviorRegistry item_behaviors;
  world::EntityGoalRegistry entity_goals;
  server::BlockBreakBehaviorRegistry block_break_behaviors;
  std::vector<server::StarterInventoryItem> starter_inventory;
  server::FluidContainerInteractionConfig fluid_containers;
};

[[nodiscard]] DedicatedRuntimeConfig make_dedicated_runtime_config(
    const io::EngineConfig& config);

[[nodiscard]] Expected<server::ServerStartInfo, DedicatedError> make_server_start_info(
    const io::EngineConfig& config,
    const DedicatedRuntimeConfig& runtime_config,
    const DedicatedLoadedWorld& loaded_world,
    const DedicatedRegistries& registries,
    const DedicatedWorldgen& worldgen,
    world::WorldGenerationPalette generation_palette);

} // namespace arv::dedicated
