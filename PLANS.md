# Natural Entity Spawning Implementation

This `PLANS.md` is a living execution plan. Keep it updated as the implementation progresses. Record discoveries, changed assumptions, measured performance, and completed work directly in this file.

The goal is to implement server-side natural entity spawning for Arvane using biome-aware, load-capped, sampling-based spawn logic. This pass must work with the current chunk loading model: chunks are currently loaded/simulated based on player distance. Do not implement chunk residency, chunk tickets, entity-driven chunk retention, or a new chunk lifetime system as part of this plan.

## Purpose / Big Picture

Arvane needs a natural entity spawning system that can populate active areas with biome-appropriate entities while respecting performance limits. The system should be server-authoritative, data-driven enough for packs, and designed around current engine constraints.

Natural spawning should:

- Spawn entities only in currently loaded/simulated areas near players.
- Use the biome at the candidate spawn position to select possible entities.
- Respect entity load using category caps, world caps, local density checks, and per-tick throttles.
- Avoid scanning every chunk every tick.
- Avoid coupling entity spawning to terrain generation.
- Avoid introducing chunk residency/tickets in this pass.
- Support future expansion for modded packs, 3D biomes, special events, structure spawns, and more detailed spawn predicates.

World generation defines terrain and biomes. Natural spawning decides whether active simulated areas should currently contain entities.

## Non-Goals

This implementation must not:

- Add a chunk residency/ticket system.
- Keep chunks loaded because natural entities exist inside them.
- Spawn natural entities during normal terrain chunk generation.
- Scan every loaded chunk every spawn tick.
- Add scripting hooks inside the hot spawn loop.
- Add plugin-defined network packets.
- Create client-authoritative spawning.
- Make passive/hostile/ambient entities share one undifferentiated cap.
- Persist thousands of transient ambient/hostile entities by default without a deliberate save policy.

If structure-placed entities are needed later, treat them separately from natural spawning.

## Current Constraint: No Chunk Residency System

The current engine loads and simulates chunks based on player distance. This plan must integrate with that model.

For this pass:

- Candidate spawn positions must be rejected if their chunk is not currently loaded.
- Candidate spawn positions must be rejected if their chunk is outside the current simulation distance.
- Natural entities must not add chunk tickets.
- Natural entities must not prevent chunk unload.
- If a chunk unloads, entity persistence must follow the existing entity/chunk save policy.
- If persistence filtering is available or can be added cleanly, transient natural entities should be discarded or not serialized according to their category policy.

The spawn system should ask the world/chunk system questions like:

```cpp
bool is_chunk_loaded(ChunkPos pos) const;
bool is_chunk_simulated(ChunkPos pos) const;
```

Use the real existing APIs where they exist. If the exact names differ, adapt to the existing engine style.

## Progress

* [x] Audit existing world/chunk simulation distance APIs.
* [x] Audit existing entity creation, replication, serialization, and despawn paths.
* [x] Add spawn category definitions.
* [x] Add spawn metadata to natural entities.
* [x] Add data-driven biome spawn table loading.
* [x] Add spawn rule registry.
* [x] Add spawn condition evaluator.
* [x] Add spawn load tracker.
* [x] Add natural spawn system.
* [x] Add natural despawn system or integrate with existing despawn logic.
* [x] Add debug counters/logging.
* [x] Add tests for rule loading.
* [x] Add tests for condition evaluation.
* [x] Add tests for caps and local density.
* [x] Add tests that spawning only occurs in loaded/simulated chunks.
* [x] Add performance benchmark or profiling harness.
* [x] Update documentation.
* [x] Validate in a real world with several biomes.

## Decision Log

Record important decisions here as they are made.

* Decision: Natural spawning is server-side only.
  Rationale: The server owns simulation authority and entity creation.

* Decision: Natural spawning is not part of chunk generation.
  Rationale: Entity population should respond to active simulation, players, difficulty, time, and load.

* Decision: This pass uses current player-distance chunk loading/simulation only.
  Rationale: Chunk residency previously caused performance issues and is out of scope.

* Decision: Biome selection uses the candidate spawn position, not the player position.
  Rationale: Correct behavior near biome borders requires the actual spawn location to decide the spawn table.

* Decision: Spawn attempts are sampling-based.
  Rationale: Avoid full loaded-chunk scans and keep work bounded.

* Decision: Natural spawn tables are authored on existing `worldgen/biome/*.yaml`
  resources under a `spawns:` field.
  Rationale: Biomes are already pack data resources and the loader already resolves
  dimension biome IDs from pack data.

