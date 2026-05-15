# Worlds and Chunks

This document defines the world model for the engine.

It establishes how the world is spatially partitioned, how chunks are addressed and accessed, what data belongs to a chunk, how chunk lifetime works, and what rules govern loading, unloading, generation, and off-thread work.

This is one of the most foundational engine documents. Many other systems depend on these decisions, including world generation, lighting, meshing, persistence, entity simulation, networking, pathfinding, and job scheduling.

---

## Goals

The world and chunk model should provide:

- a clear and stable spatial structure for infinite voxel worlds
- predictable chunk ownership and lifetime rules
- efficient access to local and neighboring voxel data
- safe multithreaded generation and processing
- a clean separation between loaded world state and persisted world state
- support for large worlds without assuming the entire world is memory-resident

---

## Core World Model

The world is divided into horizontal chunk columns, and each column contains fixed-size vertical chunk sections.

A **chunk** is a horizontal `(cx, cz)` column. It is the primary residency, streaming-interest, and generation coordination unit.

A **chunk section** is a 16 x 16 x 16 voxel storage and processing unit addressed by `(cx, cy, cz)`.

Chunks and chunk sections are addressed in chunk space, while blocks and entities may exist in world space.

The world is conceptually infinite in horizontal extent. Vertical behavior may be configurable at the game layer, but the engine world model should not assume a small hardcoded height limit.

The engine should treat the world as a collection of chunk columns and chunk sections rather than as one monolithic voxel array.

---

## Chunk Size

### Standard chunk dimensions

The standard chunk section size is:

- **16 x 16 x 16 blocks**

This is the fundamental storage and processing unit.

Reasoning:

- 16-wide horizontal dimensions are familiar and practical
- 16³ is small enough for efficient meshing, lighting, and simulation jobs
- vertical sectioning avoids the cost of treating an entire tall column as one allocation/update unit

### Chunk columns

A **chunk column** is the set of vertically stacked chunk sections sharing the same `(cx, cz)` coordinates.

A column owns the vertical section slots for one `(cx, cz)` coordinate and is used for:

- streaming coordination
- world generation jobs
- high-level visibility/load tracking
- biome and heightmap style data that is naturally column-oriented

The engine should distinguish between:

- **chunk section**: one 16 x 16 x 16 voxel storage unit
- **chunk / chunk column**: all chunk sections at a given horizontal chunk coordinate

Engine code should use `ChunkSection` or section-explicit names for section-sized work and reserve `Chunk` for the column object.

---

## World Height Settings

Each world owns explicit vertical chunk-section bounds:

- `min_chunk_y`
- `max_chunk_y`

Both bounds are inclusive, and negative chunk-section Y values are valid.

The section count for a world is:

- `section_count = max_chunk_y - min_chunk_y + 1`

The O(1) index for a section inside a chunk column is:

- `section_index = section_y - min_chunk_y`

A section Y outside the configured bounds is outside that world's live chunk-column storage. Generated worlds derive these bounds from their configured vertical block range unless the server/world provides explicit height settings. Save files may retain `min_chunk_y` / `max_chunk_y` as compatibility mirrors, but generated-world load/save paths treat `generation_config.min_y` / `generation_config.max_y` as the authoritative source and recompute the chunk-section bounds from those block-Y values. Non-generated worlds may use a narrow default, such as section `0..0`, until configured otherwise.

---

## Region Organization

### Purpose of regions

Regions exist to group chunks into larger units for:

- disk storage
- streaming metadata
- coarse-grained lookup
- batching persistence work
- reducing filesystem fragmentation

Regions are a persistence and indexing concern, not the primary simulation unit.

### Standard region size

A region groups chunk columns in the horizontal plane.

Recommended standard:

- **32 x 32 chunk columns per region**

That means a region covers:

- `32 * 16 = 512` blocks along X
- `32 * 16 = 512` blocks along Z

Vertical chunk sections remain individually addressed within those columns.

### Region addressing

Region coordinates are derived from chunk-column coordinates:

- `region_x = floor_div(chunk_x, 32)`
- `region_z = floor_div(chunk_z, 32)`

Local coordinates within a region:

- `local_chunk_x = floor_mod(chunk_x, 32)`
- `local_chunk_z = floor_mod(chunk_z, 32)`

Regions should not own live simulation state directly. They are best treated as:

- persistence containers
- indexing/grouping structures
- coarse metadata carriers

---

## Coordinate Spaces

The engine uses several coordinate spaces.

### Block world coordinates

World block coordinates are integer voxel coordinates in the global world.

Example:

- `(wx, wy, wz)`

These identify a specific block position in the world.

### Chunk coordinates

Chunk coordinates identify a horizontal chunk column.

Example:

- `(cx, cz)`

### Chunk section coordinates

Chunk section coordinates identify the 16 x 16 x 16 section containing a block.

Example:

- `(cx, cy, cz)`

Each component refers to a 16-block section in that axis.

### Local block coordinates

Local coordinates identify a block within a chunk section.

Example:

- `(lx, ly, lz)`

Each local coordinate is in the range:

- `0..15`

