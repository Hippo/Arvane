# World Generation

---

## Overview

This document defines the complete world generation system for Arvane.

World generation is responsible for producing:

* terrain shape
* biome distribution
* caves and underground features
* surface composition
* features and structures

The system must be:

* deterministic
* chunk-independent
* thread-safe
* extensible
* versionable

---

## Core Principles

### Determinism

All generation must be derived from:

* world seed
* world-space coordinates
* generator version

No global mutable state is allowed.

---

### Chunk Independence

Chunks must be generatable:

* in any order
* on any thread
* without neighbor mutation

---

### Stateless Execution

Generation functions must:

* not store persistent state
* not depend on execution order
* recompute results from inputs

---

### Versioning

Each world uses a **generator version**.

This allows:

* terrain changes across updates
* compatibility with existing saves

---

## High-Level Pipeline

Generation occurs in ordered stages:

1. Noise Sampling & Biome Evaluation
2. Base Terrain (Density Field)
3. Surface Layering
4. Carving (Caves)
5. Feature Placement (Ores, Trees)
6. Structure Generation
7. Post Processing (optional)

---

# Terrain Generation

---

## Density Field Model

Terrain is defined using a density function:

```text
density(x, y, z) → float
```

* `> 0` → solid
* `<= 0` → air

This enables:

* overhangs
* caves
* cliffs
* non-heightmap terrain

---

## Density Composition

```text
density =
    base_shape(x, z)
  + terrain_variation(x, z)
  - vertical_falloff(y)
  + detail_noise(x, y, z)
  - cave_subtraction(x, y, z)
```

---

## Domain Warping

To avoid repetition:

```text
(x', z') = (x, z) + warp_noise(x, z)
```

All terrain sampling uses warped coordinates.

---

## Noise Requirements

Noise functions must:

* be deterministic
* be seeded with world seed
* be thread-safe
* support 2D and 3D sampling

Recommended:

* OpenSimplex or similar

---

## Terrain Components

### Base Shape (Continentalness)

* very low frequency
* defines oceans vs land

---

### Terrain Variation

* medium frequency
* defines mountains, hills

---

### Vertical Falloff

* ensures empty sky
* controls terrain height range

---

### Detail Noise

* high frequency
* adds roughness and variation

---

## Cave Generation

Caves subtract from density:

```text
if noise3D(x, y, z) > threshold:
    density -= carve_strength
```

Variants:

* spaghetti caves (tunnels)
* blob caves (caverns)

---

# Biome System

---

## Biome Parameter Space

Biomes are defined using multiple noise dimensions:

* temperature
* humidity
* continentalness (optional)
* erosion (optional)
* weirdness (optional)

---

## Biome Sampling

```text
biome = select_biome(params)
```

Where:

```text
params = {
  temperature,
  humidity,
  ...
}
```

---

## Biome Selection Strategies

Supported:

* grid lookup (simple)
* nearest-point (Voronoi-style)
* parameter blending (advanced)

---

## Biome Effects

Biomes influence:

* terrain shaping modifiers
* surface blocks
* vegetation
* feature distribution

---

## Biome Blending

Future-ready:

* smooth transitions between biomes
* interpolation of terrain parameters

---

# Surface Layering

---

## Overview

Applies biome-specific surface materials.

---

## Rules

* only modifies top layers
* operates after terrain generation
* must not break determinism

---

## Example

* grass → dirt → stone
* sand in deserts
* snow in cold biomes

---

# Feature System

---

## Overview

Features are small-scale, repeatable elements:

* trees
* ores
* plants

---

## Placement Model

Uses **region-based deterministic placement**.

---

## Regions

World is divided into regions:

```text
region_size = NxN (e.g., 32x32 blocks)
```

---

## RNG

Each feature uses:

```text
rng = hash(world_seed, feature_id, region_coord)
```

---

## Placement Steps

For each region:

1. Initialize RNG
2. Generate candidate positions
3. Validate placement
4. Place feature

---

## Validation Rules

* biome match
* terrain suitability
* spacing constraints

---

## Cross-Chunk Behavior

