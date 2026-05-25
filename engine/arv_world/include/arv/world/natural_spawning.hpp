#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/natural_spawn_types.hpp"
#include "arv/world/world_runtime.hpp"
#include "arv/world/worldgen/registry.hpp"
#include "arv/world/worldgen/terrain.hpp"

#include <array>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::world {

struct SpawnConditions {
  std::optional<i32> min_y{};
  std::optional<i32> max_y{};
  std::optional<u8> min_light{};
  std::optional<u8> max_light{};
  bool requires_surface{};
  bool requires_sky_access{};
  bool requires_water{};
  bool requires_air_body{true};
  bool requires_air_head{true};
  std::vector<std::string> block_tags_below{};
  std::vector<std::string> block_tags_inside{};
  std::vector<std::string> block_tags_head{};
  u32 min_distance_from_player_blocks{24};
  u32 max_distance_from_player_blocks{96};
  std::optional<TimePredicate> time{};
};

struct SpawnEntry {
  EntityTypeID entity_type{};
  std::string entity_type_name{};
  SpawnCategory category{SpawnCategory::Passive};
  u32 weight{};
  u8 min_group{1};
  u8 max_group{1};
  SpawnConditions conditions{};
  bool persistent_by_default{};
  bool replicated_by_default{true};
  entity::EntityComponentSet authored_components{};
};

struct SpawnDiagnostic {
  std::string biome_id{};
  std::string field{};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

struct BiomeSpawnTable {
  std::vector<SpawnEntry> entries{};
  u32 total_weight{};

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const SpawnEntry* weighted_pick(u32 roll) const noexcept;
};

class SpawnRuleRegistry {
public:
  [[nodiscard]] static SpawnRuleRegistry build_from_dimension(
      const worldgen::ResolvedDimension& dimension,
      const entity::EntityRegistry& entity_registry,
      const voxel::BlockRegistry& block_registry);

  [[nodiscard]] const BiomeSpawnTable* find_table(
      std::string_view biome_id,
      SpawnCategory category) const noexcept;

  [[nodiscard]] std::span<const SpawnDiagnostic> diagnostics() const noexcept {
    return diagnostics_;
  }

  [[nodiscard]] usize entry_count() const noexcept;

private:
  struct BiomeTables {
    std::string biome_id{};
    std::array<BiomeSpawnTable, SpawnCategoryCount> categories{};
  };

  std::vector<BiomeTables> tables_{};
  std::vector<SpawnDiagnostic> diagnostics_{};
};

struct NaturalSpawnCounters {
  u64 attempts_total{};
  u64 successes_total{};
  u64 rejected_chunk_not_loaded{};
  u64 rejected_chunk_not_simulated{};
  u64 rejected_no_biome_table{};
  u64 rejected_category_cap{};
  u64 rejected_world_cap{};
  u64 rejected_local_density{};
  u64 rejected_conditions{};
  u64 rejected_group_failed{};
  u64 rejected_no_generation_context{};
  u64 rejected_entity_creation_failed{};
  u64 despawned_natural_hostile{};
  u64 despawned_natural_ambient{};
  u64 despawned_natural_water{};
  u64 despawned_natural_other{};
  u32 attempts_last_interval{};
  u32 successes_last_interval{};
  u64 avg_spawn_tick_cost_us{};
  u64 max_spawn_tick_cost_us{};
  u64 spawn_jobs_submitted{};
  u64 spawn_jobs_completed{};
  u64 spawn_jobs_stale{};
  u32 spawn_jobs_pending{};
  u64 spawn_intents_generated{};
  u64 spawn_intents_committed{};
  u64 despawn_intents_generated{};
  u64 despawn_intents_committed{};
  u64 avg_spawn_prepare_cost_us{};
  u64 max_spawn_prepare_cost_us{};
  u64 avg_spawn_worker_cost_us{};
  u64 max_spawn_worker_cost_us{};
  u64 avg_spawn_apply_cost_us{};
  u64 max_spawn_apply_cost_us{};
  std::array<u32, SpawnCategoryCount> natural_entities_by_category{};
  std::array<u32, SpawnCategoryCount> total_entities_by_category{};
};

struct SpawnEvaluationContext {
  std::vector<Vec3f> player_positions{};
  const voxel::BlockRegistry* block_registry{};
  bool allow_loaded_candidate_sections_near_players{};
  u32 loaded_candidate_section_max_distance_blocks{};
};

class SpawnConditionEvaluator {
public:
  [[nodiscard]] bool passes(
      const WorldRuntime& world,
      const SpawnEntry& entry,
      BlockPos candidate,
      SpawnCategory category,
      const SpawnEvaluationContext& context) const;
};

struct SpawnLoadSnapshot {
  u32 total_world_entities{};
  std::array<u32, SpawnCategoryCount> natural_by_category{};
  std::array<u32, SpawnCategoryCount> total_by_category{};
};

struct NaturalSpawnPlayerSnapshot {
  PlayerID id{};
  Vec3f position{};
};

struct NaturalSpawnEntitySnapshot {
  EntityID id{};
  SpawnCategory category{SpawnCategory::Passive};
  EntityTypeID source_entity_type{};
  Vec3f position{};
  TickIndex spawn_tick{};
  bool transient{true};
  bool persistent{};
  bool player_owned{};
};

struct NaturalSpawnIntent {
  SpawnEntry entry{};
  std::vector<BlockPos> positions{};
};

struct NaturalDespawnIntent {
  EntityID entity{};
  SpawnCategory category{SpawnCategory::Passive};
  EntityTypeID source_entity_type{};
  TickIndex spawn_tick{};
};

struct NaturalSpawnJobRequest {
  WorldID world_id{};
  TickIndex tick{};
  NaturalSpawningConfig config{};
  SpawnRuleRegistry rules{};
  std::shared_ptr<const worldgen::ResolvedDimension> dimension{};
  u64 generation_seed{};
  WorldClock clock{};
  u64 random_seed{};
  std::vector<NaturalSpawnPlayerSnapshot> players{};
  std::vector<NaturalSpawnEntitySnapshot> natural_entities{};
  std::vector<ChunkSectionCoord> loaded_chunks{};
  std::vector<ChunkSectionCoord> active_chunks{};
  SpawnLoadSnapshot load{};
};

struct NaturalSpawnJobResult {
  WorldID world_id{};
  TickIndex source_tick{};
  bool canceled{};
  NaturalSpawnCounters counters{};
  std::vector<NaturalSpawnIntent> spawn_intents{};
  std::vector<NaturalDespawnIntent> despawn_intents{};
  u64 worker_duration_us{};
};

struct NaturalSpawnApplyStats {
  u32 spawned{};
  u32 despawned{};
  bool stale{};
  u64 apply_duration_us{};
};

class SpawnLoadTracker {
public:
  void refresh(const WorldRuntime& world);

