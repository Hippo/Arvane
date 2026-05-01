#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/player_state.hpp"
#include "arv/voxel/storage.hpp"
#include "arv/world/fluid_solver.hpp"
#include "arv/world/regions.hpp"
#include "arv/world/world_lighting.hpp"
#include "arv/world/world_generation.hpp"

#include <entt/entt.hpp>

#include <array>
#include <deque>
#include <optional>
#include <span>
#include <string>
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
  u32 fluid_interval{1};
  u32 block_update_budget{5000};
  u32 fluid_budget{2000};
};

struct WorldTickStats {
  u32 scheduled_ticks_processed{};
  u32 block_updates_processed{};
  u32 fluid_updates_processed{};
  u32 entity_updates_processed{};
};

enum class EntityDespawnReason : u16 {
  Removed,
  WorldUnload,
  Transfer,
};

struct EntityCreateInfo {
  EntityTypeID type_id{};
  std::string type_name{};
  Vec3f position{};
  Quatf rotation{};
  Vec3f velocity{};
  bool persistent{};
  std::string persistent_uuid{};
  bool replicated{true};
  std::optional<PlayerID> owning_player{};
  std::optional<player::PlayerState> player_state{};
};

struct ReplicatedBlockUpdate {
  BlockPos pos{};
  voxel::BlockVariantID new_state{};
  TickIndex server_tick{};
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
      std::optional<std::array<voxel::LightWord, voxel::ChunkVolume>> lights,
      std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids = std::nullopt) noexcept;
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
  void update_simulation_radii(std::span<const ChunkCoord> centers, i32 radius) noexcept;
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
      std::array<voxel::LightWord, voxel::ChunkVolume> lights,
      std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids = std::nullopt) noexcept;
  [[nodiscard]] EntityID create_entity(EntityCreateInfo info = {});
  bool destroy_entity(EntityID entity_id, EntityDespawnReason reason = EntityDespawnReason::Removed);
  void flush_entity_despawns();
  [[nodiscard]] bool entity_exists(EntityID entity_id) const noexcept;
  [[nodiscard]] usize entity_count() const noexcept;
  [[nodiscard]] std::vector<EntityID> entity_ids() const;
  [[nodiscard]] std::optional<EntityID> entity_for_player(PlayerID player_id) const noexcept;
  [[nodiscard]] std::optional<PlayerID> player_for_entity(EntityID entity_id) const noexcept;
  [[nodiscard]] std::optional<EntityNetID> network_id_for_entity(EntityID entity_id) const noexcept;
  [[nodiscard]] std::optional<ChunkCoord> entity_chunk(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityTransform* find_entity_transform(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityTransform* find_entity_transform(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityVelocity* find_entity_velocity(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityVelocity* find_entity_velocity(EntityID entity_id) const noexcept;
  [[nodiscard]] const entity::EntityType* find_entity_type(EntityID entity_id) const noexcept;
  [[nodiscard]] bool entity_is_persistent(EntityID entity_id) const noexcept;
  bool set_entity_transform(EntityID entity_id, Vec3f position, Quatf rotation) noexcept;
  bool set_entity_position(EntityID entity_id, Vec3f position) noexcept;
  [[nodiscard]] std::vector<entity::EntitySnapshotEntry> collect_entity_snapshots() const;
  [[nodiscard]] std::vector<entity::PersistentEntityRecord> persistent_entity_records() const;
  void restore_persistent_entities(
      std::span<const entity::PersistentEntityRecord> records,
      const entity::EntityRegistry* entity_registry = nullptr);
  u32 tick_entities();
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
      FluidUpdateReason reason = FluidUpdateReason::NeighborChanged,
      u32 delay_ticks = 0U);
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
  [[nodiscard]] std::optional<FluidSimulationRequest> make_fluid_simulation_request(
      u32 budget);
  u32 apply_fluid_simulation_result(
      FluidSimulationResult result,
      const voxel::FluidDefinition& definition);
  WorldTickStats tick(
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& fluid_definition);
  [[nodiscard]] std::vector<ReplicatedBlockUpdate> drain_replicated_block_updates();
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
  bool set_fluid_internal(
      BlockPos pos,
      voxel::FluidCell fluid,
      const voxel::FluidDefinition& definition,
      bool enqueue_updates) noexcept;
  void enqueue_fluid_update_unique(FluidUpdate update);
  [[nodiscard]] std::vector<FluidUpdate> drain_due_fluid_updates(u32 budget);
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
  [[nodiscard]] entt::entity entity_handle(EntityID entity_id) const noexcept;
  [[nodiscard]] ChunkCoord chunk_for_position(Vec3f position) const noexcept;
  void update_entity_chunk(entt::entity handle) noexcept;
  void record_replicated_block_update(BlockPos pos, voxel::BlockVariantID new_state);

  WorldID id_{};
  StableHashMap<ChunkCoord, voxel::ChunkSection> chunks_;
  entt::registry entity_registry_;
  StableHashMap<EntityID, entt::entity> entity_handles_;
  StableHashMap<PlayerID, EntityID> player_entities_;
  std::vector<EntityID> pending_entity_despawns_;
  u64 next_entity_id_{1};
  u32 next_entity_net_id_{1};
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
  HashMap<BlockPos, FluidUpdate> pending_fluid_updates_by_position_;
  HashMap<ChunkCoord, std::vector<FluidUpdate>> deferred_boundary_fluid_updates_;
  std::vector<ReplicatedBlockUpdate> replicated_block_updates_;
  std::deque<ChunkCoord> dirty_mesh_chunks_;
  HashSet<ChunkCoord> dirty_mesh_chunk_set_;
};

} // namespace arv::world
