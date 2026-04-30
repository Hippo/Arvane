# Blocks and Registry

## Purpose

This document defines the block system used by the engine and game layer.

The goals of this design are:

* fully data-driven block definitions
* strong support for modding and extensibility
* fast runtime queries for simulation and rendering
* clean separation between logical block identity and runtime representation
* minimal decoding work in hot paths (meshing, physics, lighting)

This document defines:

* block definitions
* runtime ID model
* air invariants
* variant expansion
* registry loading
* face visibility rules
* tags and properties

---

## Core Principles

### Data-driven definitions

Blocks are defined entirely through data.

The engine does not hardcode block types. Instead, it loads block definitions at startup and builds a runtime registry.

Block definitions describe:

* logical identity
* properties and behavior flags
* tags
* optional state schema

---

### Logical names are stable

Blocks are referenced by stable logical names:

* `arvane:air`
* `arvane:stone`
* `arvane:grass_block`
* `arvane:stone_stairs`

These names are used in:

* authored content
* gameplay logic
* commands and debugging
* save formats (directly or indirectly)

---

### Runtime IDs are not stable

Runtime IDs are assigned during registry load.

They:

* are compact integers
* are optimized for performance
* are not stable across runs or mod sets

Game code must never hardcode numeric IDs.

---

## Runtime Identity Model

The system uses two runtime identifiers.

---

### BlockID

`BlockID` identifies a block **family**.

Examples:

* air
* stone
* grass
* stone_stairs

This is used for:

* tags
* high-level property queries
* gameplay logic
* logical grouping

---

### BlockVariantID

`BlockVariantID` identifies an exact **resolved runtime variant** of a block.

Examples:

* air
* stone
* stone_stairs[facing=north]
* stone_stairs[facing=south]
* stone_stairs[facing=east]
* stone_stairs[facing=west]
* grass

This is what is stored in chunk data.

It is used for:

* meshing
* collision
* occlusion
* rendering
* exact behavior lookup

---

### Relationship

* Each `BlockVariantID` maps to exactly one `BlockID`
* A `BlockID` may own one or many variants

---

### Example

| BlockID | BlockVariantID | Meaning |
|--------|--------------:|--------|
| 0 | 0 | Air |
| 1 | 1 | Stone |
| 2 | 2 | Stairs North |
| 2 | 3 | Stairs South |
| 2 | 4 | Stairs East |
| 2 | 5 | Stairs West |
| 3 | 6 | Grass |

---

### Storage Rule

Chunk cells store:

```

BlockVariantID

```

They do not store:

* logical names
* separate `(BlockID, state_bits)`
* dynamic state objects

All meaning is derived through the registry.

---

## Air Invariants

Air is a special block and must obey strict rules.

---

### Identity

* Logical name: `arvane:air`
* `BlockID(0)` is always Air
* `BlockVariantID(0)` is always Air

No other block or variant may use `0`.

---

### Behavior

Air must always be:

* non-solid
* non-collidable
* non-opaque
* non-light-blocking
* non-occluding
* empty
* safe default for uninitialized cells

---

### Engine implications

Because Air is zero:

* chunks can be zero-initialized
* empty memory is valid world data
* many hot-path checks become trivial (`id == 0`)
* sparse or compressed storage becomes simpler

---

## Block Definitions

A block definition describes a **block family**.

---

### Required fields

* logical name
* default variant
* tags
* base properties

---

### Common properties

Definitions should support:

* `solid`
* `collidable`
* `opaque`
* `blocks_light`
* `emits_light`
* `replaceable`
* `fluid`
* `full_cube`

These may be defined at the block level or overridden per variant if necessary.

---

### Tags

Tags are group memberships used for behavior queries.

Examples:

* `arvane:mineable/pickaxe`
* `arvane:mineable/shovel`
* `arvane:transparent`
* `arvane:logs`
* `arvane:leaves`
* `arvane:fluids`
* `arvane:replaceable`

Tags must be:

* data-driven
* efficiently queryable
* usable in both engine and gameplay code

---

### Item Derivation

Blocks may optionally define whether they should produce a corresponding item in the item registry.

This allows the engine to automatically generate item definitions from block data, reducing duplication and ensuring consistency between blocks and their inventory representation.

---

### `is_item` Property

Blocks may define:

```json
"is_item": true
```

#### Behavior

* `is_item: true`

  * The engine will generate a corresponding item for this block.
  * This is the default behavior.

* `is_item: false`

  * No item will be generated for this block.
  * The block will exist only in world simulation and not in inventories.

