# World Simulation

This document defines how Arvane world simulation works.

It covers:

- simulation ownership
- tick phases
- block updates
- scheduled updates
- world events
- liquid simulation
- chunk boundary behavior
- dirty flag integration
- threading and job handoff

This document accompanies [World and Chunks](70_world_and_chunks.md), which defines chunk ownership, chunk lifetime, neighbor access, dirty flags, and the rule that workers compute while the world commits authoritative changes.

---

## Goals

World simulation should provide:

- predictable voxel-world behavior
- bounded per-tick work
- clean interaction between blocks, liquids, lighting, meshing, saving, networking, and entities
- safe multithreaded computation
- explicit cross-chunk propagation
- familiar Minecraft-style liquid behavior
- data-driven rules where possible

World simulation should not become a chaotic collection of recursive updates.

The engine should prefer:

- queues
- dirty flags
- chunk-local processing
- explicit world-controlled commits
- bounded propagation

---

## Core Principles

### World Owns Simulation State

Each world instance owns its simulation state.

This includes:

- live chunk state
- block contents
- fluid contents
- lighting data
- scheduled block updates
- active fluid update queues
- simulation dirty flags
- world-local entity simulation hooks

Worker threads may assist with simulation computation, but workers do not authoritatively mutate live world state.

---

### Workers Compute, World Commits

The world simulation model follows the same ownership rule as chunk meshing, lighting, and generation:

1. the world prepares inputs
2. workers compute from snapshots or guarded read views
3. workers produce command buffers or deltas
4. the world validates and commits results
5. committed changes mark downstream systems dirty

Workers must not directly mutate arbitrary live chunks.

---

### Simulation Is Incremental

The engine must avoid full-world simulation passes.

Simulation is driven by:

- active chunks
- dirty chunks
- block update queues
- fluid update queues
- scheduled tick queues
- chunk activation events
- explicit game/server operations

Inactive chunks should not run continuous simulation.

---

### Simulation Is Bounded

Every simulation phase should have a configurable budget.

Examples:

- maximum block updates per tick
- maximum fluid updates per tick
- maximum scheduled ticks per tick
- maximum cross-chunk propagation events per tick
- maximum simulation jobs dispatched per tick

When a budget is exhausted, remaining work stays queued for a future tick.

The engine should prefer temporary delay over unbounded frame or server stalls.

---

## Simulation Radius

Only chunks inside the active simulation set participate in normal simulation.

A chunk may be:

- loaded but not simulated
- simulated but not visible
- visible but not fully active for all systems

The active simulation set is controlled by world/server policy.

Common sources:

- nearby players
- spawn tickets
- entities requiring simulation
- forced-loaded server regions
- plugin/gameplay tickets

Simulation systems must respect this distinction.

---

## World Tick Structure

A world tick should be organized into stable phases.

Recommended order:

1. input collection
2. authoritative mutation
3. scheduled tick processing
4. block update processing
5. fluid update scheduling
6. async simulation dispatch
7. result collection
8. commit phase
9. dirty system propagation
10. cleanup and unload checks

The exact implementation may split or combine phases, but the ownership boundaries must remain clear.

---

## Phase 1: Input Collection

Collect external simulation inputs.

Examples:

- player block placement
- player block breaking
- item use
- network messages
- commands
- plugin requests
- entity interactions

Input collection should not directly cause deep simulation cascades.

Instead, inputs are converted into explicit world operations.

---

## Phase 2: Authoritative Mutation

Apply direct authoritative world changes.

Examples:

- set block
- remove block
- place fluid source
- remove fluid
- spawn entity
- destroy entity
- modify block entity data

When a block or fluid cell changes, the world must:

- increment the owning section block/geometry version when geometry or block contents changed
- mark the chunk dirty for save if persistent
- mark affected `Geometry` render/mesh state dirty
- enqueue neighbor updates
- enqueue lighting updates if relevant
- enqueue fluid updates if relevant
- emit world events if needed

No system should perform recursive neighbor updates, lighting propagation, meshing, or simulation cascades inline from the edit. The edit queues work; worker jobs compute; the world commits.

---

## Phase 3: Scheduled Ticks

Scheduled ticks are delayed simulation callbacks.

They are used for systems that intentionally update later.

Examples:

- crops
- block decay
- redstone-like logic
- delayed block reactions
- temporary blocks
- game-specific mechanics

A scheduled tick contains at minimum:

