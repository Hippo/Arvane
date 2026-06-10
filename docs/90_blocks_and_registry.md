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
* `hardness`
* `drops_with_wrong_tool`

These may be defined at the block level or overridden per variant if necessary.

---

### Blast Properties

Blocks may define a `blast` object for server-authoritative explosions:

```json
{
  "blast": {
    "resistance": 4.5,
    "destroyable_by_explosions": true,
    "absorbs_blast": false,
    "flammable": false,
    "drops_items": true
  }
}
```

`resistance` is a non-negative finite value that reduces ray power.
`destroyable_by_explosions` determines whether the explosion may replace the
block with air. `absorbs_blast` stops the ray after this block is encountered.
`drops_items` gates whether normal block drops may be spawned when the block is
destroyed. Air is always non-destructible by explosions, has zero resistance,
and does not drop.

---

### Tags

Tags are group memberships used for behavior queries.

Examples:

* `arvane:mineable/pickaxe`
* `arvane:mineable/shovel`
* `arvane:mineable/hand`
* `arvane:transparent`
* `arvane:logs`
* `arvane:leaves`
* `arvane:fluids`
* `arvane:replaceable`

Tags must be:

* data-driven
* efficiently queryable
* usable in both engine and gameplay code

`arvane:mineable/<tool>` tags are base-pack gameplay data. Generic block and
item registries store the tags, while `arv_base` registers the
`arvane:block_break/mineable_tags` server behavior that interprets them for
mining speed and drop eligibility. Blocks with mineable tags only drop when
broken with a matching tool unless `drops_with_wrong_tool` is enabled, which is
useful for blocks such as dirt that can be collected by hand but break faster
with a shovel.

---

### Behavior IDs

Blocks may declare a compiled behavior binding:

```json
{
  "name": "arvane:water",
  "behavior": "arvane:block_behavior/fluid"
}
```

The block registry stores the authored behavior ID on both the block family and
its resolved variants. Generic block loading does not depend on `arv_base`; it
accepts an optional behavior validation callback. Runtime composition registers
base block behavior IDs into a generic `BlockBehaviorRegistry` before building
the base pack registry. If a block declares a non-empty behavior ID and the
composition-supplied registry does not contain it, startup fails with a block
registry diagnostic.

