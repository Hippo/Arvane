#pragma once

#include "arv/world/world_runtime.hpp"

#include <entt/entt.hpp>

#include <array>
#include <deque>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace arv::world::detail {

inline constexpr std::array<voxel::BlockFace, voxel::BlockFaceCount> AllFaces{
    voxel::BlockFace::PositiveX,
    voxel::BlockFace::NegativeX,
    voxel::BlockFace::PositiveY,
    voxel::BlockFace::NegativeY,
    voxel::BlockFace::PositiveZ,
    voxel::BlockFace::NegativeZ};

inline constexpr std::array<Vec3i, voxel::BlockFaceCount> NeighborOffsets{{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
}};

[[nodiscard]] i32 abs_i32(i32 value) noexcept;
[[nodiscard]] i32 max_abs3(i32 x, i32 y, i32 z) noexcept;
[[nodiscard]] BlockPos offset_block(BlockPos pos, Vec3i offset) noexcept;
[[nodiscard]] ChunkSectionCoord chunk_for_position(Vec3f position) noexcept;
[[nodiscard]] usize lifecycle_index(voxel::ChunkLifecycleState state) noexcept;

class WorldDirtyTracker;
class WorldRegionRuntime;

class WorldChunkStore {
public:
  void configure_height(WorldHeightSettings settings);
  [[nodiscard]] WorldHeightSettings height_settings() const noexcept;
  [[nodiscard]] Chunk& ensure_column(ChunkCoord coord);
  [[nodiscard]] Chunk* find_column(ChunkCoord coord) noexcept;
  [[nodiscard]] const Chunk* find_column(ChunkCoord coord) const noexcept;
  [[nodiscard]] voxel::ChunkSection& ensure_chunk(
      ChunkSectionCoord coord,
      voxel::ChunkLifecycleState state,
      WorldDirtyTracker& dirty);
  void request_chunk_load(ChunkSectionCoord coord, WorldDirtyTracker& dirty);
  bool unload_chunk(ChunkSectionCoord coord) noexcept;

  [[nodiscard]] voxel::ChunkSection* find_chunk(ChunkSectionCoord coord) noexcept;
  [[nodiscard]] const voxel::ChunkSection* find_chunk(ChunkSectionCoord coord) const noexcept;

  [[nodiscard]] usize loaded_count() const noexcept;
  [[nodiscard]] usize active_count() const noexcept;
  [[nodiscard]] bool is_active(ChunkSectionCoord coord) const noexcept;
  [[nodiscard]] usize lifecycle_count(voxel::ChunkLifecycleState state) const noexcept;
  [[nodiscard]] std::vector<ChunkSectionCoord> loaded_coords() const;
  [[nodiscard]] ChunkWorkloadSnapshot workload_snapshot(
      bool lighting_work_pending,
      usize last_lighting_snapshot_chunk_count,
      usize dirty_mesh_chunk_count) const noexcept;

  bool set_lifecycle_state(ChunkSectionCoord coord, voxel::ChunkLifecycleState state) noexcept;
  void set_lifecycle_state_internal(
      voxel::ChunkSection& chunk,
      voxel::ChunkLifecycleState state) noexcept;
  void update_simulation_radius(WorldRuntime& world, ChunkSectionCoord center, i32 radius);
  void update_simulation_radii(
      WorldRuntime& world,
      std::span<const ChunkSectionCoord> centers,
      i32 radius);

  template<typename Fn>
  void for_each_loaded_section(Fn&& fn) {
    for (auto& [coord, chunk] : chunks_) {
      (void)coord;
      chunk.for_each_loaded_section(fn);
    }
  }

  template<typename Fn>
  void for_each_loaded_section(Fn&& fn) const {
    for (const auto& [coord, chunk] : chunks_) {
      (void)coord;
      chunk.for_each_loaded_section(fn);
    }
  }

  [[nodiscard]] StableHashMap<ChunkCoord, Chunk>& chunks() noexcept {
    return chunks_;
  }

  [[nodiscard]] const StableHashMap<ChunkCoord, Chunk>& chunks() const noexcept {
    return chunks_;
  }

private:
  void note_simulation_topology_changed() noexcept;

  WorldHeightSettings height_{};
  StableHashMap<ChunkCoord, Chunk> chunks_;
  std::array<usize, 9> lifecycle_counts_{};
  usize active_chunk_count_{};
  u64 simulation_topology_revision_{};
  std::vector<ChunkSectionCoord> cached_simulation_centers_;
  i32 cached_simulation_radius_{};
  u64 cached_simulation_topology_revision_{};
  bool has_cached_simulation_activity_{};
};