### Region coordinates

A region is addressed by:

- `(rx, rz)`

---

## World Coordinate Conversions

All coordinate conversions must use mathematically correct floor-based division and modulo semantics.

This is especially important for negative coordinates.

The engine must never rely on truncating integer division semantics when converting world coordinates to chunk coordinates.

### World block to chunk section coordinate

For a chunk size of 16:

- `cx = floor_div(wx, 16)`
- `cy = floor_div(wy, 16)`
- `cz = floor_div(wz, 16)`

### World block to local block coordinate

- `lx = floor_mod(wx, 16)`
- `ly = floor_mod(wy, 16)`
- `lz = floor_mod(wz, 16)`

Result is always in:

- `0..15`

### Chunk section and local back to world

- `wx = cx * 16 + lx`
- `wy = cy * 16 + ly`
- `wz = cz * 16 + lz`

### Region from chunk column

- `rx = floor_div(cx, 32)`
- `rz = floor_div(cz, 32)`

### Local chunk-within-region

- `rcx = floor_mod(cx, 32)`
- `rcz = floor_mod(cz, 32)`

### Required helpers

The engine should provide canonical helpers for:

- world block -> chunk section position
- chunk section -> chunk column position
- world block -> local block position
- chunk + local -> world block
- chunk column -> region
- chunk section -> neighboring chunk section coordinates

These helpers should live in core world/chunk utilities so every subsystem uses identical logic.

---

## Chunk Contents

This section is intentionally explicit.

A chunk owns only data that is spatially local to that chunk section and whose lifetime is naturally tied to the chunk section being loaded.

## Data that belongs to a chunk section

A chunk section should own:

- 1D array block storage for its 16 x 16 x 16 voxel contents
- block state/storage metadata for those voxels
- local lighting data for those voxels
- local dirty flags related to remeshing, relighting, serialization, and generation stages
- per-section caches that are derived only from that section's voxel contents and immediate neighbor observations
- section-local bookkeeping needed for thread-safe state transitions

Examples:

- raw block array
- sky light values for each block
- block light values for each block
- pending mesh dirty flags
- generation state
- save dirty flag
- version/counter for mutation tracking

### Data that may belong to a chunk section if needed

Depending on final design, a chunk section may also own:

- fluid cells or compact per-block fluid state
- local block entity storage for block entities whose position lies inside the chunk
- local scheduled block updates scoped to positions inside the chunk
- local navigation or collision caches derived from chunk data

These are acceptable only if their ownership is clearly local to the chunk section.

## Data that does not belong to a chunk section

A chunk section should not directly own:

- the global world registry
- the scheduler/executor
- global entity registries
- player/session state
- cross-world systems
- persistence backends
- renderer-owned GPU resources as authoritative state
- long-lived references to arbitrary other chunk columns or sections
- large global caches that span many chunks

A chunk section may be associated with render resources, but authoritative ownership of GPU objects should live in rendering systems, not inside the chunk’s core world state object.

---

## Column-Level Data

Some data is naturally column-oriented rather than section-oriented.

A chunk column may own or expose:

- biome layout
- heightmap-like summaries
- surface summaries
- generation metadata that applies to the whole vertical stack
- load intent state for the column
- per-column tickets/references from players, systems, or simulation rules

Column-level data should not replace chunk-section ownership for actual voxel contents.

---

## Live Chunk Storage

The live world store is organized as:

```text
ChunkCoord (cx, cz) -> Chunk column -> vector slots of ChunkSection
```

The column vector is sized from the world's height settings. Looking up a loaded section is:

1. map lookup by `(cx, cz)`
2. bounds check for `cy`
3. vector index `cy - min_chunk_y`

This avoids treating section lookup as a three-dimensional map search while preserving section-sized ownership for block, light, fluid, dirty, lifecycle, and version data.

---

## Neighbor Access

Neighbor access is critical and must be tightly defined.

### Principle

Chunk sections do not permanently own neighboring sections, and chunk columns do not permanently own neighboring columns.

Instead, neighboring data is accessed through world-level query interfaces or short-lived views.

This prevents hidden ownership graphs, dangling references, and unsafe cross-thread mutation.

### Allowed neighbor model

A chunk-related system may request:

- the six face neighbors
- edge/corner neighbors if the algorithm truly needs them
- column-neighbor metadata
- read-only snapshots or guarded views of nearby chunks

Neighbor access must go through controlled APIs.

### Avoid

Do not embed raw owning pointers from one chunk to its neighbors as permanent authoritative links.

Reasons:

- neighbor lifetime changes constantly
- unloads become harder and more error-prone
- thread-safety becomes less obvious
- ownership becomes cyclic and fragile

### Recommended access style

Use world or chunk-manager APIs such as:

- `try_get_chunk(coord)`
- `try_get_chunk_read(coord)`
- `try_get_chunk_write(coord)`
- `gather_neighbor_reads(coord, mask)`
- `sample_block(world_pos)`
- `sample_block_if_loaded(world_pos)`

The exact API may vary, but the ownership model should remain the same:

- the world/chunk manager owns live chunk-column and chunk-section instances
- systems obtain temporary access
- neighbor references are not stored long-term unless they are weak handles or revalidated handles

---

## Safe Neighbor Access Rules

### Read access

Reading from neighboring chunks is allowed when:

- access is obtained through a valid world/chunk interface
- the chunk is confirmed loaded and readable
- the algorithm does not retain the raw access past the allowed scope

Typical safe read cases:

- meshing
- lighting propagation reads
- collision sampling
- generation seam checks
- surface classification
- visibility/occlusion checks

### Write access

Writing into neighboring chunks is more restrictive.

Direct multi-chunk mutation should be avoided unless the operation is explicitly designed for it.

Preferred approaches:

- mutate the owning chunk directly only when it is the operation target
- enqueue boundary updates for neighbors rather than mutating them inline
- use command buffers / deferred mutation queues for cross-chunk effects
- perform multi-chunk write operations only under explicit coordination rules

Example:

If a block change at a chunk edge affects lighting in a neighbor, the system should generally:

- mutate the source chunk
- mark neighboring chunks dirty or enqueue relight work
- let the neighbor process its own local update through controlled work execution

This reduces lock contention and ownership ambiguity.

---

## Chunk Lifetime

Chunks move through a lifecycle.

A chunk should not be thought of as either “exists” or “does not exist” only. It should have well-defined states.

### Recommended chunk lifecycle states

A chunk section may pass through states similar to:

1. **Unloaded**
2. **Requested**
3. **Loading**
4. **Generating**
5. **Populating/Finalizing**
6. **Loaded**
7. **Active**
8. **Marked for Unload**
9. **Saving**
10. **Unloaded**

The exact names may vary, but the ideas should remain.

### Meaning of these states

#### Unloaded

No live chunk object is present in active memory.

#### Requested

Some system has expressed interest in the chunk existing soon.

Examples:

- player movement
- simulation ticket
- generation neighbor dependency
- preload radius

#### Loading

The chunk is being loaded from persistence or prepared for generation.

#### Generating

Terrain/block contents are being produced.

#### Populating/Finalizing

Post-generation work is being completed.

Examples:

- surface passes
- feature placement
- light initialization
- heightmap updates
- neighbor seam finalization

#### Loaded

The chunk exists in memory and its core voxel data is valid.

#### Active

The chunk is participating in simulation, updates, ticking, and related systems.

A loaded chunk is not necessarily active.

#### Marked for Unload

The chunk currently has no reason to remain resident and is queued for save/release once safe.

#### Saving

Persistent state is being written out.

---

## Load and Unload Rules

Load/unload policy must be explicit and ticket-driven.

### Chunk residency principle

A chunk remains loaded only while at least one valid reason requires it.

Those reasons should be represented as **tickets** or equivalent residency references.

### Common ticket sources

Examples of reasons to keep a chunk resident:

- player proximity
- entity simulation requirements
- world generation dependencies
- explicit system pinning
- server spawn region residency
- active block/entity operations
- in-flight jobs that require the chunk to remain valid

### Important rule

A chunk must not unload while any job still holds valid access requiring its data.

This means the chunk manager must coordinate with the job system through:

- refcounts
- scoped access guards
- generation counters
- pin/unpin residency handles
- or another equally explicit mechanism

### Unload conditions

A chunk may be unloaded only when all of the following are true:

- no active residency ticket remains
- no critical job is still using it
- it is not currently in a protected state transition
- required dirty data has been serialized or intentionally discarded
- unloading it will not violate a parent workflow expecting its presence

### Unload behavior

On unload:

- authoritative CPU-side chunk state is released
- derived caches may be discarded
- renderer resources may be released separately by renderer systems
- persistent dirty state must already be saved or intentionally handled

---

## Simulation Radius

The engine should distinguish between **loaded radius** and **simulation radius**.

These are not the same thing.

### Loaded radius

The loaded radius controls which chunk columns are resident in memory.

This is primarily for:

- visibility
- nearby interactions
- smoother movement across chunk boundaries
- prefetching
- network relevance

### Simulation radius

The simulation radius controls which chunk columns are eligible for active world simulation. Section-oriented systems still process individual chunk sections inside those columns.

This includes:

- block ticks
- fluid updates
- local AI
- environment logic
- other world systems that should not run everywhere infinitely

### Rule

Simulation should only run for chunk sections inside the active simulation set.

Chunk columns may be loaded while some or all of their sections are not actively simulated.

This separation is important for performance.

### Practical guideline

A typical arrangement is:

- simulation radius <= loaded radius

For example, a server may load a somewhat larger shell around players for streaming smoothness, but only fully simulate the inner set.

### Cross-boundary behavior

Systems that depend on neighbors near the simulation edge must define graceful behavior.

Examples:

- boundary chunk sections may still sample loaded non-simulating neighbors
- updates that would push beyond the active simulation set may be deferred, clamped, or propagated only when neighbors activate

The policy should be system-specific, but the world model must allow the distinction.

---

## Generation Boundaries

The world model supports procedural generation beyond currently loaded areas, but generation itself is demand-driven.