* Decision: The V1 biome query uses the resolved dimension terrain profile for
  the candidate chunk and caches profiles inside a spawn tick.
  Rationale: Loaded chunks currently cache surface height only, not biome index,
  so regenerating a bounded number of candidate profiles is less invasive than
  changing chunk storage in this pass.

* Decision: V1 does not include weather spawn predicates.
  Rationale: Weather is not currently an implemented world simulation signal.
  Unknown spawn condition fields are rejected during biome data loading instead
  of silently ignored.

## Audit Notes

* Server tick entry point: `engine/arv_server/src/server_runtime.cpp`,
  `ServerRuntimeImpl::tick()`. This is the server-only hook point for natural
  spawn/despawn work.
* Player-distance simulation state: `ServerRuntimeImpl::update_world_simulation_activity()`
  in `engine/arv_server/src/server_player_simulation.cpp` computes player chunk
  centers and calls `WorldRuntime::update_simulation_radii(centers, simulation_radius_)`.
* Loaded/simulated chunk API: `WorldRuntime::find_chunk(ChunkSectionCoord)` tests
  loaded sections; `WorldRuntime::is_chunk_active(ChunkSectionCoord)` tests the
  current simulated/active state. Active chunks are represented as
  `voxel::ChunkLifecycleState::Active`.
* Server chunk distance config: `ServerChunkStreamingConfig::simulation_distance`
  is wired from `io::EngineConfig::chunks.simulation_distance` by desktop and
  dedicated startup helpers.
* Entity creation/despawn API: `WorldRuntime::create_entity(EntityCreateInfo)`,
  `WorldRuntime::destroy_entity(EntityID, EntityDespawnReason)`, and
  `WorldRuntime::flush_entity_despawns()`.
* Entity replication path: server tick calls `send_entity_replication()` and
  `send_dropped_item_replication()` after world simulation; replicated entities
  are those with `entity::NetworkEntity`.
* Entity persistence path: `WorldRuntime::persistent_entity_records()` delegates
  to `WorldEntityStore::persistent_records()` and is consumed by desktop and
  dedicated save-state capture.
* Player query API: `WorldRuntime::player_ids()` and `WorldRuntime::find_player(PlayerID)`.
* Block/light/fluid query API: `WorldRuntime::get_block(BlockPos)`,
  `WorldRuntime::get_light(BlockPos)`, `WorldRuntime::get_fluid(BlockPos)`, and
  `WorldSearch` for cached nearby lookups.
* Biome/worldgen data: pack biomes are loaded by
  `worldgen::load_worldgen_registry_from_packs()` from `worldgen/biome`, resolved
  into `worldgen::ResolvedDimension`, and used by `worldgen::generate_terrain_profile()`.
* Entity registry: `entity::EntityRegistry` resolves authored entity names to
  `EntityTypeID` and exposes authored components/persistence/replication defaults.
* Block tags: `voxel::BlockRegistry::has_tag(BlockID, string_view)` is available;
  spawn condition tag checks should resolve block variants to block IDs.
* Config loading/saving: `io::EngineConfig` is parsed in
  `engine/arv_io/src/config_runtime.cpp` and re-emitted through the same file.

## Implementation Notes

* Spawn category and metadata types live in
  `engine/arv_world/include/arv/world/natural_spawn_types.hpp`.
* The main rule registry, condition evaluator, load tracker, counters, spawn
  tick, group spawn, and natural despawn behavior live in
  `engine/arv_world/include/arv/world/natural_spawning.hpp` and
  `engine/arv_world/src/natural_spawning.cpp`.
* Natural spawn metadata is stored on world entities through `WorldRuntime`
  helpers and is filtered in `WorldEntityStore::persistent_records()` when
  marked transient.
* Biome spawn tables are parsed from `spawns:` in `worldgen/biome/*.yaml`.
  Syntactically malformed entries fail worldgen data loading. Semantic runtime
  validation, such as unknown entity types, non-spawnable entity definitions, or
  unknown block tags, records spawn diagnostics and skips the affected entries.
* Server startup builds the natural spawn rule registry from the resolved
  dimension, entity registry, and block registry. If natural spawning is enabled
  but startup lacks an entity registry or resolved dimension, the server logs a
  warning and disables the natural spawn system for that run.
* The server tick calls natural spawning after simulation activity is updated
  and before ordinary world ticking/replication.
* First-party `arvane_base` biome data now gives grassy land biomes passive pig
  entries, and `arvane:grass_block` carries the `arvane:spawn/grass_like` tag.
* Natural spawn config is parsed, validated, and canonically re-emitted under
  top-level `natural_spawning:` in `arvane.yaml`.

## Validation Results

Latest validation pass:

* `cmake --build build -j 4`: passed.
* `./build/tests/arv_tests`: passed, 752 tests.
* `./build/tests/arv_tests --gtest_filter='NaturalSpawn*'`: passed, 10 tests.
* `./build/benchmarks/arv_benchmarks --benchmark_filter=NaturalSpawning --benchmark_min_time=0.02s --benchmark_repetitions=3`: passed.
* `timeout 2 ./build/apps/arv_dedicated/arv_dedicated`: loaded the dedicated server, resolved the overworld, loaded 5 natural spawn entries with 0 diagnostics, and shut down cleanly after the timeout.

Benchmark snapshot from the natural-spawning benchmark run:

| Case | Mean real time | Median real time | Counters |
|---|---:|---:|---|
| Single player, low population | 1.423 ms | 1.447 ms | attempts=169, successes=8 |
| Dense local population | 1.584 ms | 1.576 ms | local_rejects=61 |
| Two players, 64 table entries | 1.480 ms | 1.472 ms | attempts=258, successes=16 |

The benchmark host reported CPU scaling enabled, so these numbers are useful as
a local regression baseline rather than stable hardware-independent timings.

## Architecture Overview

Add a natural spawning system shaped roughly like this:

```text
WorldSimulation
  NaturalSpawnSystem
    SpawnRuleRegistry
    SpawnConditionEvaluator
    SpawnLoadTracker
    SpawnRng
  NaturalDespawnSystem / Despawn integration
```

The simulation tick should call natural spawning on the server only:

```cpp
void WorldSimulation::tick() {
    // existing block/entity/world simulation work

    natural_spawn_system.tick(world, tick_index);

    // existing cleanup/despawn work, or:
    natural_despawn_system.tick(world, tick_index);
}
```

The spawn system should not directly know how to render, replicate, or save entities. It should use existing entity creation APIs. Existing replication should notify clients that a new entity exists.

## Data Model

### SpawnCategory

Add a compact enum for natural spawn categories.

Suggested categories:

```cpp
enum class SpawnCategory : uint8_t {
    Passive,
    Hostile,
    Ambient,
    Water,
    Underground,
    Special,
};
```

Use the project’s existing enum/string conversion style if one exists.

Each category should have separate:

* Spawn cap.
* Spawn interval or attempt budget.
* Despawn policy.
* Persistence policy.
* Local density limit.

### Natural Spawn Metadata

Natural entities need enough metadata for counting and despawn decisions.

Suggested component:

```cpp
struct NaturalSpawnComponent {
    SpawnCategory category;
    EntityTypeID source_entity_type;
    TickIndex spawn_tick;
    bool transient;
};
```

If the existing ECS/entity system has a different metadata model, adapt to that style. The important point is that naturally spawned entities can be counted and despawned by category.

Do not add this component to player-created, command-created, plugin-managed, named, or structure-critical entities unless deliberately intended.

### Spawn Entry

A spawn entry represents one possible entity in a biome/category spawn table.

```cpp
struct SpawnEntry {
    EntityTypeID entity_type;
    SpawnCategory category;

    uint32_t weight;

    uint8_t min_group;
    uint8_t max_group;

    SpawnConditions conditions;
};
```

### Spawn Conditions

Use native C++ predicates backed by data. Do not execute arbitrary scripts inside the hot spawn loop.

Suggested first-pass conditions:

```cpp
struct SpawnConditions {
    std::optional<int32_t> min_y;
    std::optional<int32_t> max_y;

    std::optional<uint8_t> min_light;
    std::optional<uint8_t> max_light;

    bool requires_surface = false;
    bool requires_sky_access = false;
    bool requires_water = false;
    bool requires_air_body = true;
    bool requires_air_head = true;

    std::vector<BlockTagID> block_tags_below;
    std::vector<BlockTagID> block_tags_inside;
    std::vector<BlockTagID> block_tags_head;

    uint32_t min_distance_from_player_blocks = 24;
    uint32_t max_distance_from_player_blocks = 96;

    std::optional<TimePredicate> time;
    std::optional<WeatherPredicate> weather;
};
```

Keep this small and practical for V1. Add TODOs for later predicates if needed.

## Data-Driven Spawn Tables

Add biome spawn data under pack data. Use the existing pack/data conventions. Do not invent a separate asset root if the project already has one.

Example shape:

```yaml
spawns:
  passive:
    - entity: arvane:sheep
      weight: 12
      min_group: 2
      max_group: 4
      conditions:
        surface: true
        min_light: 8
        block_tags_below: [arvane:grass_like]

    - entity: arvane:deer
      weight: 4
      min_group: 1
      max_group: 3
      conditions:
        surface: true
        min_light: 7
        block_tags_below: [arvane:grass_like]

  hostile:
    - entity: arvane:zombie
      weight: 10
      min_group: 1
      max_group: 3
      conditions:
        max_light: 7
        requires_air_body: true
        requires_air_head: true

  ambient:
    - entity: arvane:firefly
      weight: 6
      min_group: 3
      max_group: 8
      conditions:
        time: night
        max_light: 10
```