Behavior IDs are for native hooks that cannot be represented cleanly as pure
data. Ordinary grouping should remain tags, and ordinary physical/rendering
facts should remain properties or model data.

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
  "texture": "arvane:texture/block/stone"
}
```

This will produce an item equivalent to:

```json
{
  "name": "arvane:stone",
  "behavior": "arvane:item_behavior/block",
  "category": "blocks",
  "max_stack_size": <default_block_stack_size>,
  "places_block": "arvane:stone"
}
```

The block-item behavior ID is supplied by runtime composition rather than
hard-coded in the generic item registry. `arv_base` registers
`arvane:item_behavior/block` for the built-in base pack.

Generated block items also inherit the default held-item presentation behavior:

* primary use uses `arvane:swing/default` and plays immediately
* secondary use uses `arvane:swing/default` only when placement is predicted to
  succeed

If a block item needs custom animation behavior, define an explicit item with the
same logical name and override its `held_animations` binding. For example:

```json
{
  "name": "arvane:stone",
  "held_animations": {
    "secondary_use_start": {
      "animation": "arvane:swing/heavy",
      "play_when": "placement_succeeds"
    }
  }
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

## Block States

Block states describe the concrete runtime variants of a block.

A block definition represents the logical block type, such as `arvane:stone_stairs`.
A block state represents one exact placement/configuration of that block, such as:

- `arvane:stone_stairs[facing=north]`
- `arvane:stone_stairs[facing=east]`

The engine does not simulate vague block definitions in the world. The world stores concrete runtime state IDs.

---

## Authored State Schema

Block definitions may declare a `states` object.

Each key is a state property name.
Each value is the list of allowed authored values.

Example:

```json
{
  "name": "arvane:stone_stairs",
  "states": {
    "facing": ["north", "south", "east", "west"],
    "half": ["bottom", "top"]
  }
}
```

This expands into concrete runtime states:

```text
arvane:stone_stairs[facing=north,half=bottom]
arvane:stone_stairs[facing=north,half=top]
arvane:stone_stairs[facing=south,half=bottom]
arvane:stone_stairs[facing=south,half=top]
...
```

The registry loader computes the Cartesian product of all declared state values.

---

## Runtime Block ID vs Runtime State ID

The engine distinguishes between logical blocks and concrete states.

```text
BlockID = logical block type
StateID = concrete block state variant
```

Example:

```text
BlockID 0 = arvane:air
BlockID 1 = arvane:stone
BlockID 2 = arvane:stone_stairs

StateID 0 = arvane:air
StateID 1 = arvane:stone
StateID 2 = arvane:stone_stairs[facing=north,half=bottom]
StateID 3 = arvane:stone_stairs[facing=north,half=top]
StateID 4 = arvane:stone_stairs[facing=south,half=bottom]
...
```

World block storage uses `StateID`, not `BlockID`.

Systems that only care about logical identity may resolve `StateID -> BlockID`.

---

## Default State

Every block has a default state.

If a block has no explicit state schema, it has exactly one state.

```json
{
  "name": "arvane:stone"
}
```

Equivalent runtime result:

```text
arvane:stone
```

For blocks with state schemas, the default state uses either the explicit `default_state` object or the first value listed for each property.

```json
{
  "name": "arvane:stone_stairs",
  "states": {
    "facing": ["north", "south", "east", "west"],
    "half": ["bottom", "top"]
  },
  "default_state": {
    "facing": "north",
    "half": "bottom"
  }
}
```

If `default_state` is omitted:

```json
{
  "facing": "north",
  "half": "bottom"
}
```

is implied because those are the first declared values.

---

## State Property Types

State values are authored as strings, integers, or booleans.

```json
{
  "states": {
    "facing": ["north", "south", "east", "west"],
    "open": [false, true],
    "level": [0, 1, 2, 3, 4, 5, 6, 7]
  }
}
```

The registry preserves the authored property names and values for tools, debugging, serialization metadata, and command usage.

Hot runtime systems should not repeatedly perform string lookups. During registry load, states are converted into compact runtime IDs and compact per-state metadata.

---

## Common State Categories

The engine should not hard-code a fixed global list of state names, but common block state categories include:

### Orientation States

Used for blocks whose behavior or model depends on direction.

```json
{
  "states": {
    "facing": ["north", "south", "east", "west"]
  }
}
```

Examples:

* stairs
* furnaces
* doors
* ladders
* signs
* directional decorative blocks

### Axis States

Used for blocks aligned to one axis.

```json
{
  "states": {
    "axis": ["x", "y", "z"]
  }
}
```

Examples:

* logs
* pillars
* pipes
* beams

### Shape States

Used for blocks whose geometry changes based on neighbors or placement.

```json
{
  "states": {
    "shape": ["straight", "inner_left", "inner_right", "outer_left", "outer_right"]
  }
}
```

Examples:

* stairs
* fences
* walls
* panes
* rails

### Half States

Used for blocks occupying the upper or lower half of a block space.

```json
{
  "states": {
    "half": ["bottom", "top"]
  }
}
```

Examples:

* slabs
* stairs
* trapdoors

### Connection States

Used for blocks that connect to adjacent blocks.

```json
{
  "states": {
    "north": [false, true],
    "south": [false, true],
    "east": [false, true],
    "west": [false, true],
    "up": [false, true],
    "down": [false, true]
  }
}
```

Examples:

* fences
* glass panes
* redstone-like wiring
* pipes
* cables

Connection states are usually derived from neighboring blocks and updated when neighbors change.

### Open / Powered / Active States

Used for interactive or logic-driven blocks.

```json
{
  "states": {
    "open": [false, true],
    "powered": [false, true],
    "active": [false, true]
  }
}
```

Examples:

* doors
* trapdoors
* lamps
* buttons
* levers
* machines

### Fluid Level States

Used for liquid depth and flow.

```json
{
  "states": {
    "level": [0, 1, 2, 3, 4, 5, 6, 7]
  }
}
```

### Growth / Age States

Used for blocks that progress over time.

```json
{
  "states": {
    "age": [0, 1, 2, 3, 4, 5, 6, 7]
  }
}
```

Examples:

* crops
* saplings
* vines
* spreading blocks

### Variant States

Used for authored visual or gameplay variants under one logical block.

```json
{
  "states": {
    "variant": ["normal", "mossy", "cracked"]
  }
}
```

Use `variant` only when the variants are still the same logical block. If variants have meaningfully different gameplay behavior, separate block definitions are preferred.

---

## State Explosion

State schemas should be kept compact.

Every combination of state values becomes a concrete runtime state. For example:

```json
{
  "states": {
    "facing": ["north", "south", "east", "west"],
    "half": ["bottom", "top"],
    "shape": ["straight", "inner_left", "inner_right", "outer_left", "outer_right"]
  }
}
```

Expands to:

```text
4 * 2 * 5 = 40 states
```

Avoid encoding high-cardinality or arbitrary data as block states.

Do not use block states for:

* inventories
* owner UUIDs
* custom names
* large counters
* arbitrary NBT-like data
* machine progress timers
* per-instance metadata

Those belong in block entities or another per-position data system.

---

## State-Driven Engine Behavior

A concrete block state may affect:

* model selection
* mesh generation
* collision shape
* occlusion behavior
* light emission
* light opacity
* fluid behavior
* interaction behavior
* neighbor update behavior
* ticking behavior
* sound/material behavior
* item drop behavior

The registry should expose fast lookup tables indexed by `StateID`.

Example conceptual tables:

```text
state_id -> block_id
state_id -> model_id
state_id -> collision_shape_id
state_id -> occlusion_shape_id
state_id -> light_emission
state_id -> light_opacity
state_id -> material_id
state_id -> behavior_flags
```

Hot systems should use direct table lookups, not string comparisons.

---

## State Overrides

Block definitions may provide per-state overrides for model, collision, lighting, or behavior.

Example:

```json
{
  "name": "arvane:stone_stairs",

  "states": {
    "facing": ["north", "south", "east", "west"],
    "half": ["bottom", "top"]
  },

  "default_state": {
    "facing": "north",
    "half": "bottom"
  },

  "model": {
    "path": "arvane:model/block/stairs",
    "textures": ["arvane:texture/block/cobblestone"]
  },

  "state_overrides": [
    {
      "when": {
        "facing": "north",
        "half": "bottom"
      },
      "model": "arvane:model/block/stairs_north_bottom"
    },
    {
      "when": {
        "facing": "north",
        "half": "top"
      },
      "model": "arvane:model/block/stairs_north_top"
    }
  ]
}
```

Overrides are optional. If no override matches a concrete state, the block-level defaults are used.

The loader must validate that every `when` property exists in the state schema and that every value is legal.

---

## Placement State Resolution

When a player places a block, the engine resolves the logical block into a concrete state.

Example:

```text
held item: arvane:stone_stairs
placement context:
  clicked face = north
  hit position = upper half
  player yaw = east

resolved state:
  arvane:stone_stairs[facing=east,half=top]
```

Placement state resolution may use:

* player look direction
* clicked block face
* hit position inside the target block
* neighboring blocks
* item metadata
* block-specific placement rules

Placement logic must return a concrete `StateID`.

---

## Neighbor-Derived States

Some states are derived from surrounding blocks.

Examples:

* fence connections
* pane connections
* wall height/shape
* stair corner shape
* fluid flow level
* pipe/cable connections

When a block changes, the world should schedule neighbor updates for affected positions. These updates may replace a block state with another state of the same logical block.

Example:

```text
fence[north=false,south=false,east=false,west=false]
```

may become:

```text
fence[north=true,south=false,east=false,west=false]
```

after a compatible block is placed to the north.

Neighbor-derived state updates must be deterministic for a given local world state.

---

## Serialization

Saves must not persist raw runtime `StateID` values as stable long-term identifiers.

Runtime IDs are session-local and may change when registries are rebuilt.

Chunk/region saves should persist block state references through stable block names plus compact local palettes.

Example conceptual palette:

```json
{
  "palette": [
    {
      "name": "arvane:air"
    },
    {
      "name": "arvane:stone"
    },
    {
      "name": "arvane:stone_stairs",
      "properties": {
        "facing": "north",
        "half": "bottom"
      }
    }
  ]
}
```

Chunk block data then references palette indices, not global runtime IDs.

---

## Network Encoding

Network packets may use runtime state IDs after the client and server have agreed on the same registry/session mapping.

The protocol must not assume runtime IDs are stable across sessions.

---

## Validation Rules

The registry loader must reject invalid state definitions.

Invalid examples:

* duplicate state property names
* empty state value arrays
* duplicate values inside one property
* `default_state` references an unknown property
* `default_state` uses a value not listed in the schema
* `state_overrides.when` references an unknown property
* `state_overrides.when` uses an illegal value
* generated state count exceeds configured limits
* duplicate concrete state definitions after normalization

`arvane:air` must always exist and must always resolve to runtime `StateID = 0`.

---

## Recommended Initial State Names

The following names are recommended for consistency:

```text
facing   = north | south | east | west | up | down
axis     = x | y | z
half     = bottom | top
shape    = straight | inner_left | inner_right | outer_left | outer_right
open     = false | true
powered  = false | true
active   = false | true
waterlogged = false | true
level    = integer fluid/growth level
age      = integer growth level
variant  = authored visual/gameplay variant
```

Custom state names are allowed, but common concepts should reuse these names unless there is a strong reason not to.

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

A block may specify a `model` component that references a model asset ID. The default
`arvane:model/block/...` namespace resolves under:

```
assets/models/block/
```

Example:

```json
"model": "arvane:model/block/stairs"
```

OR

```json
"model": {
  "path": "arvane:model/block/stairs",
  "textures": ["arvane:texture/block/cobblestone"]
}
```

`model.textures` is the input texture array for the model. A model texture entry such as
`"$0"` resolves to index `0` from this array. See [model format](113_model_format.md).

#### Behavior

* The model defines:

  * geometry (faces, elements, rotations)
  * UV mapping
  * textures (via the model’s own texture definitions)

* The block registry does not use top-level `texture` / `textures` fields when `model` is used.

* Models may be fully self-contained with specific texture IDs, or may use `"$0"`, `"$1"`,
  etc. to receive texture inputs from the block definition.

* If `model.textures` is present, the first input texture is used as the block
  particle texture unless a future format supplies a more specific particle
  field.

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
"texture": "arvane:texture/block/stone"
```

* Applies the same texture to all six faces.
* Equivalent to a standard cube with uniform material.
* Also uses the same texture for block dust particles.

---

#### Per-Face Textures

```json
"texture": [
  "arvane:texture/block/grass_top",
  "arvane:texture/block/dirt",
  "arvane:texture/block/grass_side",
  "arvane:texture/block/grass_side",
  "arvane:texture/block/grass_side",
  "arvane:texture/block/grass_side"
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

Object-form `textures` may also include a non-face `particle` slot:

```json
"textures": {
  "top": "arvane:texture/block/grass_top",
  "side": "arvane:texture/block/grass_side",
  "bottom": "arvane:texture/block/dirt",
  "particle": "arvane:texture/block/dirt"
}
```

The `particle` texture is used for block hit, mining, sprint, and break dust.
It is packed into the client particle atlas during asset loading.

---

### Validation Rules

The following rules must be enforced during block loading:

* A renderable block should define one of:

  * `model`
  * `texture`
  * `textures`

* Defining both is invalid.

* Defining neither is valid only for non-rendered blocks such as `arvane:air`.

* If `texture` is:

  * a string → treated as single-texture mode
  * an array -> must contain exactly 6 entries

* If object-form `textures.particle` is present, it must name the block's dust
  texture. If string-form `textures` is used, that same string is the dust
  texture.

---

### Engine Responsibilities

When loading blocks:

* Resolve model IDs such as `arvane:model/block/stairs` through the model asset system
* Resolve texture IDs such as `arvane:texture/block/stone` through the texture asset system
* Register all referenced textures for atlas packing
* Register authored block particle textures for particle atlas packing
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
