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

Arvane is structured around four top-level areas:

```text
engine/      → reusable runtime systems and linked built-in modules
arvane_home/ → client/local runtime packs, assets, data, config, saves
arvane_server/ → dedicated server runtime root
apps/        → executable products
sdk/         → standalone public authoring SDKs
```

---

## 1. Engine Layer (`engine/`)

Defines reusable runtime systems and infrastructure.

* generic modules do **not** define game rules
* does **not** define final products
* is designed to be reusable and extensible
* `engine/arv_base/` is the linked built-in module for the `arvane_base` pack,
  not a generic framework module

---

## 2. Runtime Home (`arvane_home/`)

Stores client/local runtime content and user state.

* `packs/arvane_base/` contains data and assets for the built-in `arvane`
  namespace
* `packs/enabled.yaml` selects the active pack stack
* `config/` contains client/local config
* `saves/` contains singleplayer integrated-server save roots
* `cache/server-packs/` stores verified downloaded multiplayer pack bundles
* C++ source code does not live in this directory

Dedicated servers use `arvane_server/` for authoritative packs, worlds, cache,
logs, and `server.yaml`.

---

## 3. Application Layer (`apps/`)

Defines runnable products.

* composes generic engine systems, `arv_base`, and runtime packs
* defines entrypoints
* defines runtime configuration and bootstrapping

---

## 4. SDK Layer (`sdk/`)

Defines standalone public SDKs that external authoring code can consume without
linking engine modules.

* `sdk/arvane_plugin/` defines the native plugin ABI and `Arvane::SDK`
* SDK headers must not include engine runtime headers. Standalone,
  SDK-compatible engine headers may be exported deliberately, such as
  `arv/io/codec.hpp`.
* SDK targets provide compile-time authoring support, not engine runtime code

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
├─ arvane_server/
│  ├─ server.yaml
│  ├─ packs/
│  ├─ worlds/
│  ├─ cache/
│  └─ logs/
├─ tests/
├─ examples/
├─ sdk/
│  └─ arvane_plugin/
├─ engine/
│  ├─ arv_core/
│  ├─ arv_platform/
│  ├─ arv_io/
│  ├─ arv_asset/
│  ├─ arv_reflect/
│  ├─ arv_ecs/
│  ├─ arv_sched/
│  ├─ arv_voxel/
│  ├─ arv_blueprint/
│  ├─ arv_world/
│  ├─ arv_physics/
│  ├─ arv_entity/
│  ├─ arv_player/
│  ├─ arv_protocol/
│  ├─ arv_net/
│  ├─ arv_persistence/
│  ├─ arv_plugin/
│  ├─ arv_render/
│  ├─ arv_ui/
│  ├─ arv_client/
│  ├─ arv_server/
│  └─ arv_base/
├─ apps/
│  ├─ arv_desktop/
│  └─ arv_dedicated/
└─ sandbox/
```

## Runtime Home Directory

`arvane_home/` is the runtime home directory for the engine/game.

In development, it lives at the project root by default.

It contains:

* `packs/`
* `config/`
* `saves/`
* optional `logs/`
* optional `cache/`

`saves/` contains **singleplayer server save directories**, not individual
world directories. Dedicated-server worlds live under `arvane_server/worlds/`.

`config/server_list.json` stores the desktop client's saved multiplayer server
entries. It is client UI configuration, not world or server-save data.

Example:

```text
arvane_home/
  packs/
    enabled.yaml
    arvane_base/
      pack.yaml
      assets/
        arvane/
      data/
        arvane/
  config/
    server_list.json
  saves/
    server-save-1/
      players/
      worlds/
        default/
  cache/
    server-packs/

arvane_server/
  server.yaml
  packs/
    enabled.yaml
    arvane_base/
  worlds/
    default/
      pack-lock.json
      world.json
      regions/
  cache/
    client-pack-exports/
  logs/
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
* `DataValue` and `DataDocument`
* YAML, JSON, and binary codec backends
* plugin config and persistent data storage backends
* safe resource-key to storage-path mapping

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
* current player block selections
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

## `arv_blueprint`

Shared Arvane Blueprint document support.

* blueprint data model
* `.abp` text format encode/decode
* palette plus RLE block storage
* owner-scoped storage key helpers

Blueprint commands, permissions, and user workflow live in `arv_base`; this
module owns only the reusable format and codec surface.

---

## `arv_plugin`

Plugin loader and engine-side SDK bridge.

* loading/unloading
* lifecycle
* construction of `ArvAPI` function tables
* conversion between SDK ABI types and engine internals
* registration hooks

The plugin authoring surface lives in `sdk/arvane_plugin/`, not in this engine
module. External plugins link `Arvane::SDK`; they do not link `arv_plugin`.

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

## `arv_ui`

Player-facing UI framework and screens.

### Responsibilities

* retained UI tree, layout, input routing, and draw extraction
* reusable gameplay widgets
* full screen/menu flows
* command/data-source interfaces for app/runtime integration

### Internal Structure

```text
arv_ui/
  core/      → framework primitives
  screens/   → full menu/screen flows
  widgets/   → reusable HUD and gameplay UI components
```

### Constraints

* does not own gameplay/runtime state
* emits commands instead of starting servers or mutating saves directly
* app/runtime code supplies data sources and handles commands

---

## `arv_client`

Client runtime library.

### Responsibilities

* input handling
* camera
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

# BUILT-IN PACK MODULE

## `arv_base`

### Purpose

Compiled behavior provider for the built-in `arvane_base` pack.

### Responsibilities

* stable `arvane` IDs used by base behavior
* built-in game-mode registration
* built-in block and item behavior ID registration
* built-in entity behavior ID registration
* built-in player entity definition
* base starter inventory defaults
* base command implementations
* first-class selection and blueprint command implementations

### Constraints

* generic modules must not depend on `arv_base`
* application/runtime composition registers `arv_base` into generic extension
  registries before any dependent registry construction or runtime startup
* runtime data and assets stay under `arvane_home/packs/arvane_base/`
* `arv_base` is linked into products; it is not loaded as a dynamic plugin

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
generic engine modules → arv_base/app composition → apps
```

* generic systems do not depend on `arv_base`
* `arv_base` depends on generic registries/frameworks and registers built-in
  behavior into them
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

* **generic engine systems separated from built-in gameplay behavior**
* **clear layering: generic engine modules → arv_base/app composition → apps**
* **runtime vs product distinction**
* **world-local simulation ownership**
* **minimal global systems**
* **interface-driven architecture**
* **plugin-first extensibility**