Implement the loader so invalid entries produce useful diagnostics:

* Unknown entity type.
* Unknown spawn category.
* Invalid weight.
* Invalid group range.
* Unknown block tag.
* Invalid condition field.
* Biome file references missing biome ID.

A bad spawn table should not crash the engine. Prefer structured errors and skip invalid entries where safe.

## Spawn Config

Add server/world config for natural spawning.

Suggested shape:

```yaml
natural_spawning:
  enabled: true
  interval_ticks: 20

  min_distance_from_player_blocks: 24
  max_distance_from_player_blocks: 96

  attempts_per_player_per_category: 4
  max_successes_per_player_per_tick: 2
  max_successes_per_world_per_tick: 32

  local_density_radius_blocks: 32

  caps:
    passive:
      per_player: 24
      per_world_soft: 512
      local_max: 12

    hostile:
      per_player: 48
      per_world_soft: 1024
      local_max: 24

    ambient:
      per_player: 32
      per_world_soft: 768
      local_max: 32

    water:
      per_player: 32
      per_world_soft: 768
      local_max: 24

  despawning:
    hostile:
      natural_despawn: true
      soft_distance_blocks: 96
      hard_distance_blocks: 160
      min_lifetime_ticks: 600
      save_transient: false

    ambient:
      natural_despawn: true
      soft_distance_blocks: 64
      hard_distance_blocks: 128
      min_lifetime_ticks: 200
      save_transient: false

    passive:
      natural_despawn: false
      save_transient: true
```

Actual defaults may be tuned after measurement. The config shape should allow category-specific behavior.

## Spawn Algorithm

The algorithm must be bounded and sampling-based.

High-level flow:

```cpp
void NaturalSpawnSystem::tick(World& world, TickIndex tick) {
    if (!config.enabled) {
        return;
    }

    if (tick % config.interval_ticks != 0) {
        return;
    }

    load_tracker.refresh(world);

    uint32_t world_successes = 0;

    for (SpawnCategory category : all_spawn_categories()) {
        if (!load_tracker.has_category_budget(world, category)) {
            continue;
        }

        for (Entity player : world.players()) {
            if (world_successes >= config.max_successes_per_world_per_tick) {
                return;
            }

            uint32_t player_successes = 0;

            for (uint32_t attempt = 0;
                 attempt < config.attempts_per_player_per_category;
                 ++attempt) {
                if (player_successes >= config.max_successes_per_player_per_tick) {
                    break;
                }

                if (attempt_spawn_near_player(world, player, category, tick)) {
                    ++player_successes;
                    ++world_successes;
                }
            }
        }
    }
}
```

Candidate attempt order should be cheap-first:

```text
1. Pick random candidate position in a ring around a player.
2. Convert candidate position to chunk position.
3. Reject if chunk is not loaded.
4. Reject if chunk is not simulated.
5. Reject if candidate is too close or too far from all relevant players.
6. Query biome at candidate position.
7. Get biome/category spawn table.
8. Weighted-pick a spawn entry.
9. Check global/category cap.
10. Check cheap static conditions: y range, time, weather.
11. Check block/light/material conditions.
12. Check local density.
13. Pick group size.
14. Validate group member positions.
15. Spawn the group only if enough valid positions exist.
```

Do not query expensive block/light/local density information before confirming that:

* The chunk exists.
* The chunk is simulated.
* The biome has a spawn table for this category.
* The category has budget.

## Candidate Position Sampling

Sample around each player, not over all loaded chunks.

Use an annulus/ring:

```text
min_distance_from_player_blocks <= distance <= max_distance_from_player_blocks
```

Default suggestion:

```text
min = 24 blocks
max = 96 blocks
```

For V1, horizontal distance is the primary filter. Vertical rules should come from the candidate search and spawn conditions.

Possible implementation:

```cpp
BlockPos pick_candidate_near_player(PlayerPosition player_pos) {
    int dx = random_signed_range(min, max);
    int dz = random_signed_range(min, max);

    // Reject if inside min radius.
    // Then pick y according to category:
    // - surface/passive: use surface height if available
    // - hostile/underground: random y or local nearby y depending on existing terrain APIs
    // - water: use water surface/volume search if available
}
```

Prefer existing heightmap/surface APIs where available. If no efficient heightmap exists, add a narrow bounded search in the target column and record its cost in benchmarks.