```cpp
struct ScheduledTick {
  WorldPos pos;
  TickTime execute_at;
  ScheduledTickKind kind;
};
```

Scheduled ticks should be stored in a time-ordered queue or bucketed timing wheel.

When executed, a scheduled tick may:

* inspect the target block
* emit block changes
* enqueue block updates
* enqueue fluid updates
* schedule another future tick

Scheduled ticks must be bounded per world tick.

---

## Phase 4: Block Updates

Block updates are local reactions to nearby changes.

They are used when a block needs to respond to:

* neighbor changes
* support changes
* fluid contact
* lighting-affecting changes
* placement/removal
* plugin/gameplay events

A block update should contain:

```cpp
struct BlockUpdate {
  WorldPos pos;
  BlockUpdateReason reason;
};
```

Block updates are queued.

They are not recursive.

When processed, the block system may:

* read the target block
* read nearby blocks
* decide whether the block remains valid
* emit block mutations
* enqueue neighbor block updates
* enqueue fluid updates
* enqueue lighting updates

Example:

* remove a support block
* enqueue update for torch above it
* torch update detects missing support
* torch emits a remove-block mutation

---

## Neighbor Update Rules

When a block changes, the world should generally enqueue updates for the six face neighbors:

* +X
* -X
* +Y
* -Y
* +Z
* -Z

Edge and corner neighbor updates should only be used by systems that explicitly require them.

Examples:

* lighting may need broader propagation
* meshing may need adjacent face neighbors
* ambient occlusion may need edge/corner sampling
* fluid simulation usually starts with face neighbors

---

## World Events

World events are notifications produced by committed simulation changes.

Examples:

* block changed
* fluid changed
* chunk became active
* chunk became inactive
* entity entered chunk
* explosion occurred
* block entity created/destroyed

World events are useful for:

* plugins
* networking
* audio/particles
* game rules
* debug tooling

World events must describe committed state, not speculative worker results.

---

# Liquid Simulation

Arvane uses a Minecraft-style level-based liquid simulation.

This is a gameplay-oriented cellular model, not a physically conservative fluid solver.

The goal is familiar voxel liquid behavior:

* sources create flowing liquid
* liquid flows downward first
* liquid spreads sideways when blocked
* flow weakens over distance
* removing sources causes unsupported flow to decay
* updates are localized and queue-driven

This model is chosen because it is:

* familiar to players
* easy to debug
* compact to store
* friendly to networking
* friendly to chunked worlds
* suitable for async compute plus world commit
* easier to mesh than true volume simulation

---

## Liquid Terminology

### Fluid Type

A fluid type defines the rules for a kind of liquid.

Examples:

* water
* lava
* acid
* oil
* honey

Fluid definitions also own their timing. A water-like fluid may use a smaller
`flow_interval_ticks` than a lava-like fluid, while both continue to run under
the same global world tick rate.

Example:

```cpp
struct FluidDefinition {
  FluidTypeID type;
  u8 max_level;
  u8 horizontal_decay;
  u8 vertical_flow_level;
  u32 flow_interval_ticks;
};
```

`flow_interval_ticks` controls update cadence only. It must not cause a fluid to
move multiple blocks in one tick or make rendering responsible for liquid speed.

### Fluid Cell

A fluid cell is the liquid state stored at a block position.

A block position may contain:

* no fluid
* a fluid source
* a flowing fluid cell

### Source Cell

A source cell is a full-strength fluid cell that does not decay by itself.

Examples:

* placed water source
* generated lake/ocean water
* source created by source-creation rules

### Flowing Cell

A flowing cell is non-source liquid produced by spread.

Flowing cells weaken over horizontal distance and may decay if unsupported.

---

## Fluid Storage

A chunk section may store compact fluid state separately from block state.

Recommended representation:

```cpp
struct FluidCell {
  FluidTypeID type;
  u8 level;
  FluidFlags flags;
};
```

Where:

* `type` identifies the fluid definition
* `level == 0` means empty
* `level > 0` means fluid exists
* `level == max_level` usually means full/source-strength fluid
* `flags` stores source/settled/falling metadata as needed

Recommended flags:

```cpp
enum class FluidFlags : u8 {
  None    = 0,
  Source  = 1 << 0,
  Falling = 1 << 1,
  Settled = 1 << 2,
};
```

The exact bit layout may change, but the concepts should remain.

---

