
# Architecture

## 1. Overview

Arvane is a **server-authoritative voxel engine** written in C++, designed for:

* infinite procedural worlds
* multiplayer-first simulation
* integrated singleplayer via local server runtime
* native C++ plugins
* clean subsystem boundaries
* high-performance world, physics, and rendering systems

The engine is structured around a small set of core principles:

* **server owns authoritative state**
* **simulation runs on fixed ticks**
* **clients are presentation-focused**
* **worlds own world-local simulation**
* **server runtime owns global services**
* **subsystems depend on interfaces where appropriate, not concrete implementations**
* **plugins extend behavior through stable engine APIs rather than ad-hoc coupling**

Arvane uses the `arv_` prefix for engine modules and `arv::` as the primary C++ namespace.

---

## 2. Core Design Principles

### 2.1 Server-authoritative simulation

The server is the source of truth for all gameplay-relevant state.

This includes:

* block/world mutations
* entity state
* inventories
* health/status
* world actions
* simulation scheduling
* cross-world transfers

Clients do not own gameplay truth. They send input, receive authoritative updates, and maintain presentation-side state such as interpolation, animation, and visual effects.

Singleplayer uses the same server codepath through an **integrated server** model. Arvane does not maintain a separate gameplay implementation for singleplayer.

---

### 2.2 Fixed-tick simulation

Simulation runs on a fixed tick rate. Rendering runs independently.

This separation exists to keep:

* gameplay stable
* server behavior predictable
* scheduling understandable
* rendering smooth and decoupled from simulation

The default simulation target may be configurable, but the architecture assumes a fixed-tick world model rather than frame-driven gameplay logic.

---

### 2.3 Deterministic enough where it matters

Arvane does **not** target strict cross-platform bitwise determinism.

Instead, it targets **practical determinism** for authoritative simulation:

* fixed-tick updates
* stable system phase ordering
* controlled mutation paths
* controlled randomness
* no arbitrary background writes into authoritative world state

This gives the engine predictable behavior, easier debugging, and strong multiplayer correctness without imposing the full cost of strict deterministic lockstep design.

---

### 2.4 World-local simulation, server-global services

Arvane separates simulation ownership into two levels:

#### Server-global runtime

Owns systems and data that must exist above any specific world, such as:

* connected sessions
* loaded world registry
* player directory
* permissions
* command dispatch
* plugin lifecycle
* global scheduling contexts
* cross-world routing

#### World-local runtime

Owns simulation data that belongs to a specific world or dimension, such as:

* chunk storage
* ECS registry
* world-local entities
* world simulation systems
* block and entity scheduled actions
* world-specific rules and state

This distinction allows cross-world features such as messaging, teleport routing, and player presence without collapsing all game state into a single global simulation container.

---

### 2.5 Clean subsystem boundaries

Subsystems should depend on concrete modules only when there is a clear structural reason.

Otherwise, they should depend on narrow interfaces or adapters.

Example:

* `arv_physics` should not depend directly on `arv_world`
* physics only needs an interface for world queries such as solidity, collision sampling, or voxel shape lookup

This rule reduces coupling and keeps modules reusable, testable, and easier to evolve.

---

### 2.6 Plugin-first extensibility

The engine provides the runtime and infrastructure. Plugins provide game-specific behavior and content.

Plugins may define:

* blocks
* items
* entities
* systems
* world generation rules
* commands
* gameplay behavior

Plugins must use engine APIs, including scheduler APIs, rather than mutating engine state through arbitrary background logic.

---

## 3. Runtime Topology

Arvane is best understood as a composition of runtime layers.

```text
Client Runtime
  - input collection
  - rendering
  - audio
  - interpolation/presentation
  - protocol handling
  - local prediction/presentation helpers

Server Runtime
  - sessions
  - world registry
  - commands/permissions
  - plugin manager
  - global services
  - authoritative simulation routing

World Runtime (per world/dimension/instance)
  - chunk storage
  - ECS registry
  - world systems
  - world-local scheduled actions
  - authoritative entity/block state
```