Do not introduce broad terrain scans.

## Biome Lookup Policy

For this pass, use the biome at the candidate spawn position.

Add or use existing APIs equivalent to:

```cpp
BiomeID biome_at_block(BlockPos pos);
BiomeID surface_biome_at_column(BlockXZ pos);
```

If the engine currently only has 2D/surface biomes, use the column biome for all surface spawning and document that underground/volume biomes are future work.

Recommended policy:

* Passive surface entities: surface/column biome.
* Hostile surface entities: surface/column biome.
* Water entities: biome at water column or surface/column biome.
* Underground entities: volume biome if available; otherwise use column biome plus y/block predicates.

Do not use the player’s biome to select spawn entries.

## Load Accounting

Implement load accounting by category.

The spawn system needs to answer:

```cpp
bool has_world_budget(World& world) const;
bool has_category_budget(World& world, SpawnCategory category) const;
bool has_local_budget(World& world, BlockPos pos, SpawnCategory category, EntityTypeID type) const;
```

Suggested accounting:

```cpp
struct CategorySpawnCount {
    uint32_t total = 0;
    uint32_t natural = 0;
};

struct SpawnLoadSnapshot {
    uint32_t total_world_entities = 0;
    HashMap<SpawnCategory, CategorySpawnCount> category_counts;
};
```

Refresh this snapshot on each spawn interval, not every frame.

### Local Density

Local density prevents one area from overpopulating.

Preferred implementation:

* Use an existing entity spatial index if present.
* Otherwise, use chunk-local entity lists if present.
* If neither exists, implement a bounded ECS scan for V1 with early exit and benchmark it.

Local density should count:

* Entities in the same category near the candidate.
* Optionally entities of the same type near the candidate.

Do not make local density checks mandatory for every entity in the world if the cost becomes measurable. If the temporary ECS scan is too expensive, keep category/global caps and add a documented TODO to replace local density with a spatial index.

## Spawn Group Validation

Group spawning should validate each member position.

Flow:

```text
1. Pick root position.
2. Pick group size in [min_group, max_group].
3. Generate nearby offsets.
4. Validate each member position independently.
5. Spawn the group only if at least min_group positions are valid.
```

Do not spawn all members at the same block.

If the group cannot place at least `min_group` members, reject the whole spawn attempt.

## Spawn Condition Evaluation

Add a `SpawnConditionEvaluator`.

Suggested interface:

```cpp
class SpawnConditionEvaluator {
public:
    bool passes(
        const World& world,
        const SpawnEntry& entry,
        BlockPos candidate,
        SpawnCategory category
    ) const;
};
```

Keep checks deterministic enough for testing, but the spawn system itself can use normal world RNG.

Condition checks should include:

* Chunk loaded/simulated.
* Y range.
* Time predicate.
* Weather predicate, if weather exists.
* Light predicate, if light data exists.
* Required body/head air blocks.
* Required block tags below/inside/head.
* Surface/water requirements.
* Sky access, if supported.
* Distance to player constraints.

If a feature does not yet exist, such as weather or sky access, do not fake it. Either skip unsupported fields with a diagnostic or reject config entries using unsupported predicates. Prefer explicit diagnostics.

## Despawning

Entity load must be controlled by despawning as well as caps.

Implement or integrate natural despawn rules for entities with `NaturalSpawnComponent`.

Suggested behavior:

* Passive:

  * Usually persistent.
  * No natural despawn by default.
* Hostile:

  * Despawn when far from players.
  * Respect minimum lifetime.
  * Hard-despawn past a larger distance.
* Ambient:

  * Despawn aggressively.
  * Usually transient.
* Water:

  * Despawn similarly to ambient or hostile unless marked persistent.

Despawning must not remove:

* Players.
* Player-placed entities.
* Named/preserved entities.
* Plugin-managed entities.
* Structure-critical entities.
* Entities without natural spawn metadata unless existing systems already despawn them intentionally.

Distance policy:

```text
soft_distance:
  eligible for probabilistic despawn after min_lifetime

hard_distance:
  despawn immediately after min_lifetime
```

If the engine lacks named/persistent entity metadata, add a minimal persistence marker or leave a documented TODO before applying broad despawn behavior.

## Entity Persistence / Chunk Unload

Because this pass does not include chunk residency, natural entities must not affect chunk lifetime.

When chunks unload under the current player-distance system:

* Persistent natural entities may be saved if the existing system supports it.
* Transient natural entities should not cause save bloat.
* Ambient and hostile entities should usually be transient by default.
* Passive animals can be persistent by default if that matches the game design.

