This is a really strong direction now—this v3 is where the architecture starts to feel *intentional* instead of just “well-organized.”

Below is a clean, tightened **Project Structure Document v3** with:

* `engine/`, `game/`, `apps/` split
* `arv_desktop` rename
* strict separation between runtime libraries vs products
* clarified layering rules
* zero timeline language

---

# Arvane Project Structure (v3)

## Purpose

This document defines the structure of the Arvane codebase.

It establishes:

* module boundaries
* ownership rules
* dependency direction
* system responsibilities

The goal is to ensure the project remains:

* modular
* scalable
* performant
* maintainable
* extensible

This document is **timeless** and describes architecture, not implementation order.

---

# Core Architectural Model

Arvane is structured into three layers:

```text
engine/   → foundational runtime systems
game/     → built-in gameplay rules
apps/     → executable products
```

---

## 1. Engine Layer (`engine/`)

Defines reusable runtime systems and infrastructure.

* does **not** define game rules
* does **not** define final products
* is designed to be reusable and extensible

---

## 2. Game Layer (`game/`)

Defines the built-in Arvane gameplay.

* uses engine systems
* defines rules, mechanics, interactions
* can be replaced or extended in the future

---

## 3. Application Layer (`apps/`)

Defines runnable products.

* composes engine + gameplay
* defines entrypoints
* defines runtime configuration and bootstrapping

---

# Repository Structure

```text
arvane/
├─ docs/
├─ tools/
├─ arvane_home/
│  ├─ assets/
│  ├─ plugins/
│  ├─ config/
│  ├─ saves/
│  ├─ logs/
│  └─ cache/
├─ tests/
├─ examples/
├─ engine/
│  ├─ arv_core/
│  ├─ arv_platform/
│  ├─ arv_io/
│  ├─ arv_asset/
│  ├─ arv_reflect/
│  ├─ arv_ecs/
│  ├─ arv_sched/
│  ├─ arv_voxel/
│  ├─ arv_world/
│  ├─ arv_physics/
│  ├─ arv_entity/
│  ├─ arv_player/
│  ├─ arv_protocol/
│  ├─ arv_net/
│  ├─ arv_persistence/
│  ├─ arv_plugin/
│  ├─ arv_render/
│  ├─ arv_client/
│  └─ arv_server/
├─ game/
│  └─ arv_gameplay/
├─ apps/
│  ├─ arv_desktop/
│  └─ arv_dedicated/
└─ sandbox/
```

## Runtime Home Directory

`arvane_home/` is the runtime home directory for the engine/game.

In development, it lives at the project root by default.

It contains:

* `assets/`
* `plugins/`
* `config/`
* `saves/`
* optional `logs/`
* optional `cache/`

`saves/` contains **server save directories**, not individual world directories.

Example:

```text
arvane_home/
  saves/
    server-save-1/
      players/
      worlds/
        default/
```

Code should not hardcode root-relative runtime paths outside the central filesystem/path utility.

---

# Module Definitions

## ENGINE MODULES

---

## `arv_core`

### Purpose

Foundational utilities and shared types.

### Responsibilities

* math primitives (vectors, matrices, AABB, transforms)
* fixed-point utilities (if used engine-wide)
* logging
* assertions
* error/result handling
* hashing and identifiers
* general low-level utilities

### Constraints

* no domain logic
* no gameplay logic
* minimal dependencies

---

## `arv_platform`

Platform abstraction layer.

* OS-specific functionality
* timing
* threading helpers
* dynamic library loading
* filesystem/environment access

---

## `arv_io`

General IO systems.

* file access
* streams
* virtual filesystem concepts
* config loading helpers

---

## `arv_asset`

Asset/resource system.

* asset registry
* asset handles
* loading (textures, shaders, definitions)
* caching/lifecycle

---

## `arv_reflect`

Runtime metadata and type registration system.

### Responsibilities

#### Type metadata

* type IDs
* size/alignment
* type classification

#### Field introspection

* named fields
* type info
* metadata (optional/default/range)

#### Registration

* components
* assets
* packets
* events
* plugin-defined types

#### Enum support

* enum ↔ string conversion
* introspection

#### Factory hooks

* optional runtime construction

#### Serialization metadata

* persistent/transient flags
* version hints

#### Debug tooling support

* generic inspection utilities

### Constraints

* not required for all types
* not a dynamic object system
* used only where it provides real value

---

## `arv_ecs`

ECS infrastructure.