* features may span chunks
* generation must clip safely per chunk
* no duplication across chunk borders

---

# Ore Generation

---

## Overview

Ores are treated as features with specialized placement.

---

## Models

### Blob Deposits

* spherical clusters

---

### Veins

* directional paths through terrain

---

### Stratified Layers

* horizontal bands (rare, biome-specific)

---

## Placement

* deterministic per region
* height constraints
* biome constraints

---

# Structure System

---

## Overview

Structures are large, multi-block features:

* villages
* dungeons
* ruins

---

## Anchoring

Each structure has a deterministic anchor:

```text
anchor = hash(world_seed, structure_id, region_coord)
```

---

## Bounding Boxes

Structures define bounds.

Chunks:

* detect intersection
* generate only overlapping parts

---

## Stateless Generation

Structures must:

* not rely on generation order
* be reproducible from seed

---

## Structure Templates

Structures are defined via:

* templates (block layouts)
* procedural rules (optional)

---

## Jigsaw System (Future)

Supports:

* modular structures
* piece-based assembly
* randomized layouts

---

# Cross-Chunk Consistency

---

## Requirements

* no seams at chunk borders
* consistent feature placement
* shared sampling across boundaries

---

## Rules

* always sample in world space
* never rely on chunk-local randomness
* ensure feature ownership is region-based

---

# Threading Model

---

## Execution

Generation runs on worker threads.

---

## Rules

* no access to live world state
* no locking
* operate only on input + local buffers

---

## Ownership

During generation:

* chunk is exclusively owned by job

After generation:

* handed to world system

---

# Generation Boundaries

---

## Generation Radius

* chunks must be generated

---

## Simulation Radius

* subset of chunks actively simulated

---

# Data Flow Summary

### Input

* world seed
* chunk coordinate

---

### Output

* fully populated chunk:

  * block states
  * biome data
  * features applied

---

# Data-Driven World Generation Definitions

## Overview

World generation configuration is authored in **YAML**.

YAML definitions control:

* generator presets
* biome definitions
* surface rules
* feature placement
* ore distribution
* structure placement

Code owns the execution model. YAML owns tunable content.

YAML must not define arbitrary logic or scripts.

---

## Design Goals

Data-driven generation should allow content changes without recompiling the engine.

Definitions must be:

* deterministic
* validated at load time
* versioned
* registry-backed
* safe for plugins to extend

---

## File Organization

Recommended layout:

```text
data/
  arvane/
    worldgen/
      generators/
        overworld.yaml
      biomes/
        plains.yaml
        forest.yaml
        desert.yaml
      features/
        trees/
          oak_tree.yaml
        ores/
          copper_ore.yaml
      structures/
        ruins/
          small_ruin.yaml
```

Plugins may contribute additional files under their own namespace:

```text
data/
  example_plugin/
    worldgen/
      biomes/
        crystal_forest.yaml
```

---

## Namespaced IDs

All worldgen definitions use namespaced string IDs.

Examples:

```yaml
id: arvane:plains
id: arvane:oak_tree
id: example_plugin:crystal_forest
```

IDs are stable authored identifiers.

Runtime numeric IDs may be assigned internally, but YAML must never depend on runtime IDs.

---

## Generator Preset Definition

A generator preset defines the high-level terrain pipeline for a world.

Example:

```yaml
id: arvane:overworld
version: 1

seed_salt: "overworld"

vertical:
  min_y: -256
  max_y: 512
  sea_level: 64

noise:
  domain_warp:
    enabled: true
    frequency: 0.002
    amplitude: 48.0
    octaves: 2

  continentalness:
    type: open_simplex_2d
    frequency: 0.0008
    octaves: 4
    amplitude: 1.0

  erosion:
    type: open_simplex_2d
    frequency: 0.002
    octaves: 4
    amplitude: 1.0

  detail:
    type: open_simplex_3d
    frequency: 0.035
    octaves: 2
    amplitude: 0.15

density:
  base_height: 72
  height_scale: 96
  vertical_falloff: 1.0
  detail_strength: 12.0

biomes:
  selector: arvane:overworld_biome_selector

features:
  enabled: true

structures:
  enabled: true
```