class WorldDirtyTracker {
public:
  [[nodiscard]] u64 lighting_revision() const noexcept;
  [[nodiscard]] u64 mesh_revision() const noexcept;

  void mark_lighting_content_dirty() noexcept;
  void mark_mesh_content_dirty() noexcept;

  [[nodiscard]] bool has_lighting_work() const noexcept;
  void recompute_lighting_work_pending(const WorldRuntime& world);
  [[nodiscard]] usize last_lighting_snapshot_chunk_count() const noexcept;
  void set_last_lighting_snapshot_chunk_count(usize count) const noexcept;
  [[nodiscard]] std::vector<ChunkSectionCoord> dirty_lighting_chunks(
      const WorldRuntime& world);
  [[nodiscard]] usize dirty_lighting_chunk_count() const noexcept;
  void mark_chunk_lighting_dirty(WorldRuntime& world, ChunkSectionCoord coord) noexcept;
  void forget_lighting_chunk(ChunkSectionCoord coord) noexcept;

  void enqueue_dirty_mesh_chunk(ChunkSectionCoord coord);
  [[nodiscard]] usize dirty_mesh_chunk_count() const noexcept;

  [[nodiscard]] std::vector<ChunkSectionCoord> dirty_chunks(
      const WorldRuntime& world,
      voxel::ChunkDirtyFlags flag) const;
  [[nodiscard]] std::vector<ChunkSectionCoord> consume_dirty_mesh_chunks(
      const WorldRuntime& world,
      voxel::ChunkDirtyFlags flag);

  void mark_boundary_neighbors_dirty(
      WorldRuntime& world,
      ChunkSectionCoord coord,
      SectionLocalPos local) noexcept;
  void mark_loaded_chunk_neighbors_dirty(WorldRuntime& world, ChunkSectionCoord coord) noexcept;
  void mark_loaded_chunk_lighting_dependencies_dirty(
      WorldRuntime& world,
      ChunkSectionCoord coord) noexcept;
  void mark_light_dependency_neighbors_dirty(WorldRuntime& world, ChunkSectionCoord coord) noexcept;
  void mark_neighbor_mesh_dependency_dirty(WorldRuntime& world, ChunkSectionCoord coord) noexcept;
  void mark_neighbor_mesh_only_dependency_dirty(WorldRuntime& world, ChunkSectionCoord coord) noexcept;
  void mark_neighbor_light_dependency_dirty(WorldRuntime& world, ChunkSectionCoord coord) noexcept;
  void mark_neighbor_lighting_dependency_dirty(WorldRuntime& world, ChunkSectionCoord coord) noexcept;

private:
  u64 lighting_revision_{1};
  u64 mesh_revision_{1};
  bool lighting_work_pending_{};
  mutable usize last_lighting_snapshot_chunk_count_{};
  HashSet<ChunkSectionCoord> dirty_lighting_chunks_;
  std::deque<ChunkSectionCoord> dirty_mesh_chunks_;
  HashSet<ChunkSectionCoord> dirty_mesh_chunk_set_;
};

class WorldTickQueues {
public:
  void set_policy(WorldTickPolicy policy) noexcept;
  [[nodiscard]] WorldTickPolicy policy() const noexcept;

  void schedule_block_update(
      BlockPos pos,
      TickIndex current_tick,
      TickIndex delay_ticks,
      BlockUpdateReason reason);
  void enqueue_block_update(BlockPos pos, BlockUpdateReason reason);
  void enqueue_neighbor_block_updates(BlockPos pos);

  [[nodiscard]] u32 process_due_scheduled_ticks(TickIndex current_tick);
  [[nodiscard]] u32 drain_block_updates(u32 budget);
  [[nodiscard]] usize pending_block_update_count() const noexcept;

private:
  WorldTickPolicy tick_policy_{};
  std::vector<ScheduledBlockUpdate> scheduled_block_updates_;
  std::deque<BlockUpdate> block_updates_;
};

class WorldFluidRuntime {
public:
  void enqueue_update(
      BlockPos pos,
      TickIndex current_tick,
      FluidUpdateReason reason,
      u32 delay_ticks);
  void enqueue_update_unique(FluidUpdate update);

  [[nodiscard]] std::vector<FluidUpdate> drain_due_updates(
      TickIndex current_tick,
      u32 budget);

  void store_deferred_boundary_update(ChunkSectionCoord target, FluidUpdate update);
  void enqueue_chunk_activation_updates(WorldRuntime& world, ChunkSectionCoord coord);

  [[nodiscard]] usize pending_update_count() const noexcept;
  [[nodiscard]] usize deferred_boundary_event_count(ChunkSectionCoord coord) const noexcept;
  [[nodiscard]] usize total_deferred_boundary_event_count() const noexcept;

