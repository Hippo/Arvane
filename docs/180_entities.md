# Entities

This document defines the non-block world simulation model for Arvane.

Entities are runtime objects that exist inside a world but are not part of chunk block storage.

Examples include:

- players
- dropped items
- mobs
- projectiles
- vehicles
- temporary effects
- interactable non-block objects

Entities must not be treated as blocks, chunk data, or renderer-only objects.

---

## Core Rules

- Entities are owned by a world instance.
- Runtime entities are represented using EnTT.
- Each world owns its own ECS registry.
- Entities may reference world positions, chunk positions, and block positions.
- Entities do not own chunk storage.
- Chunks do not own entities.
- Entity simulation may be suspended outside the active simulation range.
- Entity persistence is separate from chunk block persistence.

---

## Ownership Model

Each world owns:

- an EnTT registry
- entity lifecycle state
- entity simulation systems
- entity persistence queues
- entity spatial indexing
- entity-to-chunk relevance tracking

Entities belong to exactly one world at a time.

An entity may move between worlds only through an explicit transfer operation.

Player handling is split between:

- global player profile/session state
- world-local player entity state

The global player profile is not an ECS entity.

---

## ECS Usage

Arvane uses EnTT for runtime entity storage and queries.

The ECS should store runtime simulation data such as:

- transform
- velocity
- item stack payload for dropped item entities
- physics body
- inventory reference
- health
- AI state
- render representation
- interaction state
- lifetime timers

The ECS should not store:

- chunk block arrays
- global registries
- asset definitions
- stable authored entity definitions
- long-lived account/profile data

Entity definitions should come from registries or authored data.

Runtime ECS components are instances of those definitions.

Dropped items use the stable entity type name `arvane:dropped_item` and keep
their item stack, pickup delay, transform, velocity, network identity, and
persistence identity on the same world entity. The server should replicate and
persist dropped items from this entity state rather than maintaining a separate
server-only dropped-stack list.

---

## Authored Entity Definitions

Authored entities are **pure data definitions** describing how an entity is constructed at spawn time.

They do not contain logic. All runtime behavior is derived from:

* ECS components
* behavior configuration (see Entity Behavior)
* engine systems

Entity definitions are loaded from:

```text
data/<namespace>/entities.json in enabled packs
```

The built-in entity definitions live at
`arvane_home/packs/arvane_base/data/arvane/entities.json`.

---

## File Shape

Preferred format:

```json
{
  "entities": [
    {
      "name": "arvane:pig",
      "display_name": "Pig",
      "category": "passive",

      "components": {
        "arvane:transform": {},
        "arvane:collision": { "box": [0.9, 0.9, 0.9] },
        "arvane:movement": { "walk_speed": 0.2 },
        "arvane:health": { "max": 10 },

        "arvane:brain": {
          "behavior": "arvane:pig_behavior"
        }
      },

      "presentation": {
        "model": "arvane:model/entity/pig",
        "animation": "arvane:animation/pig"
      },

      "spawnable": true,
      "persistent_by_default": true,
      "replicated_by_default": true
    }
  ]
}
```

The loader also accepts:

* a root array of entity objects
* a single root object (for small tests) containing a `name`

---

## Core Fields

### `name`

Required stable identifier:

```text
namespace:name
```

Examples:

```text
arvane:pig
arvane:zombie
plugin:custom_npc
```

Rules:

* Must be globally unique
* Must be stable across versions for save compatibility
* Used for serialization and networking

---

### `display_name`

Optional user-facing label for debug tools and UI.

Not used in simulation.

---

### `category`

Optional grouping string for debug/editor tooling.

Examples:

```text
passive
hostile
ambient
utility
```

No gameplay meaning.

---

## Components

### `components`

Defines the ECS component set applied when the entity is spawned.

```json
"components": {
  "arvane:transform": {},
  "arvane:movement": { "walk_speed": 0.2 },
  "arvane:health": { "max": 10 }
}
```

Rules:

* Keys are **component type IDs**
* Values are **component initialization data**
* Missing components are not added implicitly
* Systems operate purely on component presence and data
* No component implies no participation in that system

### Component Principles

* Components define **state**, not behavior logic
* Components must be **data-only**
* Components must be serializable where applicable
* Component schemas are owned by their respective modules

Examples:

```text
arvane:transform
arvane:velocity
arvane:collision
arvane:movement
arvane:health
arvane:brain
arvane:inventory
```

---

## Behavior Binding

Behavior is attached via components, not via entity types.

Example:

```json
"arvane:brain": {
  "behavior": "arvane:pig_behavior"
}
```

This links the entity to a data-defined behavior (see Entity Behavior document).

Rules:

* Entity definitions do not define behavior logic inline
* Behavior is referenced by ID
* Behavior execution is handled by ECS systems
* Multiple entities may share the same behavior definition

---

## Presentation

### `presentation`

Defines non-simulation data used for rendering and client extraction.

```json
"presentation": {
  "model": "arvane:model/entity/pig",
  "animation": "arvane:animation/pig"
}
```

Rules:

* Presentation data must not affect simulation
* Safe to change without breaking gameplay logic
* Extracted by render systems, not simulation systems
* `animation` is optional

---

### `model`

Model asset reference.

Supported forms:

```json
"model": "pig.json"
```

or:

```json
"model": "arvane:model/entity/pig"
```

Short names resolve under:

```text
assets/<namespace>/models/entity/
```

Preferred format is full asset ID.

### `animation`

Optional animation asset reference.

```json
"animation": "arvane:animation/pig"
```

Animation references resolve under:

```text
assets/<namespace>/animations/
```

Animation data is client presentation data. It is not simulation behavior and
does not change authoritative entity state.

---

## Spawn / Lifecycle Flags

### `spawnable`

Whether debug tools or commands may spawn this entity type.

Does not affect gameplay spawning systems.

---

### `persistent_by_default`

Default persistence flag for newly spawned entities.

Controls whether the entity is saved to disk unless overridden.

---

### `replicated_by_default`

Default replication flag.

Controls whether the entity is replicated to clients unless overridden.

---

## Design Constraints

### No Entity Classes

The engine must not implement:

```text
class Zombie : public Entity
class Pig : public Entity
```

There is no per-entity-type logic class.

---

### No Per-Entity Tick Methods

Entities must not define:

```cpp
void Zombie::tick()
```

All logic is executed by systems operating on components and behavior data.

---

### Composition Over Inheritance

Entities are defined by composition:

```text
Entity = Components + Behavior + Data
```

Not:

```text
Entity = Subclass hierarchy
```

---

### Data Ownership

* Entity definitions define **initial state**
* ECS owns **runtime state**
* Systems own **execution**
* Behavior definitions own **decision logic structure**

---

## Built-In Entities

### `arvane:player`

Reserved entity type.

Rules:

* Always present
* Stable entity type ID
* Not spawnable via debug tools
* May have additional engine-managed components
* Still follows ECS + component rules

---

## Validation

Entity loading must validate:

* required fields (`name`)
* component IDs exist
* component data schema is valid
* behavior references exist
* asset references resolve

Invalid definitions must fail fast with clear diagnostics.

---

## Summary

Entity definitions are:

```text
data-only
component-driven
behavior-referenced
render-separated
fully ECS-compatible
```

They do not contain:

```text
logic
tick functions
hardcoded behavior
engine-side subclasses
```

This ensures:

* consistency with ECS architecture
* strong data-driven workflows
* easy modding and extension
* clean separation between data and execution


---

## Entity Model Format

Entity model JSON files define textured cuboid geometry using the same authored
model schema as block models.

Model files live under:

```text
assets/<namespace>/models/entity/
```

Entity model textures should use texture asset IDs such as `arvane:texture/entity/pig`.
Short texture filenames such as `pig.png` remain supported and resolve under:

```text
assets/<namespace>/textures/entity/
```

Entity models contain:

- `textures`: model texture list
- `texture_sizes`: authored UV-space dimensions
- `boxes`: cuboids with per-face texture indices and authored UV rectangles
- `boxes[].bone`: authored group name used by animation keyframes
- `boxes[].pivot`: rotation origin for that authored group

`boxes[].bone == ""` means root or static geometry. Non-empty `bone` values are
names that animation keyframes can target. This is model-side binding metadata,
not a model-owned hierarchy.

Box coordinates and pivots are authored in sixteenths of a block. Runtime
rendering converts them to world units by dividing by 16. Animated transforms are
evaluated on the client presentation side and never mutate authoritative entity
simulation state.

Animations are separate assets under:

```text
assets/<namespace>/animations/
```

Animation clips contain keyframes with a `bone` string, a channel
(`rotation`, `position`, or `scale`), and a vec3 value. Entity definitions bind
animation assets through optional `presentation.animation`.

---

## Entity Identity

Entities have two identities:

### Runtime Entity

The EnTT entity handle is used for runtime ECS operations.

This handle is not stable across saves, loads, or world transfers.

### Persistent Entity ID

Persistent entities have a stable entity ID.

This ID is used for:

- saving
- loading
- networking
- debugging
- cross-system references

Temporary entities do not require persistent IDs unless they need to be replicated or saved.

---

## Transform Rules

Entities use a world-space transform.

A standard entity transform contains:

- position
- rotation
- scale, if needed
- previous position for interpolation
- previous rotation for interpolation

Position is expressed in world coordinates.

Rendering may interpolate between previous and current transforms.

Simulation must use fixed-tick transforms.

Render interpolation must not feed back into simulation state.

---

## Chunk Relevance

Entities are not stored inside chunks, but they may be spatially associated with chunks.

The world maintains an entity spatial index for:

- querying entities near a position
- finding entities inside an area
- determining which entities are relevant to loaded chunks
- networking visibility
- simulation activation

An entity may track its current chunk coordinate as cached metadata.

This cached chunk coordinate must be updated when the entity crosses chunk boundaries.

The cached chunk coordinate is not ownership.

---

## Simulation Range

Entity simulation depends on world activity rules.

