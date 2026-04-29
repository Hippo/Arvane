#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/player/player_state.hpp"
#include "arv/voxel/storage.hpp"
#include "arv/world/fluid_solver.hpp"
#include "arv/world/regions.hpp"
#include "arv/world/world_lighting.hpp"
#include "arv/world/world_generation.hpp"

#include <array>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace arv::world {

enum class MissingNeighborPolicy {
  RequireLoaded,
  TreatMissingAsAir,
};

enum class BlockUpdateReason : u8 {
  NeighborChanged,
  ScheduledTick,
};

struct BlockUpdate {
  BlockPos pos{};
  BlockUpdateReason reason{BlockUpdateReason::NeighborChanged};
};

struct ScheduledBlockUpdate {
  BlockPos pos{};
  TickIndex execute_at{};
  BlockUpdateReason reason{BlockUpdateReason::ScheduledTick};
};

struct WorldTickPolicy {
  u32 block_update_interval{3};
  u32 fluid_interval{3};
  u32 block_update_budget{5000};
  u32 fluid_budget{2000};
};

struct WorldTickStats {
  u32 scheduled_ticks_processed{};
  u32 block_updates_processed{};
  u32 fluid_updates_processed{};
};

class WorldRuntime {
public:
  explicit WorldRuntime(WorldID id);
  [[nodiscard]] WorldID id() const noexcept;
  voxel::ChunkSection& ensure_chunk(
      ChunkCoord coord,
      voxel::ChunkLifecycleState state = voxel::ChunkLifecycleState::Loaded);
  void request_chunk_load(ChunkCoord coord);
  [[nodiscard]] std::optional<voxel::ChunkSnapshot> get_chunk_snapshot(ChunkCoord coord) const;
  void apply_chunk_snapshot(
      ChunkCoord coord,
      std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
      std::optional<std::array<voxel::LightWord, voxel::ChunkVolume>> lights) noexcept;
  bool unload_chunk(ChunkCoord coord) noexcept;
  [[nodiscard]] voxel::ChunkSection* find_chunk(ChunkCoord coord) noexcept;
  [[nodiscard]] const voxel::ChunkSection* find_chunk(ChunkCoord coord) const noexcept;
  [[nodiscard]] usize loaded_chunk_count() const noexcept;
  [[nodiscard]] std::vector<ChunkCoord> loaded_chunk_coords() const;
  [[nodiscard]] TickIndex tick_index() const noexcept;
  void set_tick_index(TickIndex tick_index) noexcept;
  [[nodiscard]] u64 lighting_revision() const noexcept;
  [[nodiscard]] u64 mesh_revision() const noexcept;
  void set_tick_policy(WorldTickPolicy policy) noexcept;
  [[nodiscard]] WorldTickPolicy tick_policy() const noexcept;
  [[nodiscard]] bool is_chunk_active(ChunkCoord coord) const noexcept;
  [[nodiscard]] usize active_chunk_count() const noexcept;
  bool set_chunk_lifecycle_state(ChunkCoord coord, voxel::ChunkLifecycleState state) noexcept;
  void update_simulation_radius(ChunkCoord center, i32 radius) noexcept;
  [[nodiscard]] voxel::BlockVariantID get_block(BlockPos pos) const noexcept;
  [[nodiscard]] voxel::LightWord get_light(BlockPos pos) const noexcept;
  [[nodiscard]] voxel::FluidCell get_fluid(BlockPos pos) const noexcept;
  bool set_block(BlockPos pos, voxel::BlockVariantID variant) noexcept;
  bool set_fluid(
      BlockPos pos,
      voxel::FluidCell fluid,
      const voxel::FluidDefinition& definition) noexcept;
  bool place_fluid_source(BlockPos pos, const voxel::FluidDefinition& definition) noexcept;
  [[nodiscard]] std::optional<voxel::ChunkSnapshot> make_meshing_snapshot(
      ChunkCoord coord,
      MissingNeighborPolicy missing_neighbor_policy = MissingNeighborPolicy::RequireLoaded) const;
  [[nodiscard]] bool validate_mesh_result(ChunkCoord coord, u64 source_version) const noexcept;
  [[nodiscard]] bool validate_mesh_content_result(
      ChunkCoord coord,
      u64 source_content_version) const noexcept;
  bool publish_mesh_result(
      ChunkCoord coord,
      u64 source_block_version,
      u64 source_light_version,
      u64 source_content_version = 0U) noexcept;
  bool publish_generated_chunk(GeneratedChunk generated) noexcept;
  void restore_saved_chunk(
      ChunkCoord coord,
      std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
      std::array<voxel::LightWord, voxel::ChunkVolume> lights) noexcept;
  player::PlayerState& ensure_player(PlayerID player_id);
  [[nodiscard]] player::PlayerState* find_player(PlayerID player_id) noexcept;
  [[nodiscard]] const player::PlayerState* find_player(PlayerID player_id) const noexcept;
  [[nodiscard]] usize player_count() const noexcept;
  [[nodiscard]] std::vector<PlayerID> player_ids() const;
  [[nodiscard]] const RegionRegistry& regions() const noexcept {
    return regions_;
  }
  [[nodiscard]] RegionRegistry& regions() noexcept {
    return regions_;
  }
  [[nodiscard]] const PlayerRegionState* player_region_state(PlayerID player_id) const noexcept;
  RegionMembershipChange update_player_region_state(PlayerID player_id, RegionQuery query);
  void schedule_block_update(
      BlockPos pos,
      TickIndex delay_ticks,
      BlockUpdateReason reason = BlockUpdateReason::ScheduledTick);
  void enqueue_fluid_update(
      BlockPos pos,
      FluidUpdateReason reason = FluidUpdateReason::NeighborChanged);
  [[nodiscard]] usize pending_fluid_update_count() const noexcept;
  [[nodiscard]] usize deferred_boundary_fluid_event_count(ChunkCoord coord) const noexcept;
  [[nodiscard]] usize total_deferred_boundary_fluid_event_count() const noexcept;
  [[nodiscard]] usize pending_block_update_count() const noexcept;
  [[nodiscard]] bool has_lighting_work() const noexcept;
  [[nodiscard]] LightingSnapshot make_lighting_snapshot() const;
  [[nodiscard]] static LightingResult build_lighting_result(
      const LightingSnapshot& snapshot,
      const voxel::BlockRegistry& registry);
  LightingPublishStats publish_lighting_result(LightingResult result) noexcept;
  u32 process_fluid_updates(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      u32 budget);
  WorldTickStats tick(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& fluid_definition);
  [[nodiscard]] std::vector<ChunkCoord> dirty_chunks(voxel::ChunkDirtyFlags flag) const;
  [[nodiscard]] std::vector<ChunkCoord> consume_dirty_mesh_chunks(
      voxel::ChunkDirtyFlags flag);

private:
  friend class FluidSolver;
  friend class WorldLighting;

