#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/location.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/player_state.hpp"
#include "arv/voxel/storage.hpp"
#include "arv/world/fluid_solver.hpp"
#include "arv/world/regions.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_lighting.hpp"
#include "arv/world/world_time.hpp"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::world {

class EntityGoalRegistry;

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

struct ChunkWorkloadSnapshot {
  usize loaded_chunks{};
  usize active_chunks{};
  usize chunks_awaiting_generation{};
  usize chunk_sections_awaiting_lighting{};
  usize chunk_sections_awaiting_meshing{};
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
  std::optional<player::ItemStack> dropped_item_stack{};
  Seconds dropped_item_pickup_delay_seconds{};
  std::optional<entity::EntityComponentSet> authored_components{};
};

enum class DamageCause : u8 {
  Generic,
  EntityAttack,
};

struct DamageRequest {
  EntityID source{};
  EntityID target{};
  f32 amount{};
  DamageCause cause{DamageCause::Generic};
};

struct DamageResult {
  bool applied{};
  bool killed{};
  f32 previous_health{};
  f32 new_health{};
};

struct ReplicatedBlockUpdate {
  BlockPos pos{};
  voxel::BlockVariantID new_state{};
  TickIndex server_tick{};
};

struct WorldHeightSettings {
  i32 min_chunk_y{};
  i32 max_chunk_y{};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return min_chunk_y <= max_chunk_y;
  }

  [[nodiscard]] constexpr bool contains(i32 chunk_y) const noexcept {
    return chunk_y >= min_chunk_y && chunk_y <= max_chunk_y;
  }

  [[nodiscard]] constexpr usize section_count() const noexcept {
    return valid() ? static_cast<usize>(max_chunk_y - min_chunk_y + 1) : 0U;
  }

  [[nodiscard]] constexpr std::optional<usize> section_index(i32 chunk_y) const noexcept {
    return contains(chunk_y)
               ? std::optional<usize>{static_cast<usize>(chunk_y - min_chunk_y)}
               : std::nullopt;
  }
};

[[nodiscard]] constexpr WorldHeightSettings normalize_world_height_settings(
    WorldHeightSettings settings) noexcept {
  if (settings.min_chunk_y > settings.max_chunk_y) {
    settings.max_chunk_y = settings.min_chunk_y;
  }
  return settings;
}

[[nodiscard]] inline WorldHeightSettings world_height_settings_from_generation_config(
    const WorldGenerationConfig& config) noexcept {
  return normalize_world_height_settings(
      {floor_div(config.min_y, ChunkSize), floor_div(config.max_y, ChunkSize)});
}

class Chunk {
public:
  explicit Chunk(
      ChunkCoord coord = {},
      WorldHeightSettings height = {});

  [[nodiscard]] ChunkCoord coord() const noexcept {
    return coord_;
  }

  [[nodiscard]] WorldHeightSettings height_settings() const noexcept {
    return height_;
  }

  [[nodiscard]] std::vector<voxel::ChunkLifecycleState> configure_height(
      WorldHeightSettings settings);
  [[nodiscard]] voxel::ChunkSection* find_section(i32 chunk_y) noexcept;
  [[nodiscard]] const voxel::ChunkSection* find_section(i32 chunk_y) const noexcept;
  [[nodiscard]] voxel::ChunkSection* find_section(ChunkSectionCoord coord) noexcept;
  [[nodiscard]] const voxel::ChunkSection* find_section(ChunkSectionCoord coord) const noexcept;
  [[nodiscard]] voxel::ChunkSection& ensure_section(
      i32 chunk_y,
      voxel::ChunkLifecycleState state = voxel::ChunkLifecycleState::Loaded);
  [[nodiscard]] voxel::ChunkSection& ensure_section(
      ChunkSectionCoord coord,
      voxel::ChunkLifecycleState state = voxel::ChunkLifecycleState::Loaded);
  [[nodiscard]] bool unload_section(i32 chunk_y) noexcept;
  [[nodiscard]] usize loaded_section_count() const noexcept;
  [[nodiscard]] std::vector<ChunkSectionCoord> loaded_section_coords() const;
  void set_generation_surface_profile(GenerationSurfaceProfile profile);
  void clear_generation_surface_profile() noexcept;
  [[nodiscard]] const GenerationSurfaceProfile* generation_surface_profile() const noexcept;
  template<typename Fn>
  void for_each_loaded_section(Fn&& fn) {
    for (const auto& section : sections_) {
      if (section) {
        fn(*section);
      }
    }
  }
  template<typename Fn>
  void for_each_loaded_section(Fn&& fn) const {
    for (const auto& section : sections_) {
      if (section) {
        fn(*section);
      }
    }
  }

private:
  [[nodiscard]] std::optional<usize> section_index(i32 chunk_y) const noexcept {
    return height_.section_index(chunk_y);
  }