A server can host:

* multiple dimensions
* multiple worlds
* future instanced environments

Singleplayer runs a local integrated server using the same simulation model.

---

## 4. Player Model

Arvane distinguishes between three different concepts that are often incorrectly merged:

### 4.1 Player profile

Persistent player identity and durable data.

Examples:

* account identity
* saved progression
* preferences
* permissions/groups
* long-term metadata

This is not world-local.

---

### 4.2 Player session

Active server-global connection/session state.

Examples:

* network connection
* authentication state
* current world membership
* latency/session info
* command and chat routing

This is also not world-local.

---

### 4.3 Player entity

The world-local simulation object representing the player in a specific world.

Examples:

* transform
* velocity
* health
* inventory component bindings
* physics/controller state
* chunk observation state

A player may have a server-global profile and session while having exactly one active world-local entity at a time.

Cross-world features such as `/msg` operate on global player/session systems, not on world ECS.

---

## 5. Simulation Ownership Model

### 5.1 Server runtime owns

* session management
* player directory
* world registry
* command dispatch
* global plugin services
* cross-world operations
* server-wide scheduled tasks

### 5.2 World runtime owns

* chunk and section storage
* world-local ECS registry
* world simulation systems
* world-local timers and scheduled actions
* block/entity authoritative state
* world-specific environment state

### 5.3 Scheduler ownership

`arv_sched` provides scheduling primitives and execution infrastructure, but does not own gameplay or world state.

Ownership of scheduled work remains with the system that created it.

Examples:

* world-local delayed block tick lives in the world runtime
* global repeating server task lives in the server runtime
* background meshing task is owned by the meshing system that requested it

---

## 6. Authoritative vs derived data

A key architectural distinction in Arvane is between **authoritative data** and **derived data**.

### 6.1 Authoritative data

Data that affects gameplay truth.

Examples:

* block states
* entity transforms
* inventories
* health/status
* world tick queues
* world-local simulation components
* player world membership
* command/gameplay effects

This data must be mutated through controlled simulation paths.

---

### 6.2 Derived data

Data computed from authoritative state for presentation, caching, or acceleration.

Examples:

* chunk meshes
* render interpolation state
* visibility sets
* mesh build products
* client animation state
* intermediate lighting caches
* pathfinding/nav caches
* compression buffers

Derived data may be processed in background systems, as long as authoritative state is not mutated arbitrarily from those jobs.

---

## 7. Determinism Policy

Arvane targets **practical simulation stability**, not strict lockstep determinism.

### 7.1 Goals

* predictable server behavior
* reproducible enough debugging
* stable multiplayer authority
* consistent world logic
* clear ordering rules

### 7.2 Non-goals

* exact bitwise determinism across all hardware and compilers
* fully deterministic client presentation
* lockstep networking assumptions

### 7.3 Rules that support practical determinism

* gameplay uses fixed ticks
* system phases have defined order
* authoritative writes happen in controlled commit paths
* randomness is scoped and seed-controlled where relevant
* plugin code cannot freely mutate authoritative state from arbitrary threads
* background jobs produce results, not unsynchronized gameplay mutations

---

## 8. Scheduling Model

Arvane distinguishes three broad categories of scheduled work.

### 8.1 Tick-scheduled authoritative work

Work tied directly to simulation ticks.

Examples:

* entity updates
* block ticks
* fluid updates
* world actions
* command-triggered gameplay mutations
* repeating world/server tasks

This work is authoritative and ordering-sensitive.

---

### 8.2 Background jobs

Work that may run asynchronously off the main simulation path.

Examples:

* meshing
* asset loading
* chunk generation prep
* compression/decompression
* save serialization
* derived cache generation

Background jobs must not directly mutate authoritative world state without passing through controlled handoff/commit mechanisms.

---

### 8.3 Timers and delayed callbacks

High-level delayed/repeating task APIs used by engine systems and plugins.

Examples:

* run after N ticks
* repeat every N ticks
* run on world tick
* run on server-global tick

These APIs should clearly specify:

* what execution context they run in
* whether they are world-local or server-global
* whether they are authoritative tick work or background work

This distinction is especially important for plugin APIs.

---

## 9. Module Overview

## 9.1 `arv_core`

Fundamental shared types and utilities.

Responsibilities:

* basic type aliases
* identifiers/handles
* logging
* time primitives
* utility containers/helpers
* math foundations used across modules
* low-level shared abstractions

---

## 9.2 `arv_platform`

Platform integration layer.

Responsibilities:

* windowing
* input backend integration
* OS-specific hooks
* application lifecycle glue
* platform services needed by runtime layers

---

## 9.3 `arv_asset`

Asset and data loading infrastructure.

Responsibilities:

* asset loading
* manifests
* resource lookup
* file/data ingestion
* content definition loading
* future pack/mod resource integration

---

## 9.4 `arv_protocol`

Protocol definitions and serialization.

Responsibilities:

* message schemas
* packet/message serialization
* protocol versioning
* compatibility rules
* client/server message contracts

This module defines what messages mean, not how they are transported.

---

## 9.5 `arv_net`

Networking transport and connection layer.

Responsibilities:

* UDP transport
* reliability layer
* packet channels
* fragmentation/reassembly
* session transport concerns
* connection management
* future encryption/compression hooks

Arvane is designed around **reliable UDP**, with support for different delivery semantics depending on message type.

`arv_net` owns transport concerns. `arv_protocol` owns protocol semantics.

---

## 9.6 `arv_ecs`

Arvane’s ECS runtime layer built on top of EnTT.

Responsibilities:

* engine ECS conventions
* registry ownership patterns
* entity lifecycle helpers
* core engine components
* ECS integration with world/runtime systems
* system registration and phase conventions

`arv_ecs` is not intended to hide EnTT completely. It defines how Arvane uses EnTT.

Each world runtime owns its own ECS registry.

There is no general-purpose global ECS by default; server-global systems should be modeled explicitly rather than forced into ECS unnecessarily.

---

## 9.7 `arv_voxel`

Voxel and block model definitions.

Responsibilities:

* block registry
* block state definitions
* runtime block IDs
* namespaced stable identifiers
* voxel-related serialization support
* block metadata and lookup infrastructure

Runtime IDs are used for in-memory and network-session efficiency.

Stable namespaced identifiers are used for disk/persistent references.

---

## 9.8 `arv_world`

World runtime and world simulation ownership.

Responsibilities:

* world instances
* dimensions/world containers
* chunk and section management
* block mutation APIs
* world-local scheduled actions
* world query surfaces
* world-bound simulation state

A world instance owns:

* chunk storage
* world ECS registry
* world-local systems
* world-local authoritative simulation state

This module is central to simulation ownership.

---

## 9.9 `arv_physics`

Physics and collision systems.

Responsibilities:

* collision queries
* AABB movement
* raycasting
* controller movement logic
* broad/narrow query helpers
* physics-side world query interfaces

`arv_physics` should depend on abstract query interfaces where possible, not directly on `arv_world`.

---

## 9.10 `arv_sched`

Scheduling primitives and execution infrastructure.

Responsibilities:

* task execution primitives
* worker/job orchestration
* scheduling APIs
* delayed/repeating task primitives
* distinction between tick work and background work

`arv_sched` provides infrastructure, not ownership of gameplay state.

---

## 9.11 `arv_render`

Rendering and visual pipeline.

Responsibilities:

* chunk meshing
* material/texture systems
* frame pipeline
* transparent/opaque passes
* GPU resource orchestration
* visual representation of world and entity data

Rendering consumes authoritative or replicated state and produces presentation output. It does not own gameplay truth.

---

## 9.12 `arv_server`

Server runtime composition layer.

Responsibilities:

* session management
* player directory
* world registry
* cross-world routing
* commands/permissions
* plugin lifecycle
* server-global services
* integrated/dedicated server composition