### Principle

The engine does not eagerly generate the infinite world.

Chunk columns are generated when required by load or explicit generation requests.

Generation is asynchronous. A streaming or gameplay caller may mark sections in a column `Requested`, but it must not run terrain generation inline on the caller path. The authoritative server/world scheduler owns column generation jobs, workers compute generated block data for the whole configured vertical column from immutable seed/config inputs, and the world thread publishes accepted section results by transitioning sections to `Loaded`.

Publication of completed generation jobs is budgeted independently from job
submission. If several workers finish at once, the world/server publishes a
small number per tick and leaves the rest queued, preventing chunk streaming,
client application, meshing, and upload work from collapsing onto one frame.

### Generation triggers

Generation may occur when:

- a chunk column is requested and no persisted data exists
- neighboring generation dependencies require it
- pregeneration tools request it
- editor or server workflows explicitly force it

All of these triggers enqueue generation work. They do not synchronously fill live chunk storage.

### Boundary rules

Generation logic must define what neighborhood is required to finalize a chunk column.

For example:

- base terrain may generate from world seed and chunk-column coordinate alone
- feature placement may require neighboring column context
- lighting initialization may require adjacent chunk data
- biome transitions may require region- or neighborhood-level sampling

### Important distinction

A chunk may be:

- generated enough to exist
- but not yet finalized enough for all downstream systems

Therefore generation stages should be separated rather than treated as one opaque boolean.

### No hard global edge by default

The engine world model should not impose a hard world border by default.

If a game wants borders, safe zones, map limits, or special void regions, that belongs in game/server policy, not the base world model.

---

## What Operations Are Safe Off-Thread

This section must stay explicit because it will shape the scheduler and chunk manager.

## Safe off-thread operations

The following operations are generally safe off-thread when they operate on isolated data, immutable inputs, snapshots, or properly pinned chunk data:

- procedural terrain generation from seed and coordinates
- chunk block data generation before publication
- mesh building from chunk data snapshots or read-only chunk views
- lighting preparation work that does not race with live mutation
- serialization and compression of chunk data copies
- decompression and decode work before main publication
- derived cache building from stable read-only chunk data
- region file parsing and preparation
- heightmap or metadata building from isolated chunk contents

## Conditionally safe off-thread operations

These can be safe, but only with explicit synchronization or isolation:

- reading loaded chunk data
- reading neighboring chunk data
- relighting across chunk boundaries
- block update propagation
- fluid simulation
- local navigation cache rebuilding
- persistence writeback from live chunks

These require one of:

- read/write locks
- immutable snapshots
- chunk pinning plus access guards
- deferred command buffers
- single-owner worker model
- or another clearly defined synchronization strategy

## Not safe off-thread by default

These should not be assumed safe without explicit coordination:

- arbitrary direct mutation of live loaded chunks
- mutating multiple live chunks ad hoc from worker threads
- modifying entity-world relationships from random jobs
- changing chunk residency/lifetime from uncontrolled worker code
- publishing partially initialized chunks to world-visible state
- retaining raw pointers/references to chunks across arbitrary async boundaries

---

## Publication Model

Off-thread work should usually follow this pattern:

1. gather immutable inputs or acquire guarded access
2. compute on worker threads
3. produce a result object
4. publish the result through controlled world/chunk-manager handoff
5. mark affected systems dirty as needed

Examples:

- generate block data off-thread, then publish chunk contents atomically
- build mesh data off-thread, then hand render upload work to render systems
- serialize chunk data from a snapshot, then commit save metadata centrally

This model is preferred over letting worker threads directly perform long chains of live world mutation.

---

## Chunk Access Principles

The engine should expose chunk access in a way that makes the safe path the easy path.

### Recommended principles

- world/chunk manager owns live chunk objects
- access is temporary and scoped
- read access is easier than write access
- multi-chunk mutation requires deliberate coordination
- background jobs should prefer snapshots or publication workflows
- live chunk payloads should use shared immutable storage or equivalent copy-on-write techniques where practical so snapshots stay cheap
- long-lived raw references to chunk internals should be avoided
- chunk lifetime and chunk access validity should be mechanically enforced where possible

---

## Persistence Expectations

Persistence format details belong in another document, but the world model assumes:

- chunks can be loaded independently
- regions group persistence units
- dirty chunks can be saved incrementally
- generated-but-unmodified chunks may be treated differently from modified chunks
- column and chunk metadata may be stored separately if useful
- persistence should not dictate live runtime ownership rules

---

## Networking Implications

The world/chunk model should naturally support streaming section-sized chunk data to clients.

This document does not define the protocol, but it assumes:

- chunk interest is selected horizontally by chunk column
- the server streams every configured vertical chunk section for each selected column
- chunk sections remain the network payload unit
- chunk columns remain the higher-level streaming, residency, and generation grouping
- loaded radius and sent-to-client radius are related but not necessarily identical
- server authority decides which chunk columns are relevant to a client
- inactive or far-away columns do not need full simulation or streaming

---

## Recommended Invariants

The following invariants should hold across the engine:

- chunk section size is always 16 x 16 x 16
- coordinate conversions always use floor division/modulo semantics
- live chunk-column and chunk-section ownership belongs to the world/chunk manager, not neighboring chunks
- neighboring chunk-section access is temporary and validated
- loaded does not automatically mean simulated
- off-thread work publishes results through controlled handoff
- chunk unload is ticket-driven and job-aware
- region grouping exists for indexing/persistence, not as the primary simulation object

---

## Threading, Background Tasks, and Update Flow

This section defines how world and chunk data interact with the job system.

The goal is to enable high-performance multithreading without sacrificing correctness, determinism (where needed), or maintainability.

---

## Core Ownership Rule

Each **world instance owns all live chunk state**.

This includes:

- block data
- lighting data
- fluid data
- chunk metadata (dirty flags, versions, etc.)
- chunk lifetime and residency

Worker threads do **not** own world state.

Worker threads perform **computation**, not authoritative mutation.

---

## Three-Tier Execution Model

All world work should conceptually fall into three tiers:

### 1. Authoritative World State (Main Ownership Layer)

Owned by the world:

- live chunks
- simulation state
- chunk lifecycle
- entity interactions
- authoritative mutations

Only world-controlled phases may safely mutate this state.

---

### 2. Background Task Inputs (Read Layer)

Prepared by the world and sent to workers:

- chunk snapshots
- read-only views
- compact neighbor data
- generation parameters

These inputs must be:

- immutable
- self-contained
- safe to use off-thread

---

### 3. Background Task Results (Write-Back Layer)

Produced by workers:

- mesh buffers
- generated chunk data
- lighting updates
- fluid updates (as commands or deltas)
- serialization buffers

These results are not immediately applied.

They must be **published back through the world**.

---

## Background Task Categories

### Derived Work (Safe and Preferred)

These tasks:

- read world data
- produce derived outputs
- do not directly mutate world state

Examples:

- chunk meshing
- serialization
- mesh LOD generation
- collision cache building
- lighting analysis/precompute
- terrain generation (pre-publication)

These should always run off-thread.

---

### Simulation Work (Controlled Mutation)

These tasks affect world state:

- block updates
- lighting propagation
- liquid simulation
- scheduled ticks

These must follow controlled mutation rules.

Workers may assist computation, but **world commits changes**.

---

## Chunk Section Versioning

Each chunk section tracks block/geometry and lighting versions separately.

Required section version fields:

```text
block_version
light_version
mesh_built_from_block_version
mesh_built_from_light_version
```

`block_version` increments when local block data, fluid geometry data, or a required neighbor boundary geometry dependency changes.

`light_version` increments when local lighting data changes or when neighbor boundary lighting used by this section's mesh changes.

Implementations may also track a transport/persistence light-data version that only increments when the section's own light buffer changes. Streaming uses that data version so dependency-only `LightMesh` invalidation does not resend otherwise unchanged chunk payloads.

Mesh workers carry both source versions in their result.

### Mesh Publication Rule

When a mesh task completes:

- if chunk no longer exists -> discard result
- if `source_block_version != current block_version` -> discard result
- otherwise -> the mesh may be accepted

A mesh result may be accepted when `source_light_version` is stale. Stale lighting in visible geometry is temporarily acceptable. If the accepted mesh used stale lighting, the section remains light-mesh dirty so a later light-only remesh can replace it.

### Lighting Publication Rule

Lighting results are authoritative world results.

When a lighting task completes:

- if chunk no longer exists -> discard result
- if source block/geometry inputs are stale -> discard result
- otherwise -> publish the light data through world-owned commit logic

Accepted light changes increment `light_version`, mark affected sections `LightMesh` dirty, and enqueue a future section mesh rebuild. Lighting publication must not rebuild meshes inline.

Consumers applying received chunk snapshots must compare block and light buffers before dirtying dependencies. Duplicate snapshots must not advance mesh revisions, light-only changes must not dirty geometry neighbors, and boundary-only light changes should dirty only the face neighbors whose mesh samples can observe that light.

This avoids complex cancellation logic while allowing geometry responsiveness to win over perfect lighting freshness.

---

## Chunk Pinning and Lifetime Safety

A chunk must not unload while:

- a task depends on it
- a worker holds a valid access reference

Two supported models:

### Snapshot Model (Preferred Default)

- copy required chunk data
- release live chunk immediately
- worker operates on snapshot

Pros:

- simple
- no locking
- safe

Cons:

- memory overhead

---

### Pinning Model (Advanced)

- chunk is "pinned" during task lifetime
- cannot unload until pin released

Used when copying is too expensive.

---

## Meshing Model

### Input

Meshing jobs must operate on:

- chunk block data
- neighbor boundary data
- lighting data

Prefer snapshot-based inputs.

### Execution

Worker:

- builds mesh buffers
- does not access world state directly

### Output

Worker returns:

- mesh buffers
- chunk coordinate
- source block/geometry version
- source light version

### Publish

World:

- validates block/geometry version
- hands mesh to rendering system
- marks mesh state clean only for versions that match current section state

---

## Lighting Model