## Fluid Definitions

Fluid behavior should be data-driven.

Example YAML:

```yaml
id: arvane:water

max_level: 8
horizontal_decay: 1
vertical_flow_level: 8

tick_interval: 5
max_horizontal_distance: 7

can_create_sources: true
source_creation_rule: two_adjacent_sources

can_flow_down: true
can_flow_sideways: true
can_replace:
  - arvane:air

blocks_movement: false
transparent: true

mesh:
  render_layer: transparent
  surface_height_from_level: true

lighting:
  reduces_sky_light: true
  sky_light_reduction: 1
  reduces_block_light: false
```

Lava may use the same algorithm with different values:

```yaml
id: arvane:lava

max_level: 8
horizontal_decay: 2
vertical_flow_level: 8

tick_interval: 20
max_horizontal_distance: 3

can_create_sources: false

can_flow_down: true
can_flow_sideways: true
can_replace:
  - arvane:air

blocks_movement: true
transparent: false
```

The engine should not hardcode water-specific rules into the simulation core.

---

## Fluid Level Semantics

Fluid level represents flow strength and render height.

For a fluid with `max_level = 8`:

* `0` = empty
* `1` = weakest flow
* `2..7` = flowing liquid
* `8` = full/source-strength liquid

Important:

Fluid levels are not exact physical volume.

They are gameplay state.

This prevents the engine from accidentally becoming a pressure solver.

---

## Source vs Flowing State

A source cell is identified by the `Source` flag.

A cell may have:

```text
level = max_level
Source flag set
```

A falling fluid cell may also have high level but should not necessarily become a permanent source.

This distinction matters.

Example:

* water falling down a cliff may use level 8 visually
* but it is not automatically a source block
* if the source above is removed, the falling column should eventually decay

---

## Fluid Update Queue

Fluid simulation is driven by active updates.

Recommended structure:

```cpp
struct FluidUpdate {
  WorldPos pos;
  FluidUpdateReason reason;
};
```

Reasons may include:

```cpp
enum class FluidUpdateReason : u8 {
  FluidAdded,
  FluidRemoved,
  NeighborChanged,
  BlockChanged,
  ChunkActivated,
  SourceCheck,
};
```

Fluid updates may be stored:

* globally per world
* per active chunk
* in chunk-local queues with a world-level scheduler

The system should avoid duplicate work where practical.

A position already queued for fluid update does not need to be queued again unless priority,
reason, or an earlier scheduled tick matters.

Scheduled fluid updates include:

```cpp
struct FluidUpdate {
  BlockPos pos;
  FluidUpdateReason reason;
  TickIndex execute_at;
};
```

Only due updates (`execute_at <= current_tick`) are drained into a bounded fluid
simulation request. Future updates remain queued. Repeated work for the same cell
is coalesced where practical, and stale superseded events are ignored when drained.

---

## Fluid Activation

A fluid cell becomes active when:

* fluid is placed
* fluid is removed
* a neighboring block changes
* a neighboring fluid changes
* a chunk becomes active
* a source is created or removed
* a block is broken near fluid
* a fluid delta crosses a chunk boundary

Stable fluid should not continuously update forever.

When a fluid cell changes, the next update for that cell and affected neighbors is
scheduled at:

```
current_tick + fluid_definition.flow_interval_ticks
```

This means water/lava-style differences are data-driven. Water can flow every few
ticks, lava can flow every larger number of ticks, and neither requires changing
global TPS.

---

## Settled Fluids

A fluid cell may be marked settled when:

* it cannot flow down
* it cannot spread sideways
* its level is supported
* its neighbors are stable
* no nearby change requires recomputation

Settled fluid cells are skipped until disturbed.

When a neighboring block or fluid changes, settled cells near the change must be reactivated.

---

# Fluid Update Algorithm

The fluid algorithm is local and priority-based.

For each active fluid cell:

1. validate current state
2. handle source creation/removal rules
3. try downward flow
4. try horizontal spread
5. decay unsupported flow
6. mark settled if stable
7. emit updates for changed cells and neighbors

---

## Step 1: Validate Cell

When processing a fluid update:

* if the chunk is not loaded, defer or discard according to policy
* if the chunk is not active, defer unless forced
* read the current block at the position
* read the current fluid cell at the position
* verify the block can contain that fluid

If the block no longer allows fluid:

* remove the fluid cell
* enqueue neighboring fluid updates
* mark mesh/save dirty