If the property is omitted, it is treated as:

```json
"is_item": true
```

---

### Use Cases

Blocks that should typically disable item generation:

* fluids (`water`, `lava`)
* temporary simulation blocks
* technical/internal blocks
* invisible or placeholder blocks

Example:

```json
{
  "name": "arvane:water",
  "render_category": "transparent",
  "textures": "arvane:texture/block/water",
  "properties": {
    "replaceable": true,
    "fluid": true
  },
  "is_item": false
}
```

Fluid speed is not configured on rendering or item data. It belongs to the fluid
definition/timing config, for example:

```yaml
simulation:
  fluids:
    water:
      flow_interval_ticks: 10
    lava:
      flow_interval_ticks: 30
```

---

### Generated Item Behavior

When a block has `is_item: true`, the item registry may automatically derive an item using:

* the block’s logical name
* the block’s visual definition (`model` or `texture`)
* default item properties (stack size, category, etc.)

Example:

```json
{
  "name": "arvane:stone",
  "texture": "stone.png"
}
```

This will produce an item equivalent to:

```json
{
  "name": "arvane:stone",
  "category": "blocks",
  "max_stack_size": <default_block_stack_size>,
  "places_block": "arvane:stone"
}
```

---

### Override Rule

If an explicit item definition exists with the same logical name as a generated block item:

* the explicit item definition overrides configurable fields
* the association with the source block should be preserved unless explicitly disabled

This allows:

* custom display names
* custom stack sizes
* custom rendering
* custom behaviors

without duplicating block definitions.

---

### Engine Responsibilities

During registry loading:

1. load block definitions
2. evaluate `is_item`
3. generate item definitions for eligible blocks
4. merge/override with explicit item definitions
5. finalize item registry

---

### Validation Rules

Registry loading should fail or warn if:

* a generated item references a missing block
* an explicit item conflicts with a non-item block (`is_item: false`)
* duplicate item definitions cannot be resolved deterministically

---

### Important Notes

* Item derivation is a **convenience layer**, not a requirement.
* Blocks remain the source of truth for world representation.
* Items are a separate registry, even when derived from blocks.

---

## Variant Expansion

Block definitions may define **state schemas**.

These are expanded into concrete runtime variants during registry load.

---

### Example (authored)

```yaml
name: arvane:stone_stairs

states:
  facing: [north, south, east, west]
```

---

### Expanded (runtime)

Variants:

* stone_stairs[north]
* stone_stairs[south]
* stone_stairs[east]
* stone_stairs[west]

Each variant receives its own `BlockVariantID`.

---

## Model and Textures

### Overview

Blocks in Arvane are **data-driven** and define their visual representation through either:

1. A **model reference**
2. A **direct texture definition**

These two approaches are **mutually exclusive** per block entry. A block must define **exactly one** of them.

This separation allows simple blocks to remain lightweight, while more complex geometry (stairs, slabs, custom shapes) can use full models.

---

### Model-Based Blocks

A block may specify a `model` component that references a model file located in:

```
assets/models/block/
```

Example:

```json
"model": "stairs.json"
```

#### Behavior

* The model defines:

  * geometry (faces, elements, rotations)
  * UV mapping
  * textures (via the model’s own texture definitions)

* The block registry does **not** define textures when `model` is used.

* Models are responsible for being fully self-contained.

#### Use Cases

Use models for:

* non-cubic blocks (stairs, slabs, fences)
* rotated or directional geometry
* multi-part shapes
* blocks requiring custom UV layouts

---

### Texture-Based Blocks

For simple cube blocks, a `texture` component can be used instead of a model.

#### Single Texture (All Faces)

```json
"texture": "all_sides.png"
```

* Applies the same texture to all six faces.
* Equivalent to a standard cube with uniform material.

---

#### Per-Face Textures

```json
"texture": [
  "top.png",
  "bottom.png",
  "front.png",
  "back.png",
  "left.png",
  "right.png"
]
```

Face order is strictly defined as:

```
[top, bottom, front, back, left, right]
```

#### Behavior

* The engine generates a **default cube model** internally.
* UVs are automatically assigned to map each texture to its respective face.
* Texture references resolve through the material/atlas system.

---

### Validation Rules

The following rules must be enforced during block loading:

* A block must define **exactly one** of:

  * `model`
  * `texture`

* Defining both is invalid.

* Defining neither is invalid.

* If `texture` is:

  * a string → treated as single-texture mode
  * an array → must contain exactly 6 entries

