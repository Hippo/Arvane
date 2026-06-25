#include "benchmark_support.hpp"

#include "arv/entity/components.hpp"
#include "arv/world/natural_spawning.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

arv::entity::EntityRegistry make_entity_registry() {
  arv::entity::EntityDefinition pig;
  pig.name = "arvane:bench_pig";
  pig.category = "passive";
  pig.spawnable = true;
  pig.persistent_by_default = false;
  pig.components.collision = arv::entity::AuthoredCollisionComponent{{0.9F, 0.9F, 0.9F}};
  auto result = arv::entity::EntityRegistry::build({std::move(pig)});
  if (!result) {
    throw std::runtime_error(result.error().message);
  }
  return std::move(result.value());
}

std::shared_ptr<const arv::world::worldgen::ResolvedDimension> make_dimension(
    arv::u32 entry_count = 1U) {
  namespace worldgen = arv::world::worldgen;

  auto dimension = std::make_shared<worldgen::ResolvedDimension>();
  dimension->raw.id = "arvane:bench/dimension/spawns";
  dimension->raw.version = 1U;
  dimension->raw.seed_salt = "bench_spawns";
  dimension->raw.vertical.min_y = 0;
  dimension->raw.vertical.max_y = 31;
  dimension->raw.vertical.sea_level = -1;
  dimension->terrain.height.base = 0;
  dimension->terrain.height.scale = 0;
  dimension->terrain.height.detail_scale = 0;
  dimension->terrain.primary_noise.seed_salt = "bench_primary";
  dimension->terrain.primary_noise.amplitude = 0.0F;
  dimension->terrain.detail_noise.seed_salt = "bench_detail";
  dimension->terrain.detail_noise.amplitude = 0.0F;

  worldgen::BiomeConfig biome;
  biome.id = "arvane:bench/biome/plains";
  auto& passive =
      biome.spawn_categories[
          arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)];
  passive.specified = true;
  passive.budget_profile = {"arvane:bench/spawn_budget_profile/passive"};
  passive.budget = {};
  passive.budget.id = passive.budget_profile.id;
  passive.budget.category = arv::world::SpawnCategory::Passive;
  passive.budget.tick_interval = 1U;
  passive.budget.attempts_per_interval = 64U;
  passive.budget.max_successes_per_interval = 8U;
  passive.budget.spawn_area.min_distance_blocks = 0U;
  passive.budget.spawn_area.max_distance_blocks = 64U;
  passive.budget.spawn_area.eligible_chunk_radius = 8U;
  passive.budget.population.soft_cap_per_player = 128U;
  passive.budget.population.hard_cap_per_player = 1024U;
  passive.budget.population.density_per_eligible_chunk = 2.0F;
  passive.budget.population.max_per_chunk = 32U;
  passive.budget.population.max_per_3x3_chunks = 128U;
  passive.budget.credit.refill_per_interval = 128.0F;
  passive.budget.credit.max_bank = 128.0F;
  passive.budget.despawn.enabled = false;
  biome.spawns.reserve(entry_count);
  for (arv::u32 index = 0; index < entry_count; ++index) {
    worldgen::BiomeSpawnEntryConfig spawn;
    spawn.id = {"arvane:bench_pig_" + std::to_string(index)};
    spawn.entity = {"arvane:bench_pig"};
    spawn.category = arv::world::SpawnCategory::Passive;
    spawn.weight = 1U + (index % 10U);
    spawn.min_group = 1U;
    spawn.max_group = 1U;
    spawn.conditions.requires_surface = true;
    spawn.conditions.min_distance_from_player_blocks = 0U;
    spawn.conditions.max_distance_from_player_blocks = 96U;
    biome.spawns.push_back(std::move(spawn));
  }
  dimension->biomes.push_back({std::move(biome), std::nullopt});
  return dimension;
}

arv::world::WorldRuntime make_world(
    const arv::voxel::BlockRegistry& registry,
    std::shared_ptr<const arv::world::worldgen::ResolvedDimension> dimension,
    arv::i32 radius_chunks) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::world::WorldGenerationConfig config =
      arv::world::config_from_resolved_dimension(std::move(dimension));
  world.configure_height(arv::world::world_height_settings_from_generation_config(config));
  world.set_generation_context(1337U, std::move(config));
  const arv::voxel::BlockVariantID grass =
      arv::bench_support::find_variant(registry, "arvane:grass");
  for (arv::i32 chunk_z = -radius_chunks; chunk_z <= radius_chunks; ++chunk_z) {
    for (arv::i32 chunk_x = -radius_chunks; chunk_x <= radius_chunks; ++chunk_x) {
      arv::bench_support::make_active_chunk(world, {{chunk_x, 0, chunk_z}});
      for (arv::i32 local_z = 0; local_z < arv::ChunkSize; ++local_z) {
        for (arv::i32 local_x = 0; local_x < arv::ChunkSize; ++local_x) {
          world.set_block(
              {{
                  chunk_x * arv::ChunkSize + local_x,
                  0,
                  chunk_z * arv::ChunkSize + local_z,
              }},
              grass);
        }
      }
    }
  }
  return world;
}

