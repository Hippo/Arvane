#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/fluid.hpp"

namespace arv::voxel {
class BlockRegistry;
} // namespace arv::voxel

namespace arv::world {

class WorldRuntime;

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
};

class FluidSolver {
public:
  static void enqueue_neighbor_updates(
      WorldRuntime& world,
      BlockPos pos,
      FluidUpdateReason reason);
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

private:
  [[nodiscard]] static bool can_fluid_occupy(
      const WorldRuntime& world,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) noexcept;
  [[nodiscard]] static bool can_flow_down_from(
      const WorldRuntime& world,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) noexcept;
  [[nodiscard]] static voxel::FluidCell ideal_fluid_at(
      const WorldRuntime& world,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos,
      voxel::FluidCell current) noexcept;
  static void process_single_update(
      WorldRuntime& world,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos);
};

} // namespace arv::world