  inline static constexpr std::array<voxel::BlockFace, voxel::BlockFaceCount> AllFaces{
      voxel::BlockFace::PositiveX,
      voxel::BlockFace::NegativeX,
      voxel::BlockFace::PositiveY,
      voxel::BlockFace::NegativeY,
      voxel::BlockFace::PositiveZ,
      voxel::BlockFace::NegativeZ};

  inline static constexpr std::array<Vec3i, voxel::BlockFaceCount> NeighborOffsets{{
      {1, 0, 0},
      {-1, 0, 0},
      {0, 1, 0},
      {0, -1, 0},
      {0, 0, 1},
      {0, 0, -1},
  }};
  [[nodiscard]] static i32 abs_i32(i32 value) noexcept;
  [[nodiscard]] static i32 max_abs3(i32 x, i32 y, i32 z) noexcept;
  [[nodiscard]] static BlockPos offset_block(BlockPos pos, Vec3i offset) noexcept;
  void mark_lighting_content_dirty() noexcept;
  void mark_mesh_content_dirty() noexcept;
  void enqueue_dirty_mesh_chunk(ChunkCoord coord);
  void recompute_lighting_work_pending() noexcept;
  void enqueue_neighbor_block_updates(BlockPos pos);
  void mark_boundary_neighbors_dirty(ChunkCoord coord, SectionLocalPos local) noexcept;
  void mark_loaded_chunk_neighbors_dirty(ChunkCoord coord) noexcept;
  void mark_light_dependency_neighbors_dirty(ChunkCoord coord) noexcept;
  void mark_neighbor_mesh_dependency_dirty(ChunkCoord coord) noexcept;
  void mark_neighbor_mesh_only_dependency_dirty(ChunkCoord coord) noexcept;
  void mark_neighbor_light_dependency_dirty(ChunkCoord coord) noexcept;
  u32 process_due_scheduled_ticks();
  u32 drain_block_updates(u32 budget);

  WorldID id_{};
  StableHashMap<ChunkCoord, voxel::ChunkSection> chunks_;
  StableHashMap<PlayerID, player::PlayerState> players_;
  RegionRegistry regions_{};
  StableHashMap<PlayerID, PlayerRegionState> player_region_states_;
  TickIndex tick_index_{};
  u64 lighting_revision_{1};
  u64 mesh_revision_{1};
  bool lighting_work_pending_{};
  WorldTickPolicy tick_policy_{};
  std::vector<ScheduledBlockUpdate> scheduled_block_updates_;
  std::deque<BlockUpdate> block_updates_;
  std::deque<FluidUpdate> fluid_updates_;
  HashMap<ChunkCoord, std::vector<FluidUpdate>> deferred_boundary_fluid_updates_;
  std::deque<ChunkCoord> dirty_mesh_chunks_;
  HashSet<ChunkCoord> dirty_mesh_chunk_set_;
};

} // namespace arv::world