---

## Noise Definition Rules

Noise entries describe parameters only.

Valid fields:

```yaml
type: open_simplex_2d
frequency: 0.01
octaves: 4
amplitude: 1.0
lacunarity: 2.0
gain: 0.5
salt: "optional_salt"
```

Rules:

* `type` must map to a built-in engine noise implementation.
* `frequency` must be positive.
* `octaves` must be at least `1`.
* `salt` is mixed with world seed to derive a deterministic local seed.
* YAML cannot define custom noise code.

---

## Biome Definition

A biome defines terrain modifiers, surface blocks, and feature lists.

Example:

```yaml
id: arvane:forest
version: 1

display_name: "Forest"

parameters:
  temperature:
    min: 0.35
    max: 0.75
  humidity:
    min: 0.55
    max: 1.0
  continentalness:
    min: -0.2
    max: 1.0

terrain:
  height_offset: 4
  height_scale: 1.05
  roughness: 0.35

surface:
  rules:
    - when:
        above_sea_level: true
      layers:
        - block: arvane:grass_block
          depth: 1
        - block: arvane:dirt
          depth: 3
        - block: arvane:stone
          depth: remaining

features:
  - feature: arvane:oak_tree
    weight: 8
  - feature: arvane:birch_tree
    weight: 3
  - feature: arvane:grass_patch
    weight: 12
```

---

## Biome Selector Definition

A biome selector maps climate/noise parameters to biomes.

Example:

```yaml
id: arvane:overworld_biome_selector
version: 1

parameters:
  temperature:
    noise: temperature
  humidity:
    noise: humidity
  continentalness:
    noise: continentalness

selection:
  mode: weighted_ranges

biomes:
  - biome: arvane:desert
    weight: 1.0
  - biome: arvane:plains
    weight: 1.0
  - biome: arvane:forest
    weight: 1.0
```

Initial implementation may use range matching from each biome’s `parameters`.

If multiple biomes match, choose deterministically by weighted score.

---

## Surface Rule Definition

Surface rules define vertical material replacement after base terrain generation.

Example:

```yaml
surface:
  rules:
    - when:
        above_sea_level: true
        slope_max: 0.65
      layers:
        - block: arvane:grass_block
          depth: 1
        - block: arvane:dirt
          depth: 3
        - block: arvane:stone
          depth: remaining

    - when:
        below_sea_level: true
      layers:
        - block: arvane:sand
          depth: 4
        - block: arvane:stone
          depth: remaining
```

Supported conditions should be explicit and finite:

```yaml
above_sea_level: true
below_sea_level: true
min_y: 0
max_y: 128
slope_min: 0.0
slope_max: 1.0
```

---

## Feature Definition

Features define small-to-medium world elements.

Example tree feature:

```yaml
id: arvane:oak_tree
version: 1

type: tree

placement:
  region_size: 32
  attempts_per_region: 6
  chance: 0.45
  salt: "oak_tree"

constraints:
  biomes:
    - arvane:forest
    - arvane:plains
  min_y: 60
  max_y: 160
  ground_blocks:
    - arvane:grass_block
    - arvane:dirt
  require_sky_access: true
  min_spacing: 4

tree:
  trunk:
    block: arvane:oak_log
    min_height: 4
---

# Data-Driven World Generation Definitions

## Overview

World generation configuration is authored in **YAML**.

YAML definitions control:

* generator presets
* biome definitions
* surface rules
* feature placement
* ore distribution
* structure placement

Code owns the execution model. YAML owns tunable content.

YAML must not define arbitrary logic or scripts.

---

## Design Goals

Data-driven generation should allow content changes without recompiling the engine.

Definitions must be:

* deterministic
* validated at load time
* versioned
* registry-backed
* safe for plugins to extend

---

## File Organization

Recommended layout:

```text
data/
  arvane/
    worldgen/
      generators/
        overworld.yaml
      biomes/
        plains.yaml
        forest.yaml
        desert.yaml
      features/
        trees/
          oak_tree.yaml
        ores/
          copper_ore.yaml
      structures/
        ruins/
          small_ruin.yaml