If entity save filtering does not exist, add a minimal category/persistence filter where entities are serialized. If that would require a major save-system rewrite, document the limitation and avoid spawning high volumes of persistent entities by default.

## Networking

Natural spawning is server-authoritative.

Do not add new client-to-server spawn packets.

Use existing entity replication. If existing replication is incomplete, fix that path generically rather than adding natural-spawn-specific networking.

If new protocol packets are absolutely required, follow Arvane packet naming conventions:

* `ClientboundX` means server sends `X` to client.
* `ServerboundX` means client sends `X` to server.

Do not invent plugin-defined packets for natural spawning.

## Debugging and Observability

Add debug counters that can be logged or displayed in existing debug UI.

Suggested counters per world/dimension:

```text
natural_spawn_attempts_total
natural_spawn_successes_total
rejected_chunk_not_loaded
rejected_chunk_not_simulated
rejected_no_biome_table
rejected_category_cap
rejected_world_cap
rejected_local_density
rejected_conditions
rejected_group_failed
despawned_natural_hostile
despawned_natural_ambient
despawned_natural_water
```

Also expose current counts:

```text
natural_entities_by_category
total_entities_by_category
spawn_attempts_last_interval
spawn_successes_last_interval
avg_spawn_tick_cost_us
max_spawn_tick_cost_us
```

Use the project’s existing profiling/timing utilities if available.

## Performance Requirements

The system must keep spawn work bounded.

Initial performance targets:

* Natural spawn tick should not scan every loaded chunk.
* Natural spawn tick should have bounded attempts:

  * `players * categories * attempts_per_player_per_category`.
* Load snapshots should refresh at spawn interval cadence, not every frame.
* Local density checks should early out once the limit is exceeded.
* Spawn condition checks should be ordered cheap-to-expensive.
* No chunk residency/ticket system should be introduced.

Measure:

* Spawn tick p50/p90/p99 time.
* Entity count by category.
* Spawn success rate.
* Rejection reasons.
* Cost with 1 player.
* Cost with several simulated players if test harness allows.
* Cost in dense entity areas.

If performance is poor, first reduce attempts and improve local density indexing. Do not solve this by adding chunk residency.

## Implementation Milestones

### Milestone 1: Audit Existing Systems

Inspect the current codebase and document the relevant APIs.

Find:

* World simulation tick entry point.
* Server/client simulation separation.
* Existing entity creation API.
* Existing entity despawn/remove API.
* Existing entity replication path.
* Existing entity serialization path.
* Existing biome lookup API.
* Existing block query API.
* Existing light query API, if any.
* Existing chunk loaded/simulated checks.
* Existing player list/query API.
* Existing config loading conventions.
* Existing pack data loading conventions.

Update this plan with exact file/class/function names discovered.

Acceptance criteria:

* This plan contains accurate codebase-specific notes.
* No implementation starts before the current entity/chunk/world APIs are understood.

### Milestone 2: Spawn Categories and Metadata

Implement `SpawnCategory` and natural spawn metadata.

Tasks:

* Add category enum.
* Add string conversion for config/data loading.
* Add `NaturalSpawnComponent` or equivalent metadata.
* Ensure metadata is attached only to naturally spawned entities.
* Add helper functions for category counting.

Acceptance criteria:

* Natural entities can be identified and counted by category.
* Non-natural entities are not accidentally affected.

### Milestone 3: Config and Data Loading

Implement spawn config and biome spawn table loading.

Tasks:

* Add natural spawning config.
* Add per-category caps.
* Add per-category despawn config.
* Load biome spawn tables from pack data.
* Validate entity IDs.
* Validate block tags.
* Validate group ranges.
* Validate weights.
* Add clear diagnostics.

Acceptance criteria:

* Valid spawn data loads successfully.
* Invalid spawn data produces useful errors.
* Spawn tables are queryable by biome and category.

### Milestone 4: Spawn Rule Registry

Implement a registry/query layer for spawn rules.

Tasks:

* Store tables by `BiomeID` and `SpawnCategory`.
* Support weighted selection.
* Support empty/missing tables.
* Avoid heap churn in hot spawn loops.
* Precompute total weights where useful.

Acceptance criteria:

* Given a biome and category, the system can select a valid weighted spawn entry.
* Missing spawn tables are handled without crashes.

### Milestone 5: Spawn Condition Evaluator

Implement native spawn condition checks.

Tasks:

* Add evaluator class/module.
* Implement loaded/simulated chunk checks.
* Implement distance checks.
* Implement y-range checks.
* Implement block/body/head checks.
* Implement block tag checks.
* Implement light checks if supported.
* Implement surface/water checks if supported.
* Add explicit diagnostics or validation for unsupported predicates.

