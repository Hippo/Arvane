#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/voxel/storage.hpp"

#include <vector>

namespace arv::voxel {
class BlockRegistry;
} // namespace arv::voxel

namespace arv::world {

class WorldRuntime;
class WorldSearch;

enum class FluidUpdateReason : u8 {
  FluidAdded,
  FluidRemoved,
  NeighborChanged,
  BlockChanged,
  ChunkActivated,
};

struct FluidUpdate {
  BlockPos pos{};
  FluidUpdateReason reason{FluidUpdateReason::NeighborChanged};
  TickIndex execute_at{};
};

struct FluidSimulationChunk {
  ChunkCoord coord{};
  bool active{};
  u64 source_content_version{};
  voxel::ChunkStorageView storage{};
};

struct FluidSimulationRequest {
  TickIndex current_tick{};
  u32 budget{};
  std::vector<FluidUpdate> updates{};
  std::vector<FluidSimulationChunk> chunks{};
};

struct FluidChange {
  BlockPos pos{};
  voxel::FluidCell fluid{};
  u64 source_content_version{};
};

struct FluidSimulationResult {
  u32 processed{};
  std::vector<FluidChange> changes{};
  std::vector<FluidUpdate> followup_updates{};
  std::vector<FluidUpdate> deferred_updates{};
};

class FluidSolver {
public:
  static void enqueue_neighbor_updates(
      WorldRuntime& world,
      BlockPos pos,
      FluidUpdateReason reason,
      u32 delay_ticks = 0U);
  static void enqueue_chunk_activation_updates(WorldRuntime& world, ChunkCoord coord);
  static void store_deferred_boundary_update(
      WorldRuntime& world,
      ChunkCoord target,
      FluidUpdate update);
  static u32 process_updates(
      WorldRuntime& world,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      u32 budget);
  [[nodiscard]] static FluidSimulationResult build_simulation_result(
      FluidSimulationRequest request,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition);

private:
  [[nodiscard]] static bool can_fluid_occupy(
      const WorldSearch& search,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) noexcept;
  [[nodiscard]] static bool can_flow_down_from(
      const WorldSearch& search,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) noexcept;
  [[nodiscard]] static voxel::FluidCell ideal_fluid_at(
      const WorldSearch& search,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos,
      voxel::FluidCell current) noexcept;
  static void enqueue_adjacent_updates(
      WorldRuntime& world,
      const WorldSearch& search,
      BlockPos pos,
      FluidUpdateReason reason,
      u32 delay_ticks = 0U);
  static void process_single_update(
      WorldRuntime& world,
      WorldSearch& search,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos);
};

} // namespace arv::world