```

Plugins may contribute additional files under their own namespace:

```text
data/
  example_plugin/
    worldgen/
      biomes/
        crystal_forest.yaml
```

---

## Namespaced IDs

All worldgen definitions use namespaced string IDs.

Examples:

```yaml
id: arvane:plains
id: arvane:oak_tree
id: example_plugin:crystal_forest
```

IDs are stable authored identifiers.

Runtime numeric IDs may be assigned internally, but YAML must never depend on runtime IDs.

---

## Generator Preset Definition

A generator preset defines the high-level terrain pipeline for a world.

Example:

```yaml
id: arvane:overworld
version: 1

seed_salt: "overworld"

vertical:
  min_y: -256
  max_y: 512
  sea_level: 64

noise:
  domain_warp:
    enabled: true
    frequency: 0.002
    amplitude: 48.0
    octaves: 2

  continentalness:
    type: open_simplex_2d
    frequency: 0.0008
    octaves: 4
    amplitude: 1.0

  erosion:
    type: open_simplex_2d
    frequency: 0.002
    octaves: 4
    amplitude: 1.0

  detail:
    type: open_simplex_3d
    frequency: 0.035
    octaves: 2
    amplitude: 0.15

density:
  base_height: 72
  height_scale: 96
  vertical_falloff: 1.0
  detail_strength: 12.0

biomes:
  selector: arvane:overworld_biome_selector

features:
  enabled: true

structures:
  enabled: true
```

---

## Noise Definition Rules

Noise entries describe parameters only.

Valid fields:

```yaml
type: open_simplex_2d
frequency: 0.01
octaves: 4
amplitude: 1.0
lacunarity: 2.0
gain: 0.5
salt: "optional_salt"
```

Rules:

* `type` must map to a built-in engine noise implementation.
* `frequency` must be positive.
* `octaves` must be at least `1`.
* `salt` is mixed with world seed to derive a deterministic local seed.
* YAML cannot define custom noise code.

---

## Biome Definition

A biome defines terrain modifiers, surface blocks, and feature lists.

Example:

```yaml
id: arvane:forest
version: 1

display_name: "Forest"

parameters:
  temperature:
    min: 0.35
    max: 0.75
  humidity:
    min: 0.55
    max: 1.0
  continentalness:
    min: -0.2
    max: 1.0

terrain:
  height_offset: 4
  height_scale: 1.05
  roughness: 0.35

surface:
  rules:
    - when:
        above_sea_level: true
      layers:
        - block: arvane:grass_block
          depth: 1
        - block: arvane:dirt
          depth: 3
        - block: arvane:stone
          depth: remaining

features:
  - feature: arvane:oak_tree
    weight: 8
  - feature: arvane:birch_tree
    weight: 3
  - feature: arvane:grass_patch
    weight: 12
```

---

## Biome Selector Definition

A biome selector maps climate/noise parameters to biomes.

Example:

```yaml
id: arvane:overworld_biome_selector
version: 1

parameters:
  temperature:
    noise: temperature
  humidity:
    noise: humidity
  continentalness:
    noise: continentalness

selection:
  mode: weighted_ranges

biomes:
  - biome: arvane:desert
    weight: 1.0
  - biome: arvane:plains
    weight: 1.0
  - biome: arvane:forest
    weight: 1.0
```

Initial implementation may use range matching from each biome’s `parameters`.

If multiple biomes match, choose deterministically by weighted score.

---

## Surface Rule Definition

Surface rules define vertical material replacement after base terrain generation.

Example:

```yaml
surface:
  rules:
    - when:
        above_sea_level: true
        slope_max: 0.65
      layers:
        - block: arvane:grass_block
          depth: 1
        - block: arvane:dirt
          depth: 3
        - block: arvane:stone
          depth: remaining

    - when:
        below_sea_level: true
      layers:
        - block: arvane:sand
          depth: 4
        - block: arvane:stone
          depth: remaining