This is where server-global logic should live.

---

## 9.13 `arv_client`

Client runtime composition layer.

Responsibilities:

* protocol consumption
* input forwarding
* client-side replication state
* interpolation/presentation state
* client runtime glue
* rendering/audio integration

This is where presentation-side runtime behavior should live.

---

## 10. World and dimension model

Arvane supports multiple worlds/dimensions under a single server runtime.

A world is a simulation container with its own:

* chunk state
* ECS registry
* world systems
* scheduled world actions

The architecture should also allow for future **instanced worlds** without requiring a redesign of the entire runtime model.

Instancing details belong in a separate document, but the core architecture should assume that worlds are not limited to a single hardcoded “overworld/nether/end” style layout.

---

## 11. Dependency philosophy

Dependencies should follow ownership and capability boundaries.

### Good dependency

A module depends on another because it genuinely uses its owned concepts.

Example:

* `arv_server` depending on `arv_world` is natural because server runtime owns loaded worlds

### Prefer interface instead

A module only needs a narrow capability, not full ownership semantics.

Example:

* `arv_physics` needs collision/solidity queries, not the full world implementation

### Avoid

* circular dependencies
* convenience-driven coupling
* “everything depends on everything” architecture
* leaking world internals into unrelated modules

---

## 12. Plugin model

Plugins provide game and content logic on top of engine infrastructure.

Plugins may register or define:

* blocks
* entities
* items
* systems
* commands
* worldgen behavior
* gameplay rules

Plugins should use engine-facing APIs for:

* scheduling
* world access
* entity operations
* data registration

Plugins should not bypass engine ownership or mutate authoritative state from arbitrary background threads.

---

## 13. Save and persistence policy

Arvane distinguishes between runtime-efficient identifiers and persistent identifiers.

### Runtime

* compact runtime IDs
* optimized for memory and networking

### Persistent

* stable namespaced identifiers
* suitable for disk and long-term compatibility

The detailed save format and migration/versioning strategy belong in a dedicated document.

---

## 14. Future architecture considerations

The core architecture should leave room for:

* additional rendering backends
* richer instancing models
* scripting layers if later desired
* replay/debug tooling
* dedicated save format/versioning documents
* more advanced world services
* protocol evolution over time

These should be layered on top of current architecture rather than forcing a rewrite of core ownership boundaries.

---


## 15. Dedicated Server Signal and Lifetime Policy

The dedicated server must have a clear process lifetime model.

This policy defines how the server starts, runs, handles shutdown requests, saves state, and exits.

The dedicated server should be able to run without a graphics context, window, renderer, or local client.

## Dedicated Server Responsibilities

A dedicated server owns:

* server process lifetime
* server configuration
* world registry
* player session registry
* networking backend
* protocol state
* loaded worlds
* ticking and simulation scheduling
* save/load lifecycle
* plugin lifecycle, when available
* logging and diagnostics

A dedicated server does not own:

* renderer state
* local player camera
* client-only input
* client-only UI
* OpenGL resources
* local audio presentation

Dedicated server code must not depend on renderer initialization.

## Server Lifetime States

The dedicated server should have explicit lifetime states.

Recommended states:

```text
starting
loading_config
loading_registries
loading_worlds
running
stopping
saving
stopped
failed
```

The server should only accept client gameplay connections once it reaches `running`.

Some diagnostic or status queries may be allowed earlier, but gameplay should not begin until configuration, registries, and required worlds are loaded.

## Startup Flow

A typical dedicated server startup should follow:

```text
parse command line
load configuration
initialize logging
load registries and assets needed by server logic
initialize save system
load or create the configured server root
initialize world registry
load or create the `server.yaml` `world`
initialize networking
start accepting connections
enter main server loop
```

Renderer, windowing, GPU resources, local input, and local UI must not be part of dedicated server startup.

## Main Server Loop

The dedicated server main loop should coordinate:

* network polling
* protocol/session event processing
* world ticking
* scheduled jobs
* chunk generation/IO completion
* save scheduling
* plugin hooks, when available
* graceful shutdown checks

The loop should avoid blocking indefinitely on any single subsystem.

Long-running work should be moved to jobs or explicit staged processing where practical.

## Signal Handling

The dedicated server should handle process shutdown signals gracefully where the platform allows it.

Typical shutdown triggers include:

```text
SIGINT
SIGTERM
console stop command
internal fatal error
admin remote stop command
```

Signal handlers should do the minimum amount of work necessary.

A signal handler should not directly save worlds, destroy networking, unload plugins, or perform complex logic.

Instead, it should request shutdown through an atomic flag or equivalent safe mechanism.

Conceptually:

```text
signal received
        ↓
set shutdown_requested flag
        ↓
main server loop observes flag
        ↓
server enters stopping state
```

## Graceful Shutdown Flow

A graceful shutdown should follow:

1. Stop accepting new connections.
2. Notify connected clients of shutdown when possible.
3. Stop processing new gameplay commands.
4. Finish or cancel pending critical world operations.
5. Flush important session/player state.
6. Save loaded worlds.
7. Save player data.
8. Shut down networking.
9. Shut down job systems.
10. Unload plugins, when available.
11. Close logs and diagnostics.
12. Exit with a success code.

The server should prefer a clean save over immediate termination when possible.

## Forced Shutdown

The server may support forced shutdown for fatal errors, repeated signals, or administrator commands.

Forced shutdown may skip non-critical work, but should still try to avoid corrupting save files.

Save writes should use safe write patterns, such as temporary files plus atomic rename where appropriate.

If forced shutdown prevents a clean save, the server should log this clearly.

## Save Boundaries During Shutdown

World saves and player saves have separate ownership.

During shutdown:

* dedicated player/session data should be saved under `<server-root>/players/`
* singleplayer player/session data should be saved under `arvane_home/saves/<server-save-name>/players/`
* each world should save its own world/chunk data
* server-level metadata should be saved separately
* runtime-only IDs should not be persisted unless explicitly remappable

The shutdown path should respect the save format boundaries defined in `save_format.md`.

## Integrated Server Relationship

The integrated singleplayer server should reuse the same lifetime concepts where practical.

The integrated server may be created and destroyed inside a client process, but it should still have clear startup, running, stopping, saving, and stopped phases.

Client shutdown should request integrated server shutdown instead of directly tearing down world state.

This keeps singleplayer behavior aligned with dedicated server behavior.

## Failure Policy

If startup fails before the server reaches `running`, the server should:

* report a clear error
* clean up initialized subsystems
* avoid creating partial world/session state when possible
* exit with a failure code

If runtime failure occurs after the server is running, the server should attempt graceful shutdown when safe.

Examples of fatal failures:

```text
required registry load failure
save format incompatibility
world registry initialization failure
network backend initialization failure
unrecoverable world corruption
```

The server should not continue running in a partially initialized authoritative state unless the failure is explicitly recoverable.

## Logging Requirements

Server lifetime transitions should be logged.

Important log points include:

* startup beginning
* configuration path used
* loaded worlds
* networking bind address/port
* running state reached
* shutdown requested
* save started
* save completed
* shutdown completed
* fatal failure reason

Logs should make it clear whether the shutdown was graceful or forced.

---

## 16. Summary

Arvane’s architecture is built around a few central truths:

* the **server is authoritative**
* **worlds own world-local simulation**
* **server runtime owns global services**
* **clients are presentation-focused**
* **EnTT powers ECS, but Arvane defines the ECS conventions**
* **reliable UDP transport and protocol semantics are separate concerns**
* **scheduler infrastructure is separate from state ownership**
* **plugins extend behavior through controlled engine APIs**
* **practical determinism is desirable; strict determinism is not required**

This structure is intended to keep the engine scalable, understandable, and suitable for both manual development and documentation-driven AI-assisted implementation.

---