Fluid propagation remains discrete. Processing one fluid update computes one local
state transition from the current/snapshot state; it does not chase the resulting
neighbor work immediately through multiple blocks in the same tick.

Example:

A solid block placed into water removes or displaces that water according to game rules.

---

## Step 2: Determine Support

A flowing cell is supported if it has a valid source path.

Common support sources:

* the cell itself is a source
* the cell has fluid above flowing downward into it
* the cell has a neighboring fluid cell with higher level
* source creation rules produce a new source

If a flowing cell is unsupported, it should decay.

---

## Step 3: Downward Flow

Downward flow has priority over sideways spread.

Given:

* current position `pos`
* below position `pos + (0, -1, 0)`

If below can accept fluid:

* create or strengthen fluid below
* mark below as falling
* usually set below level to `vertical_flow_level`
* enqueue below and its neighbors
* enqueue current cell for recheck if needed

Minecraft-style behavior generally allows vertical fluid to remain strong while falling.

Recommended rule:

```text
if below can accept fluid:
    below.level = max(below.level, fluid.vertical_flow_level)
    below.type = current.type
    below.flags.add(Falling)
```

This creates recognizable waterfalls.

---

## Step 4: Sideways Spread

If fluid cannot flow downward, it attempts horizontal spread.

Horizontal directions:

* +X
* -X
* +Z
* -Z

A flowing cell spreads to valid neighboring cells with reduced level.

Recommended rule:

```text
new_level = current.level - horizontal_decay
```

If `new_level <= 0`, no sideways spread occurs.

A source cell uses `max_level` as its current level.

Example with water:

```text
source level = 8
neighbor = 7
next = 6
next = 5
...
```

A lower-level cell should be replaced or strengthened if the new flow level is greater than the existing level.

---

## Step 5: Flow Direction Selection

The simple version spreads to all valid horizontal neighbors.

A better Minecraft-like version prefers neighbors that can eventually flow downward.

Recommended behavior:

1. inspect each horizontal neighbor
2. determine whether that neighbor can flow downward soon
3. prefer paths leading to downward flow
4. otherwise spread evenly to all valid lower/empty neighbors

This prevents water from spreading equally in all directions when a nearby ledge exists.

The search depth should be bounded by `max_horizontal_distance`.

For water, this may be 7.

For lava, this may be 3.

---

## Step 6: Unsupported Flow Decay

A non-source flowing cell should decay if it no longer has support.

Examples:

* source was removed
* higher-level neighbor disappeared
* block layout changed
* chunk boundary update invalidated flow

Decay rule:

```text
if not source and not supported:
    level -= horizontal_decay
```

If level reaches 0:

* remove fluid cell
* enqueue neighbors
* mark mesh/save dirty

The engine may also recompute the ideal level from neighbors instead of subtracting gradually.

Recommended Minecraft-style rule:

```text
ideal_level = best_neighbor_level - horizontal_decay
if has_fluid_above:
    ideal_level = vertical_flow_level

if not source:
    level = clamp(ideal_level, 0, max_level)
```

This converges faster and avoids long decay chains.

---

## Step 7: Source Creation

Some fluids may create new sources.

For water-like fluids:

```yaml
can_create_sources: true
source_creation_rule: two_adjacent_sources
```

Recommended rule:

A non-source water cell becomes a source if:

* the block below is solid or contains source-compatible fluid
* at least two horizontal neighbors are source cells of the same fluid type
* the current block can contain fluid

If true:

```text
level = max_level
Source flag = true
Falling flag = false
```

Lava-like fluids may disable source creation.

---

## Step 8: Mark Settled

After processing:

A cell may be marked settled if:

* it is a source with stable surroundings
* or it is flowing but has the expected level
* it cannot flow downward
* it has no stronger update to push
* all relevant neighbors are equal or weaker
* no pending source/decay condition exists

Settled cells are not processed again until disturbed.

---

# Fluid Commit Model

Fluid simulation may produce command buffers.

```cpp
struct FluidDelta {
  WorldPos pos;
  FluidCell new_cell;
  FluidDeltaReason reason;
};
```

Workers may compute `FluidDelta` objects from snapshots.

The world commit phase applies them.

When applying a fluid delta:

1. resolve world position to chunk/local position
2. verify chunk is loaded and active enough
3. verify version assumptions if present
4. compare old fluid cell to new fluid cell
5. apply if changed
6. increment the relevant section block/geometry version
7. mark dirty systems
8. enqueue follow-up fluid updates at `current_tick + flow_interval_ticks`

Fluid deltas are committed by world simulation. They mark chunk sections dirty and
allow the normal async meshing and lighting paths to react later. Fluid mutation
must not synchronously rebuild meshes or solve lighting.

---

## Fluid Delta Conflicts

Multiple deltas may target the same cell.

The world must resolve conflicts deterministically.

Recommended priority:

1. explicit player/server mutation
2. block replacement/removal result
3. source cell creation
4. stronger same-type fluid
5. weaker same-type decay
6. fluid removal

For different fluid types, behavior should be fluid-definition driven.

Examples:

* water + lava = stone/cobblestone/obsidian-like block
* acid + water = diluted acid
* oil + fire = burning liquid

The base engine should provide the mechanism, not hardcode every reaction.

---

# Cross-Chunk Fluid Rules

Fluids often cross chunk boundaries.

This must be explicit and safe.

## Same Rule as World Model

Workers do not directly mutate neighboring live chunks.

Cross-chunk flow is represented as:

* world-position deltas
* boundary update events
* queued neighbor fluid updates

The commit phase owns the actual mutation.

---

## Boundary Updates

When a fluid change occurs at a chunk boundary, enqueue fluid updates for neighboring chunk positions.

Example:

If local `x == 15`, then `x + 1` is in the neighboring chunk.

The world should enqueue:

* changed cell
* six face neighbors
* affected boundary cells in adjacent chunks

If the neighbor chunk is not loaded:

* do not force-load it by default
* store a deferred boundary update if policy requires
* or allow flow to continue when the neighbor loads/activates

The exact policy is server/game configurable.

---

## Loaded but Inactive Neighbor

If the neighbor chunk is loaded but outside simulation radius:

* fluid should not freely simulate deep into it
* boundary state may be updated only if policy allows
* otherwise defer until activation

Recommended default:

* active chunks may push fluid one cell into loaded inactive chunks only during commit if necessary for boundary consistency
* further propagation waits until the neighbor becomes active

This prevents simulation leaks beyond the active radius.

---

# Chunk Activation and Fluid Rescan

When a chunk becomes active, it may need a light fluid rescan.

Do not scan the entire world.

Recommended activation work:

* check chunk boundary cells
* check known fluid cells
* check cells adjacent to non-empty fluid
* process deferred boundary fluid events
* optionally scan compact fluid storage if present

If the chunk stores a sparse list of non-empty fluid cells, activation is cheap.

---

# Fluid Dirty Flags

Fluid changes must mark the owning chunk dirty.

Relevant flags:

```cpp
enum class ChunkDirtyFlags : u32 {
  None             = 0,
  NeedsSave        = 1 << 0,
  Geometry         = 1 << 1,
  Lighting         = 1 << 2,
  LightMesh        = 1 << 3,
  NeedsFluidStep   = 1 << 4,
};
```

Fluid changes usually set:

* `NeedsSave`
* `Geometry`

They may set:

* `Lighting`

They should set `Lighting` when:

* the fluid affects transparency/light attenuation
* the fluid replaced a light-emitting block
* the fluid changed sky-light propagation
* the fluid type emits light

---

# Fluid and Meshing

Fluid meshing is derived from fluid state.

Fluid cells should generally render separately from opaque terrain.

The mesher should consider:

* fluid type
* fluid level
* source flag
* falling flag
* neighboring fluid cells
* neighboring solid blocks
* transparency rules

---

## Fluid Surface Height

For non-falling fluid, render height may be derived from level.

Example:

```text
height = level / max_level
```

For source cells:

```text
height = 1.0
```

For falling cells:

```text
height = 1.0
```

This gives waterfalls full vertical faces while still allowing shallow flowing surfaces.

---

## Fluid Face Visibility

Fluid faces should be emitted when adjacent to:

* air
* transparent non-fluid blocks
* different fluid type
* lower-level fluid where a surface edge is visible

Fluid faces should generally not be emitted between two full same-type fluid cells.

---

## Fluid Render Layer

Water-like fluids usually render in a transparent pass.

Lava-like fluids may render opaque or translucent depending on definition.

The material/render layer should come from the fluid definition.

---

# Fluid and Lighting

Fluid may affect lighting.

Examples:

* water reduces sky light slightly
* lava emits block light
* opaque fluids block more light
* transparent fluids allow partial propagation

Lighting behavior should be data-driven.

Fluid changes must enqueue relight work if the fluid definition affects light.

---

# Fluid and Blocks

Blocks define whether they can contain fluid.

Examples:

```yaml
id: arvane:air
fluid_behavior: replaceable

id: arvane:stone
fluid_behavior: solid

id: arvane:tall_grass
fluid_behavior: replaceable_by_fluid

id: arvane:waterlogged_stairs
fluid_behavior: contains_fluid
```

Recommended block-fluid categories:

* `solid`
* `replaceable`
* `fluid_container`
* `fluid_permeable`
* `destroys_fluid`
* `reacts_with_fluid`

The engine should not assume only air can contain fluid.

This leaves room for:

* waterlogging
* grates
* plants
* partial blocks
* modded block behavior

---

# Fluid Reactions

When different fluids interact, reactions are resolved during commit.

Example reaction definition:

```yaml
fluid_reactions:
  - a: arvane:water
    b: arvane:lava
    result_block: arvane:stone
    consumes_a: true
    consumes_b: true
```

The simulation core should expose reaction hooks.

It should not hardcode Minecraft-specific block outcomes in the engine layer.

---

# Fluid and Entities

Entity-fluid interaction is not owned by the fluid simulator, but the fluid state should be queryable by entity systems.

Possible effects:

* swimming
* buoyancy
* drag
* current forces
* drowning checks
* lava damage
* splash particles

Entity systems should sample fluid state through world query interfaces.

---

# Natural Entity Spawning

Natural entity spawning is a server-authoritative world simulation system.

The server runs natural spawn commits after it updates the active simulation
set for loaded chunks and before ordinary world/entity ticking for the frame.
Candidate planning may run on worker threads from owner-thread snapshots. The
system samples candidate positions around online players, resolves the
candidate biome from the active dimension terrain profile, and looks up the
biome's spawn table for the sampled category.

Natural spawning must not create chunk residency pressure. A candidate is valid
only when its target chunk section is already loaded and either active or
horizontally near a player in the spawn radius. That horizontal fallback lets
surface spawns continue while a player is flying above terrain without marking
every vertical section active. The system may query deterministic terrain
profiles to identify the biome and surface height, but it does not request
generation, load missing chunks, or keep chunks alive.

Work is bounded by:

- `interval_ticks`
- attempts per player and category
- per-player and per-world success budgets
- per-category caps
- local density caps

Spawned entities are created through the world entity API using the authored
entity type's components, replication defaults, and persistence defaults. Each
natural entity also receives a natural-spawn marker containing its spawn
category, source entity type, spawn tick, and transient flag.

Natural despawning applies only to entities with that natural-spawn marker.
Player-owned entities are ignored. Persistent natural entities that are not
marked transient are kept by despawn passes. Transient natural entities are
omitted from entity persistence even when the underlying entity type would
normally be persistent.

Example configuration:

```yaml
natural_spawning:
  enabled: true
  interval_ticks: 10
  min_distance_from_player_blocks: 24
  max_distance_from_player_blocks: 96
  attempts_per_player_per_category: 24
  max_successes_per_player_per_tick: 6
  max_successes_per_world_per_tick: 48
  local_density_radius_blocks: 32
  caps:
    passive:
      per_player: 48
      per_world_soft: 1024
      local_max: 24
  despawning:
    passive:
      natural_despawn: false
      soft_distance_blocks: 96
      hard_distance_blocks: 160
      min_lifetime_ticks: 600
      save_transient: true
```

The base passive pig tables intentionally avoid a light-level condition for now
so starter passive mobs are not blocked by newly generated or stale saved
lighting. They require a standing surface tagged `arvane:spawn/grass_like`;
replaceable non-fluid body/head blocks such as short grass do not block the
spawn.

The same category keys used by biome spawn tables are used by `caps` and
`despawning`: `passive`, `hostile`, `ambient`, `water`, `underground`, and
`special`.

---

# Fluid and Networking

Fluid changes are replicated as world state changes.

Recommended replication unit:

* position
* fluid type
* level
* flags

Fluid updates should be batched per chunk section where practical.

Clients should not authoritatively simulate world fluid unless explicitly using prediction for presentation only.

The server remains authoritative.

---

# Fluid and Saving

Persistent chunks should save fluid state.