```

Supported conditions should be explicit and finite:

```yaml
above_sea_level: true
below_sea_level: true
min_y: 0
max_y: 128
slope_min: 0.0
slope_max: 1.0
```

---

## Feature Definition

Features define small-to-medium world elements.

Example tree feature:

```yaml
id: arvane:oak_tree
version: 1

type: tree

placement:
  region_size: 32
  attempts_per_region: 6
  chance: 0.45
  salt: "oak_tree"

constraints:
  biomes:
    - arvane:forest
    - arvane:plains
  min_y: 60
  max_y: 160
  ground_blocks:
    - arvane:grass_block
    - arvane:dirt
  require_sky_access: true
  min_spacing: 4

tree:
  trunk:
    block: arvane:oak_log
    min_height: 4
    max_height: 7
  leaves:
    block: arvane:oak_leaves
    radius: 2
```

Example ore feature:

```yaml
id: arvane:copper_ore
version: 1

type: ore_blob

placement:
  region_size: 16
  attempts_per_region: 8
  chance: 0.8
  salt: "copper_ore"

constraints:
  min_y: -32
  max_y: 96
  replaceable_blocks:
    - arvane:stone

ore_blob:
  block: arvane:copper_ore
  min_size: 4
  max_size: 12
```

---

## Structure Definition

Structures define larger generated content.

Example:

```yaml
id: arvane:small_ruin
version: 1

type: template_structure

placement:
  region_size: 256
  attempts_per_region: 1
  chance: 0.08
  salt: "small_ruin"

constraints:
  biomes:
    - arvane:plains
    - arvane:forest
  min_y: 55
  max_y: 120
  max_slope: 0.25

template:
  path: "arvane:structures/ruins/small_ruin.arvstruct"
  allow_rotation: true
  allow_mirroring: true

bounding_box:
  size: [12, 8, 12]
```

Structure placement must be anchor-based and region-owned.

Chunks intersecting the structure bounding box generate only their overlapping portion.

---

## Definition Loading

At startup, the engine loads worldgen definitions from:

1. base game data
2. enabled plugins
3. world-specific overrides, if supported

Definitions are validated before gameplay starts.

Invalid definitions must fail loudly with clear diagnostics.

---

## Registry Integration

Worldgen registries should include:

* generator presets
* biome selectors
* biomes
* features
* structures

Definitions may reference:

* blocks
* block states
* features
* biomes
* structure templates

All references must resolve before a world starts.

---

## Validation Rules

Validation must check:

* duplicate IDs
* unresolved references
* invalid block IDs
* invalid biome IDs
* invalid numeric ranges
* invalid noise parameters
* invalid placement rules
* unsupported feature or structure types
* version compatibility

Examples:

```text
ERROR: Feature arvane:oak_tree references unknown block arvane:oak_log
ERROR: Biome arvane:forest has humidity min greater than max
ERROR: Structure arvane:small_ruin has invalid region_size: 0
```

---

## Determinism Rules

YAML values must not introduce nondeterminism.

Rules:

* salts are deterministic strings
* all RNG derives from world seed + salt + region/position
* load order must not affect generation output
* weighted selections must sort candidate IDs before tie-breaking

---

## Plugin Extension Rules

Plugins may add:

* biomes
* features
* structures
* generator presets
* biome selectors

Plugins must not mutate base definitions in-place unless the engine supports explicit override files.

Preferred override model:

```yaml
id: arvane:forest
patch:
  features:
    add:
      - feature: example_plugin:blue_mushroom_patch
        weight: 2
```

Patch support is optional and should be implemented only after base loading is stable.

---

## Schema Stability

Each definition includes:

```yaml
version: 1
```

The engine should reject unsupported versions unless a migration path exists.

Schema changes must be explicit.

---

## Non-Goals

YAML worldgen definitions do not support:

* arbitrary scripting
* custom native code
* unsafe expressions
* runtime mutation of registries
* references to runtime numeric IDs

---

## Implementation Checklist

The first implementation should support:

* YAML parsing
* generator preset loading
* biome loading
* biome selector loading
* feature loading
* structure loading
* reference validation
* deterministic sorted registry insertion
* useful error messages



    max_height: 7
  leaves:
    block: arvane:oak_leaves
    radius: 2
```

