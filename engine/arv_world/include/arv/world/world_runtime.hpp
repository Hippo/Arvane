#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/player/player_state.hpp"
#include "arv/voxel/storage.hpp"
#include "arv/world/world_generation.hpp"

#include <array>
#include <deque>
#include <optional>
#include <queue>
#include <unordered_map>
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

enum class FluidUpdateReason : u8 {
  FluidAdded,
  FluidRemoved,
  NeighborChanged,
  BlockChanged,
  ChunkActivated,
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

struct FluidUpdate {
  BlockPos pos{};
  FluidUpdateReason reason{FluidUpdateReason::NeighborChanged};
};

struct WorldTickPolicy {
  u32 block_update_interval{3};
  u32 fluid_interval{3};
  u32 block_update_budget{5000};
  u32 fluid_budget{2000};
  bool process_lighting_synchronously{true};
};

struct WorldTickStats {
  u32 scheduled_ticks_processed{};
  u32 block_updates_processed{};
  u32 fluid_updates_processed{};
  bool lighting_processed{};
};

struct ChunkLightingSnapshot {
  ChunkCoord coord{};
  u64 source_version{};
  voxel::ChunkStorageView storage{};

  [[nodiscard]] const std::array<voxel::BlockVariantID, voxel::ChunkVolume>& blocks() const noexcept {
    return storage->blocks;
  }
};

struct LightingSnapshot {
  WorldID world_id{};
  std::vector<ChunkLightingSnapshot> chunks{};
};

struct ChunkLightingResult {
  ChunkCoord coord{};
  u64 source_version{};
  std::array<voxel::LightWord, voxel::ChunkVolume> lights{};
};

struct LightingResult {
  WorldID world_id{};
  std::vector<ChunkLightingResult> chunks{};
};

struct LightingPublishStats {
  u32 accepted_chunks{};
  u32 stale_chunks{};
  u32 changed_chunks{};
};

class WorldRuntime {
public:
  explicit WorldRuntime(WorldID id);
  [[nodiscard]] WorldID id() const noexcept;
  voxel::ChunkSection& ensure_chunk(
      ChunkCoord coord,
      voxel::ChunkLifecycleState state = voxel::ChunkLifecycleState::Loaded);
  [[nodiscard]] voxel::ChunkSection* find_chunk(ChunkCoord coord) noexcept;
  [[nodiscard]] const voxel::ChunkSection* find_chunk(ChunkCoord coord) const noexcept;
  [[nodiscard]] usize loaded_chunk_count() const noexcept;
  [[nodiscard]] std::vector<ChunkCoord> loaded_chunk_coords() const;
  [[nodiscard]] TickIndex tick_index() const noexcept;
  void set_tick_index(TickIndex tick_index) noexcept;
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
  bool publish_mesh_result(ChunkCoord coord, u64 source_version) noexcept;
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
  bool process_lighting(const voxel::BlockRegistry& registry);
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

private:
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
  void enqueue_neighbor_block_updates(BlockPos pos);
  void enqueue_neighbor_fluid_updates(BlockPos pos, FluidUpdateReason reason);
  void enqueue_chunk_activation_updates(ChunkCoord coord);
  void store_deferred_boundary_fluid_update(ChunkCoord target, FluidUpdate update);
  void mark_boundary_neighbors_dirty(ChunkCoord coord, SectionLocalPos local) noexcept;
  void mark_loaded_chunk_neighbors_dirty(ChunkCoord coord) noexcept;
  void mark_light_dependency_neighbors_dirty(ChunkCoord coord) noexcept;
  void mark_neighbor_mesh_dependency_dirty(ChunkCoord coord) noexcept;
  void mark_neighbor_mesh_only_dependency_dirty(ChunkCoord coord) noexcept;
  u32 process_due_scheduled_ticks();
  u32 drain_block_updates(u32 budget);
  [[nodiscard]] bool can_fluid_occupy(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) const noexcept;
  [[nodiscard]] bool can_flow_down_from(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos) const noexcept;
  [[nodiscard]] voxel::FluidCell ideal_fluid_at(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos,
      voxel::FluidCell current) const noexcept;
  void process_single_fluid_update(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      BlockPos pos);
  [[nodiscard]] bool can_light_enter(
      const voxel::BlockRegistry& registry,
      BlockPos pos) const noexcept;
  void rebuild_loaded_lighting(const voxel::BlockRegistry& registry);

  WorldID id_{};
  std::unordered_map<ChunkCoord, voxel::ChunkSection> chunks_;
  std::unordered_map<PlayerID, player::PlayerState> players_;
  TickIndex tick_index_{};
  WorldTickPolicy tick_policy_{};
  std::vector<ScheduledBlockUpdate> scheduled_block_updates_;
  std::deque<BlockUpdate> block_updates_;
  std::deque<FluidUpdate> fluid_updates_;
  std::unordered_map<ChunkCoord, std::vector<FluidUpdate>> deferred_boundary_fluid_updates_;
};

} // namespace arv::world