A chunk save should include:

* non-empty fluid cells
* fluid type
* level
* flags
* any required fluid metadata

If fluid state is sparse, it may be serialized sparsely.

If dense, it may be serialized as a compact chunk-local array.

The save format should not persist transient queue state unless needed for correctness.

On load, the chunk may enqueue activation fluid checks instead.

---

# Async Fluid Computation

Fluid simulation can be split into compute and commit phases.

## Worker Input

A fluid worker task may receive:

* source chunk fluid snapshot
* source chunk block snapshot
* neighbor boundary fluid data
* neighbor boundary block data
* fluid definitions
* queued update positions
* source block/geometry version

## Worker Output

A fluid worker task returns:

* source chunk coordinate
* source block/geometry version
* fluid deltas
* boundary update requests
* dirty hints

## Publish Rule

When results return:

* if the chunk no longer exists, discard
* if the source block/geometry version changed and the result cannot be safely merged, discard or partially revalidate
* otherwise commit deltas through the world

Fluid work should be more conservative than meshing because it mutates simulation state.

If correctness is uncertain, discard stale fluid results and reschedule.

---

# Recommended Initial Implementation

The first implementation should be simple and reliable.

Recommended v1:

* store fluid per block as compact `FluidCell`
* support one fluid type: water
* use `max_level = 8`
* source cells have `Source` flag
* downward flow first
* horizontal spread with level decay
* no pressure simulation
* no diagonal flow
* no infinite-source oceans beyond generated source cells
* no waterlogging initially unless already defined elsewhere
* queue-driven updates
* world commits all changes
* boundary updates are explicit
* stable cells settle

Recommended v1 water behavior:

```text
source water:
  level = 8
  source = true

flowing water:
  level = 1..7
  source = false

falling water:
  level = 8
  source = false
  falling = true
```

Horizontal spread:

```text
new_level = current_level - 1
```

Flow stops when:

```text
new_level <= 0
```

Source creation:

```text
if two horizontal source neighbors and valid support below:
    become source
```

---

# Pseudocode: Fluid Update

```cpp
void process_fluid_update(World& world, WorldPos pos) {
  BlockState block = world.get_block(pos);
  FluidCell fluid = world.get_fluid(pos);

  if (fluid.empty()) {
    return;
  }

  FluidDef def = world.fluid_defs().get(fluid.type);

  if (!block_can_contain_fluid(block, fluid.type)) {
    world.emit_fluid_delta(pos, FluidCell::empty());
    enqueue_fluid_neighbors(world, pos);
    return;
  }

  if (try_create_source(world, pos, fluid, def)) {
    world.emit_fluid_delta(pos, make_source(fluid.type, def.max_level));
    enqueue_fluid_neighbors(world, pos);
    return;
  }

  if (try_flow_down(world, pos, fluid, def)) {
    enqueue_fluid_neighbors(world, pos);
    return;
  }

  bool spread = try_spread_horizontal(world, pos, fluid, def);

  FluidCell ideal = compute_ideal_fluid_state(world, pos, fluid, def);

  if (!fluid.is_source() && ideal != fluid) {
    world.emit_fluid_delta(pos, ideal);
    enqueue_fluid_neighbors(world, pos);
    return;
  }

  if (!spread) {
    world.mark_fluid_settled(pos);
  }
}
```

This pseudocode describes behavior, not final API.

Actual implementation should respect snapshot/worker/commit boundaries.

---

# Pseudocode: Horizontal Spread

```cpp
bool try_spread_horizontal(World& world, WorldPos pos, FluidCell fluid, FluidDef def) {
  if (fluid.level <= def.horizontal_decay) {
    return false;
  }

  u8 new_level = fluid.level - def.horizontal_decay;
  bool changed = false;

  for (Direction dir : horizontal_dirs()) {
    WorldPos target = pos + dir.offset();

    if (!can_fluid_enter(world, target, fluid.type)) {
      continue;
    }

    FluidCell existing = world.get_fluid(target);

    if (existing.empty() || same_type_weaker(existing, fluid.type, new_level)) {
      world.emit_fluid_delta(target, make_flowing(fluid.type, new_level));
      changed = true;
    }
  }

  return changed;
}
```

---

# Pseudocode: Downward Flow