  [[nodiscard]] u32 process_updates(
      WorldRuntime& world,
      const voxel::BlockRegistry& registry,
      const voxel::FluidDefinition& definition,
      u32 budget);
  [[nodiscard]] std::optional<FluidSimulationRequest> make_simulation_request(
      const WorldChunkStore& chunks,
      TickIndex current_tick,
      u32 budget);

private:
  std::deque<FluidUpdate> fluid_updates_;
  HashMap<BlockPos, FluidUpdate> pending_fluid_updates_by_position_;
  HashMap<ChunkSectionCoord, std::vector<FluidUpdate>> deferred_boundary_fluid_updates_;
};

class WorldLightingRuntime {
public:
  [[nodiscard]] bool has_work(const WorldRuntime& world) const noexcept;
  [[nodiscard]] LightingSnapshot make_snapshot(const WorldRuntime& world) const;
  [[nodiscard]] static LightingResult build_result(
      const LightingSnapshot& snapshot,
      const voxel::BlockRegistry& registry);
  LightingPublishStats publish_result(WorldRuntime& world, LightingResult result) noexcept;
};

void apply_authored_components(
    entt::registry& registry,
    entt::entity handle,
    const entity::EntityComponentSet& components);

class WorldEntityStore {
public:
  explicit WorldEntityStore(WorldID world_id);

  [[nodiscard]] EntityID create_entity(EntityCreateInfo info);
  [[nodiscard]] EntityID create_dropped_item(
      player::ItemStack stack,
      Vec3f position,
      Vec3f velocity,
      Seconds pickup_delay_seconds,
      bool persistent);

  bool destroy_entity(EntityID entity_id, EntityDespawnReason reason);
  void flush_despawns(WorldRegionRuntime& regions);

  [[nodiscard]] bool exists(EntityID entity_id) const noexcept;
  [[nodiscard]] usize count() const noexcept;
  [[nodiscard]] std::vector<EntityID> ids() const;

  [[nodiscard]] std::optional<EntityID> entity_for_player(PlayerID player_id) const noexcept;
  [[nodiscard]] std::optional<PlayerID> player_for_entity(EntityID entity_id) const noexcept;
  [[nodiscard]] std::optional<EntityNetID> network_id_for_entity(EntityID entity_id) const noexcept;
  [[nodiscard]] std::optional<ChunkSectionCoord> entity_chunk(EntityID entity_id) const noexcept;
  [[nodiscard]] usize player_count() const noexcept;
  [[nodiscard]] std::vector<PlayerID> player_ids() const;