  ChunkCoord coord_{};
  WorldHeightSettings height_{};
  std::vector<std::unique_ptr<voxel::ChunkSection>> sections_{};
  std::optional<GenerationSurfaceProfile> generation_surface_profile_{};
};

class WorldRuntime {
public:
  explicit WorldRuntime(WorldID id);
  ~WorldRuntime();

  WorldRuntime(WorldRuntime&&) noexcept;
  WorldRuntime& operator=(WorldRuntime&&) noexcept;

  WorldRuntime(const WorldRuntime&) = delete;
  WorldRuntime& operator=(const WorldRuntime&) = delete;

  [[nodiscard]] WorldID id() const noexcept;
  void bind_owner_thread() noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;
  void assert_owner_thread() const noexcept;
  void configure_height(WorldHeightSettings settings);
  [[nodiscard]] WorldHeightSettings height_settings() const noexcept;
  [[nodiscard]] Chunk& ensure_chunk(ChunkCoord coord);
  [[nodiscard]] Chunk* find_chunk(ChunkCoord coord) noexcept;
  [[nodiscard]] const Chunk* find_chunk(ChunkCoord coord) const noexcept;
  [[nodiscard]] voxel::ChunkSection& ensure_chunk_section(
      ChunkSectionCoord coord,
      voxel::ChunkLifecycleState state = voxel::ChunkLifecycleState::Loaded);
  void request_chunk_section_load(ChunkSectionCoord coord);
  bool unload_chunk_section(ChunkSectionCoord coord) noexcept;
  [[nodiscard]] voxel::ChunkSection* find_chunk_section(ChunkSectionCoord coord) noexcept;
  [[nodiscard]] const voxel::ChunkSection* find_chunk_section(ChunkSectionCoord coord) const noexcept;
  voxel::ChunkSection& ensure_chunk(
      ChunkSectionCoord coord,
      voxel::ChunkLifecycleState state = voxel::ChunkLifecycleState::Loaded);
  void request_chunk_load(ChunkSectionCoord coord);
  [[nodiscard]] std::optional<voxel::ChunkSnapshot> get_chunk_snapshot(ChunkSectionCoord coord) const;
  void apply_chunk_snapshot(
      ChunkSectionCoord coord,
      std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
      std::optional<std::array<voxel::LightWord, voxel::ChunkVolume>> lights,
      std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids = std::nullopt,
      bool lighting_authoritative = true) noexcept;
  bool unload_chunk(ChunkSectionCoord coord) noexcept;
  [[nodiscard]] voxel::ChunkSection* find_chunk(ChunkSectionCoord coord) noexcept;
  [[nodiscard]] const voxel::ChunkSection* find_chunk(ChunkSectionCoord coord) const noexcept;
  [[nodiscard]] usize loaded_chunk_count() const noexcept;
  [[nodiscard]] ChunkWorkloadSnapshot chunk_workload_snapshot() const noexcept;
  [[nodiscard]] std::vector<ChunkSectionCoord> loaded_chunk_coords() const;
  [[nodiscard]] TickIndex tick_index() const noexcept;
  void set_tick_index(TickIndex tick_index) noexcept;
  [[nodiscard]] const WorldClock& clock() const noexcept;
  void configure_clock(
      Seconds day_length_seconds,
      f32 start_day_fraction,
      TickRate fixed_tick_rate,
      bool daylight_cycle = true,
      f32 time_scale = 1.0F) noexcept;
  void set_clock(WorldClock clock, TickRate fixed_tick_rate) noexcept;
  void set_day_time_tick(u32 day_time_tick) noexcept;
  void set_day_fraction(f32 day_fraction) noexcept;
  void set_daylight_cycle(bool daylight_cycle) noexcept;
  void set_time_scale(f32 time_scale) noexcept;
  void set_day_length_seconds(Seconds day_length_seconds, TickRate fixed_tick_rate) noexcept;
  [[nodiscard]] u64 lighting_revision() const noexcept;
  [[nodiscard]] u64 mesh_revision() const noexcept;
  [[nodiscard]] arv::Location spawn_point() const noexcept;
  void set_spawn_point(arv::Location spawn) noexcept;
  void mark_chunk_lighting_dirty(ChunkSectionCoord coord) noexcept;
  void set_generation_context(u64 seed, WorldGenerationConfig config);
  [[nodiscard]] u64 generation_seed() const noexcept;
  [[nodiscard]] const WorldGenerationConfig* generation_config() const noexcept;
  void clear_generation_context() noexcept;
  void set_tick_policy(WorldTickPolicy policy) noexcept;
  [[nodiscard]] WorldTickPolicy tick_policy() const noexcept;
  [[nodiscard]] bool is_chunk_active(ChunkSectionCoord coord) const noexcept;
  [[nodiscard]] usize active_chunk_count() const noexcept;
  bool set_chunk_lifecycle_state(ChunkSectionCoord coord, voxel::ChunkLifecycleState state) noexcept;
  void update_simulation_radius(ChunkSectionCoord center, i32 radius);
  void update_simulation_radii(std::span<const ChunkSectionCoord> centers, i32 radius);
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
      ChunkSectionCoord coord,
      MissingNeighborPolicy missing_neighbor_policy = MissingNeighborPolicy::RequireLoaded) const;
  [[nodiscard]] bool validate_mesh_result(ChunkSectionCoord coord, u64 source_version) const noexcept;
  [[nodiscard]] bool validate_mesh_content_result(
      ChunkSectionCoord coord,
      u64 source_content_version) const noexcept;
  bool publish_mesh_result(
      ChunkSectionCoord coord,
      u64 source_block_version,
      u64 source_light_version,
      u64 source_content_version = 0U) noexcept;
  bool publish_generated_chunk(GeneratedChunk generated) noexcept;
  void restore_saved_chunk(
      ChunkSectionCoord coord,
      std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
      std::array<voxel::LightWord, voxel::ChunkVolume> lights,
      std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids,
      bool lighting_authoritative) noexcept;
  [[nodiscard]] EntityID create_entity(EntityCreateInfo info = {});
  [[nodiscard]] EntityID create_dropped_item(
      player::ItemStack stack,
      Vec3f position,
      Vec3f velocity = {},
      Seconds pickup_delay_seconds = 0.0F,
      bool persistent = true);
  bool destroy_entity(EntityID entity_id, EntityDespawnReason reason = EntityDespawnReason::Removed);
  void flush_entity_despawns();
  [[nodiscard]] bool entity_exists(EntityID entity_id) const noexcept;
  [[nodiscard]] usize entity_count() const noexcept;
  [[nodiscard]] std::vector<EntityID> entity_ids() const;
  [[nodiscard]] std::optional<EntityID> entity_for_player(PlayerID player_id) const noexcept;
  [[nodiscard]] std::optional<PlayerID> player_for_entity(EntityID entity_id) const noexcept;
  [[nodiscard]] std::optional<EntityNetID> network_id_for_entity(EntityID entity_id) const noexcept;
  [[nodiscard]] std::optional<ChunkSectionCoord> entity_chunk(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityTransform* find_entity_transform(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityTransform* find_entity_transform(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityVelocity* find_entity_velocity(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityVelocity* find_entity_velocity(EntityID entity_id) const noexcept;
  [[nodiscard]] const entity::EntityType* find_entity_type(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityCollision* find_entity_collision(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityCollision* find_entity_collision(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityMovement* find_entity_movement(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityMovement* find_entity_movement(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityHealth* find_entity_health(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityHealth* find_entity_health(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::KnockbackResistanceComponent* find_entity_knockback_resistance(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::KnockbackResistanceComponent* find_entity_knockback_resistance(EntityID entity_id) const noexcept;
  [[nodiscard]] DamageResult apply_damage(const DamageRequest& request) noexcept;
  [[nodiscard]] entity::BrainComponent* find_entity_brain(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::BrainComponent* find_entity_brain(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::MovementIntent* find_entity_movement_intent(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::MovementIntent* find_entity_movement_intent(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::EntityAnimationState* find_entity_animation_state(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::EntityAnimationState* find_entity_animation_state(EntityID entity_id) const noexcept;
  [[nodiscard]] entity::DroppedItem* find_dropped_item(EntityID entity_id) noexcept;
  [[nodiscard]] const entity::DroppedItem* find_dropped_item(EntityID entity_id) const noexcept;
  bool set_entity_animation(EntityID entity_id, std::string animation_name) noexcept;
  bool play_entity_animation(EntityID entity_id, std::string animation_name) noexcept;
  bool set_player_visual_pose(
      PlayerID player_id,
      f32 look_yaw_radians,
      f32 look_pitch_radians,
      f32 max_head_body_yaw_radians) noexcept;
  bool set_dropped_item_stack(EntityID entity_id, player::ItemStack stack) noexcept;
  [[nodiscard]] bool entity_is_persistent(EntityID entity_id) const noexcept;
  [[nodiscard]] std::optional<std::string> persistent_entity_uuid(
      EntityID entity_id) const;
  bool add_entity_tag(EntityID entity_id, std::string tag);
  bool remove_entity_tag(EntityID entity_id, std::string_view tag) noexcept;
  [[nodiscard]] bool entity_has_tag(
      EntityID entity_id,
      std::string_view tag) const noexcept;
  bool set_entity_transform(EntityID entity_id, Vec3f position, Quatf rotation) noexcept;
  bool set_entity_position(EntityID entity_id, Vec3f position) noexcept;
  [[nodiscard]] std::vector<entity::EntitySnapshotEntry> collect_entity_snapshots() const;
  [[nodiscard]] std::vector<entity::DroppedItemEntitySnapshot> collect_dropped_item_snapshots() const;
  [[nodiscard]] std::vector<entity::PersistentEntityRecord> persistent_entity_records() const;
  void restore_persistent_entities(
      std::span<const entity::PersistentEntityRecord> records,
      const entity::EntityRegistry* entity_registry = nullptr);
  u32 tick_entities(
      const voxel::BlockRegistry* block_registry = nullptr,
      const EntityGoalRegistry* goal_registry = nullptr);
  u32 tick_dropped_items(
      Seconds tick_seconds,
      const voxel::BlockRegistry* block_registry = nullptr,
      const player::ItemRegistry* item_registry = nullptr);
  player::PlayerState& ensure_player(PlayerID player_id);
  [[nodiscard]] player::PlayerState* find_player(PlayerID player_id) noexcept;
  [[nodiscard]] const player::PlayerState* find_player(PlayerID player_id) const noexcept;
  [[nodiscard]] usize player_count() const noexcept;
  [[nodiscard]] std::vector<PlayerID> player_ids() const;
  [[nodiscard]] const RegionRegistry& regions() const noexcept;
  [[nodiscard]] RegionRegistry& regions() noexcept;
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
  [[nodiscard]] usize deferred_boundary_fluid_event_count(ChunkSectionCoord coord) const noexcept;
  [[nodiscard]] usize total_deferred_boundary_fluid_event_count() const noexcept;
  [[nodiscard]] usize pending_block_update_count() const noexcept;
  [[nodiscard]] bool has_lighting_work() const noexcept;
  [[nodiscard]] LightingSnapshot make_lighting_snapshot() const;
  [[nodiscard]] LightingBatchSnapshot make_lighting_batch_snapshot(u32 max_sections) const;
  [[nodiscard]] static LightingResult build_lighting_result(
      const LightingSnapshot& snapshot,
      const voxel::BlockRegistry& registry);
  [[nodiscard]] static LightingResult build_lighting_result(
      const LightingBatchSnapshot& snapshot,
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
      const voxel::FluidDefinition& fluid_definition,
      const EntityGoalRegistry* goal_registry = nullptr);
  [[nodiscard]] std::vector<ReplicatedBlockUpdate> drain_replicated_block_updates();
  [[nodiscard]] std::vector<ChunkSectionCoord> dirty_chunks(voxel::ChunkDirtyFlags flag) const;
  [[nodiscard]] std::vector<ChunkSectionCoord> consume_dirty_mesh_chunks(
      voxel::ChunkDirtyFlags flag);

private:
  friend class FluidSolver;
  friend class WorldLighting;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Short-lived cursor for repeated nearby lookups. Recreate or reset it after
// chunk load/unload operations so the cached pointer cannot outlive a section.
class WorldSearch final {
public:
  explicit WorldSearch(WorldRuntime& world) noexcept
      : mutable_world_(&world),
        world_(&world) {
  }

  explicit WorldSearch(const WorldRuntime& world) noexcept
      : world_(&world) {
  }

  void reset() const noexcept {
    cached_coord_.reset();
    cached_chunk_ = nullptr;
    cached_mutable_chunk_ = nullptr;
  }

  [[nodiscard]] const voxel::ChunkSection* find_chunk(ChunkSectionCoord coord) const noexcept {
    if (cached_coord_.has_value() && cached_coord_.value() == coord) {
      return cached_chunk_;
    }

    cached_coord_ = coord;
    cached_chunk_ = world_->find_chunk(coord);
    cached_mutable_chunk_ = nullptr;
    return cached_chunk_;
  }

  [[nodiscard]] voxel::ChunkSection* find_mutable_chunk(ChunkSectionCoord coord) noexcept {
    if (mutable_world_ == nullptr) {
      return nullptr;
    }
    if (cached_coord_.has_value() && cached_coord_.value() == coord &&
        (cached_mutable_chunk_ != nullptr || cached_chunk_ == nullptr)) {
      return cached_mutable_chunk_;
    }

    cached_coord_ = coord;
    voxel::ChunkSection* chunk = mutable_world_->find_chunk(coord);
    cached_chunk_ = chunk;
    cached_mutable_chunk_ = chunk;
    return chunk;
  }

  [[nodiscard]] bool is_chunk_loaded(BlockPos pos) const noexcept {
    return find_chunk(block_to_chunk(pos)) != nullptr;
  }

  [[nodiscard]] bool is_chunk_active(ChunkSectionCoord coord) const noexcept {
    const voxel::ChunkSection* chunk = find_chunk(coord);
    return chunk != nullptr && chunk->lifecycle_state() == voxel::ChunkLifecycleState::Active;
  }

  [[nodiscard]] voxel::BlockVariantID block_at(BlockPos pos) const noexcept {
    const voxel::ChunkSection* chunk = find_chunk(block_to_chunk(pos));
    return chunk == nullptr ? voxel::AirBlockVariantID : chunk->block(block_to_local(pos));
  }

  [[nodiscard]] voxel::LightWord light_at(BlockPos pos) const noexcept {
    const voxel::ChunkSection* chunk = find_chunk(block_to_chunk(pos));
    return chunk == nullptr ? voxel::NoLight : chunk->light(block_to_local(pos));
  }

  [[nodiscard]] voxel::FluidCell fluid_at(BlockPos pos) const noexcept {
    const voxel::ChunkSection* chunk = find_chunk(block_to_chunk(pos));
    return chunk == nullptr ? voxel::empty_fluid_cell() : chunk->fluid(block_to_local(pos));
  }

private:
  WorldRuntime* mutable_world_{};
  const WorldRuntime* world_{};
  mutable std::optional<ChunkSectionCoord> cached_coord_{};
  mutable const voxel::ChunkSection* cached_chunk_{};
  mutable voxel::ChunkSection* cached_mutable_chunk_{};
};

} // namespace arv::world