arv::world::NaturalSpawnSystem make_system(
    const arv::world::worldgen::ResolvedDimension& dimension,
    const arv::entity::EntityRegistry& entities,
    const arv::voxel::BlockRegistry& blocks,
    arv::u32 attempts,
    arv::u32 passive_per_player_cap = 128U,
    arv::u32 local_density_radius_blocks = 16U,
    arv::u32 passive_local_max = 32U) {
  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 64U;
  config.attempts_per_player_per_category = attempts;
  config.max_successes_per_player_per_tick = 8U;
  config.max_successes_per_world_per_tick = 32U;
  config.local_density_radius_blocks = local_density_radius_blocks;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].per_player = passive_per_player_cap;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].per_world_soft = 4096U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = passive_local_max;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(dimension, entities, blocks));
  return system;
}

void add_existing_natural_entities(
    arv::world::WorldRuntime& world,
    const arv::entity::EntityRegistry& registry,
    arv::u32 count) {
  const std::optional<arv::EntityTypeID> type_id = registry.find_type("arvane:bench_pig");
  if (!type_id.has_value()) {
    throw std::runtime_error("benchmark entity type is missing");
  }
  for (arv::u32 index = 0; index < count; ++index) {
    arv::world::EntityCreateInfo info;
    info.type_id = type_id.value();
    info.type_name = "arvane:bench_pig";
    info.position = {
        static_cast<arv::f32>(static_cast<arv::i32>(index % 64U) - 32),
        1.0F,
        static_cast<arv::f32>(static_cast<arv::i32>((index / 64U) % 64U) - 32)};
    const arv::EntityID entity = world.create_entity(std::move(info));
    (void)world.set_entity_natural_spawn(
        entity,
        {arv::world::SpawnCategory::Passive,
         type_id.value(),
         "arvane:bench_pig_existing",
         0U,
         true});
  }
}

void BM_NaturalSpawningSinglePlayerLowPopulation(benchmark::State& state) {
  const arv::voxel::BlockRegistry blocks = arv::bench_support::make_registry();
  const arv::entity::EntityRegistry entities = make_entity_registry();
  const auto dimension = make_dimension();
  arv::u64 attempts = 0U;
  arv::u64 successes = 0U;

  for (auto _ : state) {
    state.PauseTiming();
    arv::world::WorldRuntime world = make_world(blocks, dimension, 4);
    world.ensure_player(arv::PlayerID{1}).position = {0.0F, 1.0F, 0.0F};
    arv::world::NaturalSpawnSystem system = make_system(*dimension, entities, blocks, 32U);
    state.ResumeTiming();

    system.tick(world, blocks, entities, 1U);
    benchmark::DoNotOptimize(world.entity_count());

    state.PauseTiming();
    attempts += system.counters().attempts_total;
    successes += system.counters().successes_total;
    state.ResumeTiming();
  }

  state.counters["attempts"] =
      benchmark::Counter(static_cast<double>(attempts), benchmark::Counter::kAvgIterations);
  state.counters["successes"] =
      benchmark::Counter(static_cast<double>(successes), benchmark::Counter::kAvgIterations);
}

void BM_NaturalSpawningDenseLocalPopulation(benchmark::State& state) {
  const arv::voxel::BlockRegistry blocks = arv::bench_support::make_registry();
  const arv::entity::EntityRegistry entities = make_entity_registry();
  const auto dimension = make_dimension();
  arv::u64 rejected_local = 0U;

  for (auto _ : state) {
    state.PauseTiming();
    arv::world::WorldRuntime world = make_world(blocks, dimension, 4);
    world.ensure_player(arv::PlayerID{1}).position = {0.0F, 1.0F, 0.0F};
    add_existing_natural_entities(world, entities, 512U);
    arv::world::NaturalSpawnSystem system =
        make_system(*dimension, entities, blocks, 64U, 1024U, 128U, 1U);
    state.ResumeTiming();

    system.tick(world, blocks, entities, 1U);
    benchmark::DoNotOptimize(system.counters().natural_entities_by_category);

    state.PauseTiming();
    rejected_local += system.counters().rejected_local_density;
    state.ResumeTiming();
  }

  state.counters["local_rejects"] =
      benchmark::Counter(static_cast<double>(rejected_local), benchmark::Counter::kAvgIterations);
}

void BM_NaturalSpawningTwoPlayersManyEntries(benchmark::State& state) {
  const arv::voxel::BlockRegistry blocks = arv::bench_support::make_registry();
  const arv::entity::EntityRegistry entities = make_entity_registry();
  const auto dimension = make_dimension(64U);
  arv::u64 attempts = 0U;
  arv::u64 successes = 0U;

  for (auto _ : state) {
    state.PauseTiming();
    arv::world::WorldRuntime world = make_world(blocks, dimension, 5);
    world.ensure_player(arv::PlayerID{1}).position = {-16.0F, 1.0F, 0.0F};
    world.ensure_player(arv::PlayerID{2}).position = {16.0F, 1.0F, 0.0F};
    arv::world::NaturalSpawnSystem system = make_system(*dimension, entities, blocks, 24U);
    state.ResumeTiming();

    system.tick(world, blocks, entities, 1U);
    benchmark::DoNotOptimize(world.entity_count());

    state.PauseTiming();
    attempts += system.counters().attempts_total;
    successes += system.counters().successes_total;
    state.ResumeTiming();
  }

  state.counters["attempts"] =
      benchmark::Counter(static_cast<double>(attempts), benchmark::Counter::kAvgIterations);
  state.counters["successes"] =
      benchmark::Counter(static_cast<double>(successes), benchmark::Counter::kAvgIterations);
}

} // namespace

BENCHMARK(BM_NaturalSpawningSinglePlayerLowPopulation);
BENCHMARK(BM_NaturalSpawningDenseLocalPopulation);
BENCHMARK(BM_NaturalSpawningTwoPlayersManyEntries);