  [[nodiscard]] const SpawnLoadSnapshot& snapshot() const noexcept {
    return snapshot_;
  }

  [[nodiscard]] bool has_world_budget(
      SpawnCategory category,
      usize player_count,
      const NaturalSpawningConfig& config) const noexcept;
  [[nodiscard]] bool has_category_budget(
      SpawnCategory category,
      usize player_count,
      const NaturalSpawningConfig& config) const noexcept;
  [[nodiscard]] bool has_local_budget(
      const WorldRuntime& world,
      BlockPos pos,
      SpawnCategory category,
      EntityTypeID type,
      const NaturalSpawningConfig& config) const noexcept;

private:
  SpawnLoadSnapshot snapshot_{};
};

class NaturalSpawnSystem {
public:
  void configure(NaturalSpawningConfig config);
  void set_rules(SpawnRuleRegistry rules);

  [[nodiscard]] const NaturalSpawningConfig& config() const noexcept {
    return config_;
  }

  [[nodiscard]] const SpawnRuleRegistry& rules() const noexcept {
    return rules_;
  }

  [[nodiscard]] const NaturalSpawnCounters& counters() const noexcept {
    return counters_;
  }

  [[nodiscard]] NaturalSpawnCounters& counters() noexcept {
    return counters_;
  }

  void tick(
      WorldRuntime& world,
      const voxel::BlockRegistry& block_registry,
      const entity::EntityRegistry& entity_registry,
      TickIndex tick);

  [[nodiscard]] std::optional<NaturalSpawnJobRequest> make_job_request(
      const WorldRuntime& world,
      TickIndex tick);
  [[nodiscard]] static NaturalSpawnJobResult build_job_result(
      NaturalSpawnJobRequest request);
  [[nodiscard]] NaturalSpawnApplyStats apply_job_result(
      WorldRuntime& world,
      const voxel::BlockRegistry& block_registry,
      const entity::EntityRegistry& entity_registry,
      const NaturalSpawnJobResult& result,
      TickIndex tick,
      bool record_job_completion = false);

  void record_job_submitted(u32 pending_jobs) noexcept;
  void record_pending_job_count(u32 pending_jobs) noexcept;

private:
  struct CandidateProfile {
    worldgen::TerrainProfile profile{};
  };

  [[nodiscard]] u32 attempt_spawn_near_player(
      WorldRuntime& world,
      const voxel::BlockRegistry& block_registry,
      const entity::EntityRegistry& entity_registry,
      const SpawnEvaluationContext& context,
      const player::PlayerState& player,
      SpawnCategory category,
      TickIndex tick,
      HashMap<ChunkCoord, CandidateProfile>& profile_cache);
  [[nodiscard]] u32 spawn_group(
      WorldRuntime& world,
      const SpawnEntry& entry,
      BlockPos root,
      const SpawnEvaluationContext& context,
      TickIndex tick);
  [[nodiscard]] std::optional<CandidateProfile> profile_for(
      const WorldRuntime& world,
      ChunkCoord coord,
      HashMap<ChunkCoord, CandidateProfile>& profile_cache);
  [[nodiscard]] BlockPos pick_candidate_near_player(
      const player::PlayerState& player,
      SpawnCategory category,
      const worldgen::TerrainProfile& profile,
      ChunkCoord column);
  [[nodiscard]] BlockPos pick_group_member(BlockPos root);
  u32 despawn_natural_entities(
      WorldRuntime& world,
      const SpawnEvaluationContext& context,
      TickIndex tick);
  void publish_snapshot_counts() noexcept;

  NaturalSpawningConfig config_{};
  SpawnRuleRegistry rules_{};
  SpawnConditionEvaluator evaluator_{};
  SpawnLoadTracker load_tracker_{};
  NaturalSpawnCounters counters_{};
  std::mt19937_64 rng_{0xA17A4E5EEDULL};
  u64 measured_spawn_ticks_{};
  u64 total_spawn_tick_cost_us_{};
  u64 measured_spawn_prepare_ticks_{};
  u64 total_spawn_prepare_cost_us_{};
  u64 measured_spawn_worker_ticks_{};
  u64 total_spawn_worker_cost_us_{};
  u64 measured_spawn_apply_ticks_{};
  u64 total_spawn_apply_cost_us_{};
};

} // namespace arv::world