* registry helpers
* system helpers
* query utilities
* ECS-related patterns

Each world owns its own registry.

---

## `arv_sched`

Task scheduling and execution.

* task graphs
* worker execution
* dependency orchestration
* async jobs

### Constraints

* does not own simulation state
* does not define gameplay logic

---

## `arv_voxel`

Voxel domain.

* block runtime IDs
* block states
* chunk/section storage
* storage formats (packed/raw)
* visibility logic
* lighting data
* voxel queries

---

## `arv_world`

World simulation.

* world instance ownership
* ECS ownership
* chunk management
* simulation ticking
* world events
* world-local services

World is the primary simulation container.

---

## `arv_physics`

Physics systems.

* collision detection
* AABB resolution
* terrain-aware movement
* raycasting

### Dependency rule

Uses narrow interfaces, not full world implementation.

---

## `arv_entity`

Shared entity constructs.

* common components
* entity utilities
* shared entity logic

---

## `arv_player`

Global player system.

* player profiles
* sessions
* connection mapping
* permissions
* cross-world lookup

### Model

* global player/session
* world-local entity

---

## `arv_protocol`

Network protocol definitions.

* packet schemas
* serialization/deserialization
* versioning

---

## `arv_net`

Networking implementation.

* connections
* packet transport
* reliability
* session integration

---

## `arv_persistence`

Persistence systems.

* world/chunk storage
* player data
* save formats
* migrations

---

## `arv_plugin`

Plugin system.

* loading/unloading
* lifecycle
* API exposure
* registration hooks

---

## `arv_render`

Rendering systems.

* rendering backend (OpenGL initially)
* mesh handling
* shaders/materials
* texture systems
* world render extraction
* frame pipeline

---

## `arv_client`

Client runtime library.

### Responsibilities

* input handling
* camera
* UI/HUD hooks
* interpolation/prediction
* client networking integration
* render coordination

### Important

This is **not a final application**.

---

## `arv_server`

Server runtime library.

### Responsibilities

* authoritative simulation
* world manager integration
* player/session systems
* tick loop
* networking integration
* persistence integration
* plugin integration

### Important

This is **not a final executable**.

---

# GAME MODULE

## `arv_gameplay`

### Purpose

Built-in Arvane gameplay rules.

### Responsibilities

* block interactions
* item usage
* inventory systems
* combat/damage rules
* interaction mechanics
* core gameplay behavior

### Constraints

* depends on engine
* engine must not depend on gameplay

---

# APPLICATION MODULES

## `arv_desktop`

### Purpose

Desktop/game application product.

### Responsibilities

* executable entrypoint
* platform/window setup
* client runtime composition
* gameplay integration
* optional integrated server
* configuration/bootstrap

### Role

Composition root for the playable game.

---

## `arv_dedicated`

### Purpose

Dedicated server product.

### Responsibilities

* executable entrypoint
* headless runtime
* server runtime composition
* gameplay integration
* plugin loading
* configuration/bootstrap

### Role

Production server host.

---

# Dependency Rules

## Layer Direction

```text
engine → game → apps
```

* engine does not depend on game
* game does not depend on apps
* apps compose everything

---

## Runtime vs Product Separation

```text
arv_client  → runtime library
arv_server  → runtime library

arv_desktop → product
arv_dedicated → product
```

---

## Key Constraints

### 1. No circular dependencies

Extract interfaces instead.

---

### 2. Prefer narrow interfaces

Avoid exposing full systems unnecessarily.

---

### 3. Rendering is downstream

Rendering reads simulation state.

---

### 4. Protocol ≠ transport

Keep schemas separate from networking implementation.

---

### 5. Global vs world-local separation

Never blur:

* global player/session
* world-local entity

---

# Ownership Model

## Global Systems

* player sessions
* world manager
* networking
* persistence
* plugins

---

## World Systems

Each world owns:

* ECS registry
* simulation state
* entities
* chunk data
* world-local systems

---

# Multi-World Model

```text
ServerRuntime
├─ PlayerService
├─ WorldManager
│  ├─ World A
│  ├─ World B
│  └─ World C
├─ NetworkService
├─ PersistenceService
└─ PluginManager
```

---

# Summary

Arvane is built around:

* **engine/runtime separation from gameplay**
* **clear layering: engine → game → apps**
* **runtime vs product distinction**
* **world-local simulation ownership**
* **minimal global systems**
* **interface-driven architecture**
* **plugin-first extensibility**