Example ore feature:

```yaml
id: arvane:copper_ore
version: 1

type: ore_blob

placement:
  region_size: 16
  attempts_per_region: 8
  chance: 0.8
  salt: "copper_ore"

constraints:
  min_y: -32
  max_y: 96
  replaceable_blocks:
    - arvane:stone

ore_blob:
  block: arvane:copper_ore
  min_size: 4
  max_size: 12
```

---

## Structure Definition

Structures define larger generated content.

Example:

```yaml
id: arvane:small_ruin
version: 1

type: template_structure

placement:
  region_size: 256
  attempts_per_region: 1
  chance: 0.08
  salt: "small_ruin"

constraints:
  biomes:
    - arvane:plains
    - arvane:forest
  min_y: 55
  max_y: 120
  max_slope: 0.25

template:
  path: "arvane:structures/ruins/small_ruin.arvstruct"
  allow_rotation: true
  allow_mirroring: true

bounding_box:
  size: [12, 8, 12]
```

Structure placement must be anchor-based and region-owned.

Chunks intersecting the structure bounding box generate only their overlapping portion.

---

## Definition Loading

At startup, the engine loads worldgen definitions from:

1. base game data
2. enabled plugins
3. world-specific overrides, if supported

Definitions are validated before gameplay starts.

Invalid definitions must fail loudly with clear diagnostics.

---

## Registry Integration

Worldgen registries should include:

* generator presets
* biome selectors
* biomes
* features
* structures

Definitions may reference:

* blocks
* block states
* features
* biomes
* structure templates

All references must resolve before a world starts.

---

## Validation Rules

Validation must check:

* duplicate IDs
* unresolved references
* invalid block IDs
* invalid biome IDs
* invalid numeric ranges
* invalid noise parameters
* invalid placement rules
* unsupported feature or structure types
* version compatibility

Examples:

```text
ERROR: Feature arvane:oak_tree references unknown block arvane:oak_log
ERROR: Biome arvane:forest has humidity min greater than max
ERROR: Structure arvane:small_ruin has invalid region_size: 0
```

---

## Determinism Rules

YAML values must not introduce nondeterminism.

Rules:

* salts are deterministic strings
* all RNG derives from world seed + salt + region/position
* load order must not affect generation output
* weighted selections must sort candidate IDs before tie-breaking

---

## Plugin Extension Rules

Plugins may add:

* biomes
* features
* structures
* generator presets
* biome selectors

Plugins must not mutate base definitions in-place unless the engine supports explicit override files.

Preferred override model:

```yaml
id: arvane:forest
patch:
  features:
    add:
      - feature: example_plugin:blue_mushroom_patch
        weight: 2
```

Patch support is optional and should be implemented only after base loading is stable.

---

## Schema Stability

Each definition includes:

```yaml
version: 1
```

The engine should reject unsupported versions unless a migration path exists.

Schema changes must be explicit.

---

## Non-Goals

YAML worldgen definitions do not support:

* arbitrary scripting
* custom native code
* unsafe expressions
* runtime mutation of registries
* references to runtime numeric IDs

---

## Implementation Checklist

The first implementation should support:

* YAML parsing
* generator preset loading
* biome loading
* biome selector loading
* feature loading
* structure loading
* reference validation
* deterministic sorted registry insertion
* useful error messages

---

# Extension Points

---

The system must support:

* modded biomes
* modded features
* custom generators per world
* new noise layers
* new structure types

---

# Non-Goals (For Now)

---

* perfect biome blending
* runtime scripting
* fully data-driven pipeline

---

# Implementation Rules (Critical)

---

DO:

* use deterministic hashing for RNG
* keep stages isolated
* operate in world space
* ensure thread safety
* validate cross-chunk behavior

---

DO NOT:

* use global RNG
* mutate neighbor chunks
* depend on generation order
* introduce hidden state

---

# Final Summary

---

This system provides:

* modern density-based terrain
* scalable biome system
* deterministic feature placement
* robust structure generation
* safe multithreaded execution

---