Lighting is treated as **simulation + derived hybrid work**.

### Lighting Data Ownership

Each chunk owns:

- sky light values
- block light values

Lighting must remain consistent across chunk boundaries.

---

### Lighting Updates

Lighting changes are triggered by:

- block placement/removal
- light-emitting block changes
- chunk generation
- neighbor updates

---

### Lighting Execution Model

Lighting should be processed in **two phases**:

#### Phase 1: Compute (Off-thread allowed)

Workers may:

- read chunk + neighbor data
- compute light propagation results
- produce update buffers or propagation queues

Workers must not directly mutate arbitrary neighboring chunks.

---

#### Phase 2: Commit (World-controlled)

World applies:

- light value updates
- propagation effects
- neighbor dirty flags

---

### Cross-Chunk Lighting

Cross-chunk lighting must use:

- explicit propagation queues
- or boundary update events

Never rely on:

- implicit direct neighbor mutation from workers

---

### Lighting and Meshing Interaction

Lighting changes must:

- mark affected chunks as mesh-dirty
- potentially mark neighbors dirty

---

## Fluid Simulation Model

Fluid simulation follows a similar pattern to lighting.

### Execution

Workers:

- compute fluid updates from chunk-local + neighbor data
- output delta/command buffers

World:

- applies updates in commit phase
- schedules further simulation if needed

---

### Cross-Chunk Fluid Updates

Workers must not directly mutate neighbor chunks.

Instead they emit:

- neighbor update events
- or delta buffers

---

## Block Updates

Block changes are always **authoritative world operations**.

When a block changes:

1. world applies mutation
2. owning section `block_version` increments
3. chunk marked dirty:
   - geometry mesh
   - lighting propagation
   - fluid
   - save
4. a section mesh rebuild is queued immediately
5. lighting work is queued separately
6. neighbors are marked dirty only when boundary geometry or boundary lighting can affect them

Meshing must not wait for lighting. No system should directly trigger deep recursive updates, lighting propagation, simulation stepping, or mesh rebuilding inline from block placement or breaking.

---

## Dirty Flags and Work Scheduling

Chunks maintain dirty flags indicating required work.

Example:

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

World update loop:

* gathers dirty chunks
* schedules section mesh tasks for `Geometry` and `LightMesh`
* schedules one-section lighting tasks for `Lighting`
* processes results

`Geometry` means block shape, opacity, model, texture, fluid geometry, or boundary geometry input changed.

`Lighting` means authoritative lighting propagation/update work is required.

`LightMesh` means lighting changed and vertex lighting should be remeshed, without implying block geometry changed.

Lighting jobs are intentionally bounded. A lighting batch contains one or more non-face-adjacent section snapshots; each snapshot targets one dirty section and includes currently loaded face neighbors as immutable boundary inputs. Dirty face neighbors are skipped as seeds because their light buffers may still contain stale pre-repair values. For generated-world skylight, the snapshot may also include loaded sections in the same chunk column from the target upward toward the generated surface section; these are read-only context used to decide whether sunlight can reach the target top. If publication changes a boundary, the world marks the face neighbor `Lighting` so propagation continues in a later worker job after the batch's accepted targets have been applied. Dirty sections in a column are selected top-down so surface skylight repairs feed lower continuations. Missing neighbors are recorded as deferred boundary work; they are not loaded synchronously. Missing-above skylight is represented explicitly per column so unloaded upper terrain does not turn underground edits into open-sky columns.

---

## World Update Flow

A world tick should follow structured phases:

### 1. Input Phase

* player actions
* network updates
* scheduled operations

---

### 2. Authoritative Mutation Phase

* apply block changes
* update entities
* mark dirty flags

---

### 3. Work Gathering Phase

* identify chunks needing:

  * meshing
  * lighting
  * fluid simulation
  * saving

---

### 4. Job Scheduling Phase

* prepare task inputs
* dispatch to worker threads

---

### 5. Result Collection Phase

* gather completed task outputs

---

### 6. Commit Phase

* validate versions
* apply results
* update chunk state
* mark additional dirty work

---

### 7. Cleanup Phase

* handle unload decisions
* finalize saves

---

## Server Session and World Registry

The server owns the authoritative set of loaded worlds and active player sessions.

A world represents one independent simulation space, dimension, or instance. Worlds do not own each other. A server may load multiple worlds at the same time.

Examples:

```text
arvane_home/saves/server-save-1/
    players/
    worlds/
        overworld/
        caves/
        arena-instance-001/
```

Each world has its own:

* world identifier
* chunk storage
* region files
* world generation state
* simulation state
* ECS registry
* active chunk set
* loaded chunk cache
* tick configuration
* world-local runtime state

The server maintains a world registry that maps stable world identifiers to loaded world instances.

## World Identifier

Each world must have a stable identifier within a server save.

Example world identifiers:

```text
overworld
nether-like-test
arena-instance-001
debug-flatworld
```

The world identifier is used for:

* save directory lookup
* player location persistence
* world transfer
* networking references
* debugging
* server commands

World identifiers should be stable strings, not transient pointers or array indices.