Entities inside the active simulation range are ticked normally.

Entities outside the active simulation range may be:

- fully suspended
- lightly ticked
- persisted and unloaded
- handled by a special offline simulation rule

The default behavior is:

- active range: full simulation
- loaded but inactive range: no expensive AI or physics
- unloaded range: entity is saved and removed from runtime ECS unless marked global or always-active

---

## Entity Persistence

Persistent entities must be saved independently from block data.

Entity persistence records should include:

- entity type
- persistent entity ID
- world ID
- position
- rotation
- velocity, if relevant
- component state required for restoration
- inventory state, if relevant
- custom entity data

Entities are saved by world region under:

```text
worlds/<world_name>/entities/r.<region_x>.<region_z>.json
```

The entity region is derived from the entity's world-space position, converted to a block position,
then to a chunk column, then to the standard region coordinate.

Example:

```json
{
  "entity_version": 1,
  "region": { "x": 0, "z": 0 },
  "entities": [
    {
      "uuid": "entity:1:42",
      "type": "arvane:pig",
      "version": 1,
      "world_id": 1,
      "position": [4.0, 20.0, 6.0],
      "rotation": [0.0, 0.0, 0.0, 1.0],
      "velocity": [0.0, 0.0, 0.0],
      "components": {}
    }
  ]
}
```

Runtime ECS handles and transient network IDs must never be written to these files.

Saving entities inside chunk files is allowed only if the format clearly separates block storage from entity storage.

Chunk unload must trigger persistence for relevant entities.

---

## Temporary Entities

Temporary entities include things like:

- particles
- short-lived projectiles
- hit effects
- client-only visuals

Temporary entities may skip persistence.

Client-only entities must never affect authoritative gameplay simulation.

Server-authoritative temporary entities must still follow normal ECS ownership and networking rules.

---

## Update Phases

Entity updates run as part of the fixed world tick.

The default entity update phases are:

1. spawn/despawn flush
2. input command application
3. AI and behavior
4. physics integration
5. collision resolution
6. interaction processing
7. gameplay effects
8. transform finalization
9. spatial index update
10. networking replication collection
11. render extraction

Systems should not freely mutate entities at arbitrary times.

Structural ECS changes should be queued when needed and flushed at defined points.

---

## Physics Integration

Entity physics is separate from block storage.

Physics systems may query the world through an abstract collision interface.

The physics system must not directly depend on chunk internals.

For example, physics may ask:

- is this block solid?
- what collision shape exists at this block position?
- what entities overlap this volume?

Physics must not own or mutate chunk storage.

---

## Interaction Model

Entities can participate in interactions.

Interactions may include:

- player targeting
- attacking
- using
- picking up
- pushing
- riding
- opening UI
- triggering plugin behavior

Entity interaction priority must be coordinated with block interaction priority.

A player raycast may hit both entities and blocks.

The interaction system decides which target wins based on the input and interaction rules.

---

## Networking Rules

On multiplayer servers, the server owns authoritative entity state.

Clients may own local presentation state for responsiveness, but server state is authoritative.

Networked entities require:

- persistent or network entity ID
- spawn message
- despawn message
- replicated component state
- relevance filtering
- interpolation support

Clients must not create authoritative gameplay entities directly.

---

## Debug Spawning

The ImGui debug menu may expose a server-authoritative spawn tool in the `Entities` tab.

Debug spawning must:

- list spawnable authored entity types from the entity registry
- use the entity type's default persistence and replication flags
- create entities through the world/server authoritative path
- keep player entities protected from arbitrary debug despawn
- remain debug-only and not become user-facing gameplay UI

---

## Threading Rules

The ECS registry is world-owned and should be treated as main-simulation-thread state unless explicitly protected.

Background jobs may operate on snapshots.

Allowed background work:

- pathfinding jobs using world snapshots
- entity save serialization
- AI planning snapshots
- broad query preparation
- render extraction upload preparation

Background jobs must not directly mutate the live registry.

Results from jobs are returned to the world and applied during a safe update phase.

---

## Render Extraction

Rendering does not own entities.

The renderer receives extracted render data from the world.

Render extraction should produce:

- mesh/model handle
- transform
- animation state
- material overrides
- visibility flags
- interpolation data

The renderer must not read arbitrary ECS components directly unless explicitly routed through a render extraction layer.

---

## Despawn Rules

Entity despawning must be explicit.

Reasons for despawn include:

- death
- pickup
- timeout
- world unload
- transfer to another world
- conversion into another entity
- administrative removal

Despawn should clean up:

- ECS components
- spatial index entries
- networking state
- ownership references
- pending interactions
- persistence state, if needed

---

## Required Invariants

- EnTT owns runtime entity component storage.
- Worlds own ECS registries.
- Chunks do not own entities.
- Entities do not own chunk block storage.
- Runtime entity handles are not persistent IDs.
- Entity simulation runs on fixed ticks.
- Rendering uses extracted/interpolated state.
- Background jobs operate on snapshots, not the live registry.
- Entity persistence is separate from chunk block storage.

---