```cpp
bool try_flow_down(World& world, WorldPos pos, FluidCell fluid, FluidDef def) {
  WorldPos below = pos + WorldOffset{0, -1, 0};

  if (!can_fluid_enter(world, below, fluid.type)) {
    return false;
  }

  FluidCell existing = world.get_fluid(below);

  if (existing.empty() || same_type_weaker(existing, fluid.type, def.vertical_flow_level)) {
    FluidCell falling;
    falling.type = fluid.type;
    falling.level = def.vertical_flow_level;
    falling.flags = FluidFlags::Falling;

    world.emit_fluid_delta(below, falling);
    return true;
  }

  return false;
}
```

---

# Pseudocode: Source Creation

```cpp
bool try_create_source(World& world, WorldPos pos, FluidCell fluid, FluidDef def) {
  if (!def.can_create_sources) {
    return false;
  }

  if (fluid.is_source()) {
    return false;
  }

  if (!has_valid_source_support_below(world, pos, fluid.type)) {
    return false;
  }

  int source_neighbors = 0;

  for (Direction dir : horizontal_dirs()) {
    FluidCell neighbor = world.get_fluid(pos + dir.offset());

    if (neighbor.type == fluid.type && neighbor.is_source()) {
      source_neighbors++;
    }
  }

  return source_neighbors >= 2;
}
```

---

# Work Budgeting

Fluid simulation must be budgeted.

Recommended configurable values:

```yaml
simulation:
  max_block_updates_per_tick: 4096
  max_scheduled_ticks_per_tick: 4096
  max_fluid_updates_per_tick: 8192
  max_fluid_chunk_jobs_per_tick: 64
  max_cross_chunk_fluid_events_per_tick: 4096
```

These values are examples only.

They should live in config, not as hardcoded constants.

---

# Simulation Config

World simulation settings should be configurable per server/world where appropriate.

Example:

```yaml
world_simulation:
  tick_rate: 60

  block_updates:
    max_updates_per_tick: 4096

  scheduled_ticks:
    max_updates_per_tick: 4096

  fluids:
    enabled: true
    max_updates_per_tick: 8192
    max_chunk_jobs_per_tick: 64
    settle_enabled: true
    allow_cross_chunk_flow: true
    allow_flow_into_inactive_chunks: false
    water:
      flow_interval_ticks: 10
    lava:
      flow_interval_ticks: 30
```

---

# Interaction With Chunk Versions

Committed simulation changes update the relevant section versions.

This includes:

* block changes increment `block_version`
* fluid geometry changes increment `block_version`
* lighting changes increment `light_version`
* block entity changes

Derived tasks must validate against the correct versions. Mesh results reject stale `block_version` but may temporarily accept stale `light_version`.

For fluid jobs, stale results should usually be discarded unless the system can safely revalidate each delta.

---

# Invariants

The following invariants should hold:

* world simulation only runs in active chunks
* workers do not authoritatively mutate chunks
* all simulation mutation is committed by the world
* block updates are queued, not recursive
* fluid updates are queued, not global scans
* liquid flow is Minecraft-style and level-based
* fluid levels are gameplay state, not exact physical volume
* downward fluid flow is prioritized over horizontal spread
* horizontal fluid spread decays by fluid definition
* source creation is fluid-definition driven
* cross-chunk fluid movement uses explicit world-position deltas or boundary events
* fluid changes mark mesh/save dirty
* fluid lighting effects mark relight dirty
* stable fluid cells may settle and stop updating
* disturbed fluid cells must reactivate

---

# Non-Goals

The base simulation system does not attempt to provide:

* physically accurate fluid pressure
* fully conservative liquid volume
* Navier-Stokes simulation
* global fluid equilibrium
* infinite-world background simulation
* recursive immediate update cascades
* worker-owned chunk mutation

These may be explored later as specialized systems, but they are not part of the default world simulation model.

---

# Summary

Arvane world simulation is queue-driven, chunk-scoped, and world-committed.

The simulation model is built around:

* fixed world ticks
* bounded update queues
* scheduled ticks
* explicit dirty propagation
* controlled worker computation
* central world commits

Liquids use a Minecraft-style level-based model:

* source cells
* flowing cells
* downward-first behavior
* horizontal decay
* optional source creation
* queued updates
* settled-state optimization
* explicit cross-chunk propagation

This gives Arvane familiar voxel liquid behavior while preserving the engine’s larger architecture: safe chunk ownership, multithreaded computation, deterministic-enough commits, and scalable active-region simulation.