Runtime systems may use compact internal handles, but persistent data and protocol-visible references should use stable world identifiers or explicitly mapped world handles.

## Server World Registry Responsibilities

The server world registry is responsible for:

* loading worlds
* unloading worlds
* looking up worlds by stable identifier
* assigning runtime world handles if needed
* ticking loaded worlds
* routing player sessions to worlds
* routing chunk requests to the correct world
* routing entity operations to the correct world
* coordinating save boundaries

The registry should not directly own block storage details. Chunk storage remains the responsibility of each world.

The registry should not directly mutate chunks except through world-level APIs.

## Player Session

A player session represents an active connection or local integrated-server client.

A session is not the same thing as an entity.

A session may own or reference:

* connection handle
* assigned session ID
* assigned player ID
* current world ID
* controlled entity ID, if spawned
* requested view distance
* known client registry/protocol state
* chunk streaming state
* pending world transfer state

The player entity belongs to the world ECS registry of the world where it currently exists.

If a player changes worlds, the session remains server-level, while the player entity may be removed from one world and created or restored in another.

## Player Save Data

Persistent player data belongs under the server save, not inside a specific world save.

Example:

```text
arvane_home/saves/server-save-1/
    players/
        <player-id>.yaml
    worlds/
        overworld/
        caves/
```

Player data may include:

* last known world ID
* position
* rotation
* inventory
* selected hotbar slot
* health or survival state
* permissions or profile metadata
* respawn target

World-specific entity state should remain in the relevant world save.

Cross-world player identity should remain in `players/`.

## Integrated Singleplayer Server

Singleplayer should use the same server session and world registry model as multiplayer.

The local client connects to an integrated server using the same high-level lifecycle as a remote client.

Singleplayer may optimize transport internally, but it should not bypass:

* protocol compatibility assumptions
* session creation
* world lookup
* chunk streaming ownership
* authoritative simulation
* save boundaries

This keeps singleplayer and multiplayer behavior aligned.

## World Transfer

Moving a player between worlds should be treated as a server-controlled transfer.

The transfer should conceptually perform:

1. Pause or restrict gameplay input for the session.
2. Save or snapshot relevant player state.
3. Remove or deactivate the player entity in the old world.
4. Resolve the target world through the world registry.
5. Load or create the destination chunk area if needed.
6. Spawn or restore the player entity in the target world.
7. Update the session’s current world ID.
8. Reset or rebuild chunk streaming state.
9. Notify the client of the world change.
10. Begin streaming chunks for the new world.

The client must not independently decide that it has changed worlds.

## Chunk Requests and Session Routing

Client chunk requests must always be resolved relative to the session’s current world unless the protocol explicitly supports cross-world queries.

The server must validate that a session is allowed to request data for a given world.

Chunk streaming state should be tracked per session and per world.

When a player changes worlds, old chunk visibility state should be invalidated or cleanly transitioned.

## Tick Ownership

The server world registry is responsible for deciding which worlds tick.

Each loaded world may have its own tick configuration, but the server owns the top-level scheduling.

A world may be:

```text
loaded_active
loaded_suspended
loading
unloading
saving
```

Only active worlds should perform normal simulation.

Suspended worlds may remain loaded for persistence, debugging, or future transfer targets, but should not run full gameplay simulation unless explicitly configured.

## Threading Constraints

World registry operations that create, destroy, load, or unload worlds must be coordinated with the main server/simulation thread.

A worker thread may prepare world data, chunk data, or serialized data, but it must not directly publish a loaded world into the active registry without synchronization through the owning server thread.

The world registry must not expose raw mutable world pointers to arbitrary worker jobs.

Background jobs should use immutable snapshots, handles, or explicit command queues.

---

## Safe Off-Thread Operations

### Safe

* terrain generation (pre-publication)
* meshing from snapshots
* serialization
* compression
* lighting computation (non-mutating phase)
* fluid computation (command-buffer style)

---

### Conditionally Safe

* reading chunk data (must be guarded or snapshotted)
* neighbor sampling (must be controlled)

---

### Not Safe

* arbitrary mutation of live chunks
* cross-chunk writes from workers
* modifying chunk lifetime
* holding long-lived raw chunk pointers

---

## Chunk View Distance

Chunk view distance defines how far chunk columns are loaded, simulated, streamed, and rendered around a player or camera.

View distance is not a single value. Arvane separates horizontal chunk-column distance into several related ranges so that loading, simulation, networking, and rendering can be tuned independently.

### Distance Types

The engine should distinguish between:

- **Load distance**
  - Chunk columns that should exist in memory.
  - Includes columns and their configured section slots needed for rendering, simulation borders, neighbor sampling, and streaming stability.

- **Simulation distance**
  - Chunk columns whose sections may receive active world ticks.
  - Fluids, block updates, entities, and scheduled simulation events only run on sections inside this range unless explicitly forced.

- **Render distance**
  - Chunk columns whose received sections are eligible to be drawn by the client renderer.
  - Render distance must never require sections that are not loaded or received.

