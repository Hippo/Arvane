# Milestones

This document defines the implementation milestones for Arvane.

Milestones are intentionally broad enough for capable agents to make meaningful progress in one pass, but still phased enough to avoid chaotic “build everything at once” behavior.

Networking, threading, and server authority are considered foundational and must be designed in from the start.

---

# Milestone Rules

- Implement milestones in order.
- Do not skip architecture boundaries for convenience.
- Each milestone should leave the project compiling and runnable.
- Each milestone should include tests where practical.
- Prefer server-authoritative flow even in singleplayer.
- Prefer snapshot/job handoff patterns from the beginning.
- Do not introduce major systems before their milestone unless required by foundational structure.
- Leave explicit TODOs instead of inventing undocumented architecture.

---

# Milestone 1: Engine Foundation, App Shell, Threading, and Networking Skeleton

## Goal

Establish the basic executable structure, module boundaries, main loop, scheduler model, and client/server separation before gameplay systems are built.

## Scope

- CMake project structure
- `arv_desktop` client executable
- dedicated server executable skeleton
- OpenGL 3.3 context creation
- window creation
- input polling
- basic camera
- main loop structure
- fixed tick / variable render loop
- logging
- config loading
- core math/types
- scheduler/job system primitives
- main thread rules
- worker thread rules
- render thread ownership rules
- integrated server structure for singleplayer
- basic networking abstraction
- GameNetworkingSockets integration skeleton
- packet/message registry skeleton
- client/server connection state model

## Tests

Add tests for:

- fixed timestep accumulator
- core math helpers
- coordinate helpers if introduced
- scheduler job completion
- packet serialization basics
- config parsing

## Exit Criteria

- Client opens a window.
- Dedicated server can start and shut down.
- Integrated server path exists.
- Scheduler primitives exist.
- Networking has a clear abstraction, even if gameplay packets are minimal.
- Rendering, server simulation, and jobs have clear ownership boundaries.

---

# Milestone 2: World, Chunks, Blocks, Meshing, and Basic Rendering

## Goal

Build the first real voxel world pipeline, from data-driven block definitions to chunk storage, meshing, and rendering.

## Scope

- block registry
- block IDs and state IDs
- air invariants
- data-driven block definitions
- world/chunk/section storage
- world coordinate conversions
- chunk lifetime model
- chunk dirty flags
- block get/set APIs
- chunk snapshots for jobs
- chunk meshing jobs
- opaque mesh generation
- transparent mesh generation basics
- face culling
- neighbor sampling
- mesh version validation
- GPU upload handoff
- basic terrain rendering
- temporary generated/test world data

## Tests

Add tests for:

- block registration
- state expansion
- air invariants
- duplicate registry entries
- world/chunk coordinate conversion
- negative coordinate behavior
- chunk indexing
- block get/set
- dirty flags
- face culling
- chunk boundary meshing
- stale mesh result rejection

## Exit Criteria

- Data-driven blocks exist.
- Chunks store real block state.
- Meshing runs through the job/snapshot model.
- Terrain renders from chunk meshes.
- World mutation invalidates meshes correctly.
- Worker jobs do not mutate live world data.

---

# Milestone 3: World Generation, Lighting, and World Simulation

## Goal

Turn the world from static chunks into a generated and simulated voxel world.

## Scope

- deterministic world generation
- seed handling
- YAML-driven worldgen definitions
- chunk generation jobs
- generation boundaries
- lighting storage
- 16-bit RGBS light format
- sunlight propagation
- block light propagation
- lighting dirty regions
- chunk-border lighting propagation
- simulation tick phases
- block update scheduling
- liquid simulation
- Minecraft-style liquid flow
- liquid source behavior
- liquid level/state representation
- liquid spread rules
- liquid decay rules
- vertical flow priority
- horizontal flow
- chunk-border liquid updates
- simulation radius handling
- active vs loaded chunks

## Tests

Add tests for:

- deterministic generation
- same seed / same chunk output
- generated block IDs are valid
- light packing/unpacking
- sunlight propagation
- block light falloff
- light removal
- lighting across chunk borders
- liquid downward flow
- liquid horizontal spread
- liquid decay
- liquid source stability
- liquid crossing chunk boundaries
- simulation radius behavior

## Exit Criteria

- Worldgen creates deterministic terrain.
- Lighting updates after block changes.
- Liquids simulate through scheduled world ticks.
- Simulation does not require rendering.
- Chunk-border simulation behavior is defined and tested.
- Generated, lit, simulated chunks can be rendered.

---

# Milestone 4: Player, Physics, Interaction, Inventory, and Saving

## Goal

Make the world playable and persistent.

## Scope

- player controller
- camera/player separation
- input abstraction
- terrain collision queries
- AABB collision
- gravity
- jumping
- ground detection
- per-axis movement resolution
- centralized physics constants
- block targeting raycast
- reach distance
- break/place flow
- server-authoritative interaction path
- item definitions
- item stacks
- hotbar
- held item semantics
- block item placement
- drops/pickups if ready
- save directory layout
- player save data
- world save data
- region file format
- chunk serialization
- save versioning
- registry compatibility references

## Tests

Add tests for:

- AABB collision
- per-axis movement
- ground detection
- raycast hit/miss
- reach limits
- break/place validation
- stack merging/splitting
- hotbar selection
- chunk serialization round-trip
- player save round-trip
- region header parsing
- corrupted save handling

## Exit Criteria

- Player can move with collision.
- Player can break and place blocks.
- Inventory/hotbar drives placement.
- Interactions flow through server authority.
- Edited worlds persist across reloads.
- Physics constants are centralized.

---

# Milestone 5: UI, Font Rendering, Assets, and Client Presentation

## Goal

Implement the user-facing presentation layer once the core world interaction loop exists.

## Scope

- retained-mode UI system
- immediate-mode authoring API for UI construction
- UI element tree
- layout system
- input focus model
- mouse/keyboard routing
- plugin-safe UI description model
- debug ImGui remains separate
- FreeType integration
- HarfBuzz shaping
- font stack fallback
- glyph atlas management
- dynamic glyph caching
- text measurement
- shaped text runs
- Unicode support strategy
- UI text rendering
- asset/resource manager
- texture loading through `stb_image`
- texture atlas rules
- shader source organization
- resource handles
- resource cache
- missing asset fallback
- basic menus/HUD
- hotbar UI
- inventory UI if ready

## Tests

Add tests for:

- UI layout calculations
- focus transitions
- input routing
- font fallback selection
- glyph cache lookup
- text measurement
- shaped run boundaries where practical
- asset path resolution
- missing asset fallback
- resource cache identity
- atlas padding rules

## Exit Criteria

- User-facing UI exists.
- Debug ImGui is not used for gameplay UI.
- Text rendering uses FreeType and HarfBuzz.
- Font fallback is supported.
- Assets are loaded through the resource system.
- HUD/hotbar can render through the UI system.

---

# Milestone 6: Entities, Replication, and Multiplayer Gameplay Foundation

## Goal

Add non-block simulation and expand networking beyond skeleton structure.

## Scope

- world-local ECS registry
- entity creation/destruction
- entity transforms
- entity simulation phases
- entity chunk relevance
- persistent entities
- player entity mapping
- global player profile/session model
- entity replication messages
- chunk streaming protocol
- block update protocol
- inventory sync protocol basics
- server-authoritative entity updates
- client interpolation
- prediction/reconciliation placeholders if not fully implemented
- connection lifecycle
- join/leave flow
- cross-world player identity behavior

## Tests

Add tests for:

- entity lifecycle
- transform updates
- entity persistence round-trip
- player profile to world entity mapping
- packet serialization
- invalid packet handling
- chunk stream boundaries
- block update validation
- entity snapshot application
- connection state transitions

## Exit Criteria

- Worlds can own and simulate entities.
- Player identity is global, but player entities are world-local.
- Server can stream chunks to clients.
- Server can replicate block and entity updates.
- Client applies authoritative updates cleanly.
- Networking design matches singleplayer integrated-server flow.

---

# Milestone 7: Plugin API, Events, Protocol Hardening, and Extensibility

## Goal

Expose stable extension points without letting plugins break engine ownership rules.

## Scope

- plugin loading model
- plugin lifecycle
- plugin metadata
- event API
- event ordering
- cancellable events where appropriate
- block/item registration hooks
- worldgen hooks
- command registration
- scheduler access for plugins
- plugin-safe UI contribution model
- protocol versioning
- packet compatibility rules
- registry synchronization strategy
- server/client plugin boundary rules

## Tests

Add tests for:

- plugin metadata parsing
- plugin load ordering
- event dispatch
- event cancellation
- plugin block registration
- command registration
- protocol version mismatch handling
- registry sync validation

## Exit Criteria

- Plugins can register content through controlled APIs.
- Plugins cannot directly mutate unsafe engine internals.
- Event ordering is documented and testable.
- Protocol compatibility behavior is explicit.
- Plugin UI contributions use the retained UI description model.

---

# Milestone 8: Polish, Performance, Tooling, and Stability

## Goal

Stabilize the engine after the major systems exist.

## Scope

- performance counters
- debug overlays
- profiling hooks
- improved logging
- error handling cleanup
- renderer cleanup
- meshing optimization
- lighting optimization
- liquid simulation optimization
- job scheduling tuning
- network diagnostics
- asset hot reload if desired
- documentation sync
- test coverage pass
- gameplay polish
- packaging/build improvements

## Tests

Add or improve tests for:

- registries
- serialization
- world mutation
- meshing
- lighting
- liquids
- ticking
- job handoff
- networking packets
- UI layout
- font fallback
- save/load compatibility

## Exit Criteria

- The engine is stable enough for real gameplay iteration.
- Major systems have tests.
- Documentation matches implementation.
- Performance bottlenecks are visible.
- The project can evolve without large architectural rewrites.

---

# Implementation Guidance for Agents

When implementing a milestone:

1. Read all relevant design documents first.
2. Preserve documented module boundaries.
3. Implement the full milestone only to the documented exit criteria.
4. Add tests where the behavior is deterministic and non-rendering.
5. Use TODOs for future milestone behavior.
6. Do not introduce undocumented dependencies.
7. Do not bypass server authority for convenience.
8. Do not mutate live world state from worker jobs.
9. Do not use ImGui for user-facing gameplay UI.
10. Summarize deviations clearly.

The goal is not to build everything at once.

The goal is to build large, coherent layers in the right order.