Acceptance criteria:

* Conditions are deterministic enough for unit tests.
* Invalid positions are rejected.
* Valid positions pass.

### Milestone 6: Spawn Load Tracker

Implement world/category/local load tracking.

Tasks:

* Count natural entities by category.
* Count total relevant entities if needed.
* Check per-player derived caps.
* Check world soft caps.
* Check local density.
* Use existing spatial query if available.
* Add benchmark/profiling around local density if implemented with ECS scans.

Acceptance criteria:

* Category caps prevent spawning.
* World caps prevent spawning.
* Local density prevents overpopulation in one area.
* Load snapshot refresh cost is measured.

### Milestone 7: Natural Spawn System

Implement the main spawn tick.

Tasks:

* Hook into server-side world simulation tick.
* Respect `enabled`.
* Respect `interval_ticks`.
* Iterate categories.
* Iterate players.
* Sample candidate positions around players.
* Reject unloaded/non-simulated chunks.
* Query candidate biome.
* Weighted-pick spawn entry.
* Evaluate conditions.
* Validate group positions.
* Spawn group through existing entity creation path.
* Attach natural spawn metadata.
* Update debug counters.

Acceptance criteria:

* Entities spawn near players.
* Entities do not spawn in unloaded chunks.
* Entities do not spawn outside simulation distance.
* Entities use the candidate position’s biome.
* Spawn counts respect caps.
* Spawn work is bounded.

### Milestone 8: Natural Despawn

Implement natural despawn behavior.

Tasks:

* Despawn only entities with natural spawn metadata.
* Respect category config.
* Respect minimum lifetime.
* Respect soft/hard distance.
* Do not despawn protected/persistent entities.
* Add debug counters.

Acceptance criteria:

* Ambient/hostile entities despawn when far away.
* Passive entities remain if configured persistent.
* Player-created or protected entities are not removed.

### Milestone 9: Persistence Filtering

Integrate natural spawn metadata with entity save behavior if feasible.

Tasks:

* Audit entity serialization.
* Add save filtering for transient natural entities if the save path supports it cleanly.
* Ensure passive persistent entities can save if configured.
* Ensure ambient/hostile transient entities do not create large save bloat.
* If full filtering is too invasive, document the limitation and tune defaults conservatively.

Acceptance criteria:

* Transient natural entities do not create uncontrolled save growth, or the limitation is explicitly documented with conservative defaults.

### Milestone 10: Tests

Add tests.

Required tests:

* Spawn category string parsing.
* Spawn table loading.
* Invalid spawn table diagnostics.
* Weighted selection handles empty and non-empty tables.
* Spawn condition y-range.
* Spawn condition block requirements.
* Spawn condition distance from player.
* Reject candidate in unloaded chunk.
* Reject candidate in non-simulated chunk.
* Category cap prevents spawn.
* Local density prevents spawn.
* Group spawn rejects if below `min_group`.
* Despawn ignores non-natural entities.
* Despawn respects protected/persistent metadata.

Use GoogleTest/GoogleMock according to project conventions.

### Milestone 11: Benchmarks and Profiling

Add a benchmark or profiling harness for spawn logic.

Measure at minimum:

* 1 player, low entity count.
* 1 player, high entity count.
* Multiple players if easy to simulate.
* Dense local entity area.
* Many spawn table entries.
* Biome border sampling.

Capture:

```text
spawn_tick_time_us_p50
spawn_tick_time_us_p90
spawn_tick_time_us_p99
attempts_per_interval
successes_per_interval
rejections_by_reason
entity_counts_by_category
```

Acceptance criteria:

* Spawn work is bounded and does not scale with every loaded chunk.
* Any expensive local density or biome lookup cost is identified.
* Defaults are adjusted if necessary.

### Milestone 12: Documentation

Update permanent docs after implementation.

Likely docs to update:

* World/chunk simulation documentation.
* Biome/worldgen documentation.
* Entity system documentation.
* Pack/data format documentation.
* Server configuration documentation.
* Networking/protocol documentation only if entity replication changed.
* Agent instructions if there are new conventions.

Documentation must state:

* Natural spawning is server-side.
* Natural spawning is separate from chunk generation.
* Current implementation uses player-distance loaded/simulated chunks.
* No chunk residency/ticket behavior is part of this pass.
* Biome selection is based on candidate spawn position.
* Spawn caps are category-based.
* Transient natural entities may despawn or avoid persistence.

Acceptance criteria:

* Docs match implemented behavior.
* No docs imply chunk residency exists if it does not.

## Concrete Implementation Notes

### Candidate Spawn Function

Suggested shape:

```cpp
bool NaturalSpawnSystem::attempt_spawn_near_player(
    World& world,
    Entity player,
    SpawnCategory category,
    TickIndex tick
) {
    const auto player_pos = get_player_position(world, player);

    const BlockPos candidate = sampler.pick_candidate(world, player_pos, category);

    const ChunkPos chunk_pos = to_chunk_pos(candidate);

    if (!world.is_chunk_loaded(chunk_pos)) {
        counters.rejected_chunk_not_loaded++;
        return false;
    }

    if (!world.is_chunk_simulated(chunk_pos)) {
        counters.rejected_chunk_not_simulated++;
        return false;
    }

    const BiomeID biome = world.biomes().biome_for_spawn(candidate, category);

    const BiomeSpawnTable* table = spawn_rules.find_table(biome, category);
    if (table == nullptr || table->empty()) {
        counters.rejected_no_biome_table++;
        return false;
    }

    const SpawnEntry& entry = table->weighted_pick(rng);

    if (!load_tracker.has_budget(world, candidate, category, entry.entity_type)) {
        counters.rejected_load_budget++;
        return false;
    }

    if (!condition_evaluator.passes(world, entry, candidate, category)) {
        counters.rejected_conditions++;
        return false;
    }

    return spawn_group(world, candidate, entry, tick);
}
```

Adapt names to existing code style.

### Group Spawn Function

Suggested shape:

```cpp
bool NaturalSpawnSystem::spawn_group(
    World& world,
    BlockPos root,
    const SpawnEntry& entry,
    TickIndex tick
) {
    const uint32_t desired_count = rng.range(entry.min_group, entry.max_group);

    SmallVector<BlockPos, 8> valid_positions;

    for (uint32_t i = 0; i < desired_count; ++i) {
        BlockPos pos = sampler.pick_group_offset(root);

        if (condition_evaluator.passes(world, entry, pos, entry.category)) {
            valid_positions.push_back(pos);
        }
    }

    if (valid_positions.size() < entry.min_group) {
        counters.rejected_group_failed++;
        return false;
    }

    for (BlockPos pos : valid_positions) {
        Entity e = world.entities().spawn(entry.entity_type, pos);
        world.entities().add<NaturalSpawnComponent>(
            e,
            NaturalSpawnComponent{
                .category = entry.category,
                .source_entity_type = entry.entity_type,
                .spawn_tick = tick,
                .transient = is_transient_by_default(entry.category),
            }
        );
    }

    counters.spawn_successes += valid_positions.size();
    return true;
}
```

Do not use `SmallVector` unless the project already has one. Use the project’s existing small-buffer or vector type.

## Validation Checklist

Before marking complete, verify:

* [x] Natural spawning can be disabled globally.
* [x] Natural spawning only runs on the server simulation.
* [x] Spawn attempts are bounded by config.
* [x] Spawn attempts do not scan every loaded chunk.
* [x] Spawn attempts reject unloaded chunks.
* [x] Spawn attempts reject non-simulated chunks.
* [x] Candidate biome controls spawn table selection.
* [x] Entity caps are per category.
* [x] Local density prevents overpopulation.
* [x] Spawn groups validate member positions.
* [x] Natural despawn works for hostile/ambient entities.
* [x] Protected/non-natural entities are not despawned.
* [x] Transient persistence behavior is either implemented or explicitly documented.
* [x] Debug counters exist.
* [x] Tests pass.
* [x] Benchmarks/profiling data are recorded.
* [x] Documentation is updated.

## Recovery / Idempotence

The implementation should be safe to develop incrementally.

If spawn data fails to load:

* Log diagnostics.
* Disable affected entries.
* Do not crash the server.

If biome lookup fails:

* Reject the spawn attempt.
* Increment a diagnostic counter.

If entity creation fails:

* Abort the group spawn.
* Do not leave partial metadata attached to invalid entities.
* Log if failure is unexpected.

If a new system causes measurable frame/server tick spikes:

* Reduce default attempts.
* Disable local density scan temporarily behind config if needed.
* Record findings in this file.
* Do not add chunk residency as a workaround.

## Future Work

Do not implement these unless explicitly requested later:

* Chunk residency/tickets.
* Entity-driven chunk retention.
* Event-based spawns.
* Structure-specific natural spawn zones.
* Seasonal spawns.
* Weather-driven spawns if weather does not yet exist.
* 3D biome-specific underground spawn tables if 3D biomes are not ready.
* Pack/plugin extension hooks for custom spawn predicates.
* Spawn heatmaps/debug visualization.
* More advanced ecological simulation.
* Spawn suppression regions.
* Per-player difficulty-based spawning.