- **Network stream distance**
  - Chunk columns the server should send to a client.
  - For each selected column, the server streams the full configured vertical section range as section-sized payloads.
  - Usually matches or slightly exceeds render distance so the client can receive sections before they become visible.

### Ownership

The server is authoritative over chunk-column loading and section simulation.

Clients may request or configure a preferred render/view distance, but the server decides the effective stream and simulation ranges based on:

- server configuration
- player position
- performance limits
- world rules
- maximum allowed client view distance

The client renderer may maintain its own local render distance, but it must only render chunk sections that have been received and validated.

### Distance Shape

Chunk distance should be based on horizontal chunk-column coordinates, not block coordinates or section Y.

The default view area should use a square or rectangular chunk-column region around the player for simplicity and predictable streaming behavior.

Radial or frustum-based refinement may be added later, but core chunk ownership, loading, and save logic should not depend on camera frustum visibility.

### Chunk Lifecycle Interaction

As the player moves, the world should:

1. Determine the target chunk-column position of the player.
2. Compute desired chunk-column ranges for loading, simulation, rendering, and streaming.
3. Schedule missing columns for load or generation; generation jobs compute the whole configured vertical column.
4. Keep existing columns and their sections alive while they remain inside the relevant distance.
5. Mark sections outside all required ranges as candidates for unload, and unload empty columns when no sections remain.
6. Delay unloading if pending jobs, mesh uploads, lighting updates, saves, or network sends still reference the section or column.

Chunk sections must not be unloaded immediately just because their column leaves render distance. Render distance is only one part of the chunk lifecycle.

### Hysteresis

Chunk loading should use hysteresis to avoid constant load/unload churn when a player moves near a boundary.

For example:

- load chunk columns when they enter the load radius
- unload chunk columns only after they exceed the load radius plus an unload margin

This margin should be configurable.

### Interaction With Meshing

Meshing may require neighboring chunk-section data, especially for:

- face culling
- ambient occlusion
- lighting
- transparent boundary behavior

For this reason, the load distance should usually be at least one chunk column larger than the render distance.

A chunk section should not be considered fully meshable unless its required neighbor boundary data is available or explicitly represented as missing/empty according to the meshing rules.

### Interaction With Simulation

Simulation distance controls active world behavior.

Inside simulation distance:

- scheduled block ticks may run
- fluid updates may run
- entities may update
- lighting propagation may process live updates

Outside simulation distance:

- chunk columns may remain loaded
- received chunk sections may remain renderable
- most active simulation should pause
- deferred updates may be stored for later activation

When a chunk column enters simulation distance, its sections should process activation logic before normal ticking. This allows deferred fluid, lighting, and block update events to be restored safely.

### Configuration

View distance settings should be defined in server/world configuration, not hard-coded.

Suggested configuration values:

```yaml
chunks:
  load_distance: 10
  simulation_distance: 6
  render_distance: 8
  stream_distance: 9
  unload_margin: 2
```

The engine should validate these relationships:

* `simulation_distance <= load_distance`
* `render_distance <= load_distance`
* `stream_distance <= load_distance`
* `unload_margin >= 0`

Recommended default relationship:

```text
simulation_distance <= render_distance <= stream_distance <= load_distance
```

### Future Extensions

The view-distance system should leave room for:

* per-world distance settings
* per-server maximums
* per-client requested render distance
* dynamic scaling under load
* frustum-prioritized streaming
* vertical distance limits
* separate entity tracking distance
* separate high-detail and low-detail terrain ranges

---

## Key Invariants

* world owns chunk-column and chunk-section state
* workers compute, world commits
* section results are version-validated
* cross-chunk effects are explicit (events/queues)
* chunk-section unload respects active work
* derived work is async, mutation is controlled
* worker-facing snapshots hold immutable shared storage, not live world references
* chunk-section neighbor access for meshing is prefetched into snapshot storage pointers before worker execution
* full section copies are avoided unless copy-on-write detaches a mutating live section from an active snapshot

---

## Performance Notes

Chunk-section storage is optimized for the current world-owned model:

* block, light, and fluid payloads are flat contiguous arrays
* snapshots capture shared immutable storage handles and source versions
* live chunk-section mutation detaches storage only when an active snapshot still references it
* async results must validate block/content/light versions before publication
* dirty mesh queues deduplicate chunk-section coordinates before scheduling

Workers must never call world lookup APIs. The simulation/main thread prepares all chunk and neighbor inputs up front, and workers consume only snapshot data.

---

## Summary

The engine world model is based on:

- **16 x 16 x 16 chunk sections**
- **chunk columns** as horizontal residency, streaming-interest, and generation units
- per-world **height settings** that define the inclusive vertical chunk-section range
- **32 x 32 chunk-column regions** for persistence/indexing
- clear separation between **loaded** and **simulated**
- explicit chunk ownership by the **world/chunk manager**
- temporary, validated neighbor access instead of permanent neighbor ownership
- section-sized networking, meshing, lighting, and simulation work
- safe off-thread work through snapshots, guards, pinning, and controlled publication
- demand-driven whole-column generation for an effectively unbounded world

This model is intended to remain stable even as higher-level game rules evolve.