---

### Engine Responsibilities

When loading blocks:

* Resolve `model` paths relative to `assets/models/block/`
* Resolve `texture` paths through the texture/material system
* Register all referenced textures for atlas packing
* Associate the resolved visual data with the block’s **StateID**

---

### Notes

* Texture definitions are **logical references**, not direct GPU resources.
* Atlas packing and UV normalization are handled later in the rendering pipeline.
* This system keeps block definitions simple while allowing full extensibility through models.

---

### Important rule

At runtime:

* state schemas are **not interpreted dynamically**
* variants are **fully resolved ahead of time**

This avoids per-block decoding in hot paths.

---

### Variant responsibilities

Each variant defines:

* exact mesh/geometry
* collision shape
* occlusion behavior
* face visibility rules
* light behavior (if state-dependent)

---

### Block vs Variant responsibilities

**BlockID (family level):**

* logical identity
* tags
* broad properties
* gameplay classification

**BlockVariantID (exact state):**

* geometry
* collision shape
* occlusion
* rendering behavior
* face visibility

---

## Registry Loading

The block registry is built once at startup.

---

### Load pipeline

1. load all block definitions (engine + game + mods)
2. validate definitions
3. ensure `arvane:air` exists
4. expand state schemas into variants
5. assign runtime IDs
6. build lookup tables
7. finalize and freeze registry

---

### Runtime ID assignment

Rules:

* `BlockID(0)` = air
* `BlockVariantID(0)` = air
* all other IDs assigned sequentially
* ordering must be deterministic

Recommended approach:

* sort by logical name before assignment

---

### Immutability

The registry is immutable after load.

It must be:

* thread-safe for read access
* shared across all systems
* never modified during simulation

---

### Validation

Registry loading must fail on:

* duplicate block names
* missing air definition
* invalid state schemas
* invalid variant expansion
* exceeding ID limits

---

## Face Visibility Rules

Face visibility determines whether a face should be emitted during meshing.

---

### Key concept

Face visibility is not simply:

> "visible if neighbor is air"

It must be explicitly defined.

---

### Evaluation model

```
should_emit_face(self_variant, neighbor_variant, face)
```

This may depend on:

* both variants
* face direction

---

### Baseline behavior

* faces against air are visible
* faces between two full opaque cubes are hidden
* faces between identical materials may be hidden
* faces against non-occluding blocks may be visible

---

### Special cases

Some blocks require custom rules:

* glass vs glass (hide internal faces)
* water vs water (hide internal faces)
* leaves vs leaves (configurable)
* partial shapes (stairs, slabs)
* fluids

---

### Important distinction

Do not conflate:

* visibility
* collision
* light blocking

These are separate systems.

---

## Tags and Properties

---

### Tags

Used for classification:

* fast boolean membership checks
* gameplay logic
* tool interaction
* rule grouping

---

### Properties

Used for structured values:

* hardness
* friction
* light emission
* sound type
* fluid type

---

### Rule

* use **tags** for grouping
* use **properties** for values

---

## Query Surface

Systems must be able to query efficiently using `BlockVariantID`.

Examples:

* `is_air(variant)`
* `block_id(variant)`
* `is_solid(variant)`
* `is_opaque(variant)`
* `blocks_light(variant)`
* `emits_light(variant)`
* `has_tag(block_id, tag)`
* `collision_shape(variant)`
* `occlusion_shape(variant)`

Hot-path queries must avoid string lookups.

---

## Serialization Considerations

Runtime IDs are not stable.

Therefore:

* saves must not rely solely on raw IDs
* logical names or registry mappings must be preserved
* worlds should track registry version or fingerprint

---

## Networking Considerations

Client and server must share the same registry.

They must agree on:

* block definitions
* variant expansion
* runtime ID mapping

Recommended:

* validate registry compatibility during connection
* reject mismatches cleanly

---

## Threading Model

The registry is:

* constructed once
* immutable
* safe for concurrent reads

This allows use across:

* meshing threads
* lighting threads
* physics
* generation

with no locking.

---

## Final Rules

* Air is always `BlockID(0)` and `BlockVariantID(0)`
* Runtime IDs are assigned at load time only
* Runtime IDs are not stable across runs
* Block definitions are data-driven
* Chunk storage uses `BlockVariantID`
* Variant expansion happens at load time
* No dynamic state decoding in hot paths
* Registry is immutable after load
* Face visibility must use explicit rules, not assumptions
* Game code must not hardcode numeric IDs