  [[nodiscard]] entity::EntityTransform* find_transform(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityTransform* find_transform(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityVelocity* find_velocity(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityVelocity* find_velocity(EntityID entity_id) const noexcept;
  [[nodiscard]] const entity::EntityType* find_type(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityCollision* find_collision(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityCollision* find_collision(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityMovement* find_movement(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityMovement* find_movement(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityHealth* find_health(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityHealth* find_health(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::KnockbackResistanceComponent* find_knockback_resistance(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::KnockbackResistanceComponent* find_knockback_resistance(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::BrainComponent* find_brain(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::BrainComponent* find_brain(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::MovementIntent* find_movement_intent(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::MovementIntent* find_movement_intent(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityAnimationState* find_animation_state(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityAnimationState* find_animation_state(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::DroppedItem* find_dropped_item(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::DroppedItem* find_dropped_item(EntityID entity_id) const noexcept;

  bool set_animation(EntityID entity_id, std::string animation_name) noexcept;
  bool play_animation(EntityID entity_id, std::string animation_name) noexcept;
  bool set_player_visual_pose(
      PlayerID player_id,
      f32 look_yaw_radians,
      f32 look_pitch_radians,
      f32 max_head_body_yaw_radians) noexcept;
  bool set_dropped_item_stack(EntityID entity_id, player::ItemStack stack) noexcept;
  [[nodiscard]] bool is_persistent(EntityID entity_id) const noexcept;
  [[nodiscard]] std::optional<std::string> persistent_uuid(EntityID entity_id) const;
  bool add_tag(EntityID entity_id, std::string tag);
  bool remove_tag(EntityID entity_id, std::string_view tag) noexcept;
  [[nodiscard]] bool has_tag(EntityID entity_id, std::string_view tag) const noexcept;
  bool set_transform(EntityID entity_id, Vec3f position, Quatf rotation) noexcept;
  bool set_position(EntityID entity_id, Vec3f position) noexcept;

  [[nodiscard]] std::vector<entity::EntitySnapshotEntry> collect_entity_snapshots() const;
  [[nodiscard]] std::vector<entity::DroppedItemEntitySnapshot> collect_dropped_item_snapshots() const;
  [[nodiscard]] std::vector<entity::PersistentEntityRecord> persistent_records() const;
  void restore_persistent_entities(
      std::span<const entity::PersistentEntityRecord> records,
      const entity::EntityRegistry* entity_registry);

  [[nodiscard]] entt::entity handle(EntityID entity_id) const noexcept;
  void update_chunk(entt::entity handle) noexcept;

  [[nodiscard]] entt::registry& registry() noexcept {
    return registry_;
  }

  [[nodiscard]] const entt::registry& registry() const noexcept {
    return registry_;
  }

private:
  WorldID world_id_{};
  entt::registry registry_;
  StableHashMap<EntityID, entt::entity> handles_;
  StableHashMap<PlayerID, EntityID> player_entities_;
  std::vector<EntityID> pending_despawns_;
  u64 next_entity_id_{1};
  u32 next_entity_net_id_{1};
};

class WorldEntitySimulation {
public:
  [[nodiscard]] u32 tick_entities(
      WorldRuntime& world,
      WorldEntityStore& entities,
      WorldRegionRuntime& regions,
      const voxel::BlockRegistry* block_registry,
      const EntityGoalRegistry* goal_registry);
  [[nodiscard]] u32 tick_dropped_items(
      WorldRuntime& world,
      WorldEntityStore& entities,
      Seconds tick_seconds,
      const voxel::BlockRegistry* block_registry,
      const player::ItemRegistry* item_registry);

private:
  [[nodiscard]] u32 tick_brains(
      WorldRuntime& world,
      WorldEntityStore& entities,
      const EntityGoalRegistry* goal_registry);
  [[nodiscard]] u32 tick_entity_physics(
      WorldRuntime& world,
      WorldEntityStore& entities,
      const voxel::BlockRegistry* block_registry,
      const EntityGoalRegistry* goal_registry,
      Seconds tick_seconds);
  [[nodiscard]] u32 tick_entity_animations(WorldEntityStore& entities, Seconds tick_seconds);
};

class WorldPlayerStore {
public:
  [[nodiscard]] player::PlayerState& ensure_player(
      WorldEntityStore& entities,
      WorldID world_id,
      PlayerID player_id);
  [[nodiscard]] player::PlayerState* find_player(
      WorldEntityStore& entities,
      PlayerID player_id) noexcept;
  [[nodiscard]] const player::PlayerState* find_player(
      const WorldEntityStore& entities,
      PlayerID player_id) noexcept;
  [[nodiscard]] usize player_count(const WorldEntityStore& entities) const noexcept;
  [[nodiscard]] std::vector<PlayerID> player_ids(const WorldEntityStore& entities) const;
};

class WorldRegionRuntime {
public:
  [[nodiscard]] const RegionRegistry& regions() const noexcept;
  [[nodiscard]] RegionRegistry& regions() noexcept;

  [[nodiscard]] const PlayerRegionState* player_region_state(PlayerID player_id) const noexcept;
  RegionMembershipChange update_player_region_state(PlayerID player_id, RegionQuery query);
  void erase_player(PlayerID player_id) noexcept;

private:
  RegionRegistry regions_{};
  StableHashMap<PlayerID, PlayerRegionState> player_region_states_;
};

} // namespace arv::world::detail

namespace arv::world {

struct WorldRuntime::Impl {
  explicit Impl(WorldID world_id) : id(world_id), entities(world_id) {
  }

  bool set_fluid_cell(
      WorldRuntime& world,
      BlockPos pos,
      voxel::FluidCell fluid,
      const voxel::FluidDefinition& definition,
      bool enqueue_updates) noexcept;

  WorldID id{};
  std::thread::id owner_thread_id{std::this_thread::get_id()};
  detail::WorldChunkStore chunks;
  detail::WorldDirtyTracker dirty;
  detail::WorldTickQueues tick_queues;
  detail::WorldFluidRuntime fluid;
  detail::WorldLightingRuntime lighting;
  detail::WorldEntityStore entities;
  detail::WorldEntitySimulation entity_simulation;
  detail::WorldPlayerStore players;
  detail::WorldRegionRuntime regions;
  TickIndex tick_index{};
  WorldClock clock{make_world_clock()};
  std::optional<WorldGenerationConfig> generation_config{};
  u64 generation_seed{};
  std::vector<ReplicatedBlockUpdate> replicated_block_updates;
};

} // namespace arv::world
