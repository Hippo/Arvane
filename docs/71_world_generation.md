# 71. World Generation

## Purpose

This document defines the Arvane world generation data model. It describes how dimensions, biome sources, biomes, terrain, surface rules, features, carvers, and structures are represented in packs.

World generation in Arvane is data-driven at the composition layer. Packs define *what* a world contains and how its generator components are connected. The engine provides the actual generation primitives, such as heightmap terrain, climate-based biome selection, surface layering, cave carving, ore placement, vegetation placement, and blueprint-based structures.

The goal is to allow first-party content, plugins, and packs to define rich worlds without requiring every worldgen change to become new engine code.

This document is a stable reference for the world generation format. It is not an implementation plan.

---

## Design Principles

### World generation is composed from named data files

A dimension should not be a single large YAML file containing every biome, tree, ore, cave, and structure definition inline. Instead, a dimension references reusable data resources.

For example, an overworld dimension may reference:

- a biome source
- a terrain definition
- a surface rule set
- one or more carvers
- configured features
- placed features
- structure sets

This keeps world generation modular, pack-friendly, and easier to validate.

### Data selects engine-provided primitives

Worldgen data may select generator types by name, such as:

```yaml
type: arvane:terrain/heightmap
```

or:

```yaml
type: arvane:feature/ore_vein
```

The selected type identifies an engine-provided or plugin-provided primitive. The YAML configures that primitive. The YAML does not define arbitrary executable code.

### YAML should not become a scripting language

Worldgen data should remain declarative. It may contain structured rules, references, ranges, placement filters, and named stages, but it should not contain arbitrary expressions, loops, lambdas, or custom scripts.

This keeps world generation deterministic, easier to debug, easier to validate at pack load time, and safer for modded content.

### Biomes are first-class data resources

A biome is not just a surface block choice. A biome may define:

- terrain modifiers
- surface material rules
- feature lists
- ocean or land classification
- color overrides
- tags used by feature and surface rules
- future spawn, ambience, weather, and environment data

Biome files are independent resources and may be reused by multiple dimensions or biome sources.

### Structures are data-driven through blueprints

Worldgen structures are placed features backed by blueprint data. A placed structure controls *where and how often* a structure appears. The blueprint controls *what blocks the structure contains*.

Arvane blueprints use the `.abp` format.

### Generation must be deterministic

Given the same world seed, pack data, dimension, and chunk position, world generation must produce the same result regardless of chunk load order or thread scheduling.

Worldgen data should therefore use explicit salts, stage ordering, and deterministic placement rules.

---

## Pack Paths

World generation data lives under the `data/` folder of a pack.

The general path format is:

```text
<pack_root>/data/<namespace>/worldgen/<category>/<name>.yaml
```

Examples:

```text
arvane_base/data/arvane/worldgen/dimension/overworld.yaml
arvane_base/data/arvane/worldgen/noise/overworld_climate.yaml
arvane_base/data/arvane/worldgen/terrain/overworld.yaml
arvane_base/data/arvane/worldgen/biome_source/overworld.yaml
arvane_base/data/arvane/worldgen/surface_rule_set/overworld.yaml
arvane_base/data/arvane/worldgen/biome/plains.yaml
arvane_base/data/arvane/worldgen/configured_feature/oak_tree.yaml
arvane_base/data/arvane/worldgen/placed_feature/oak_tree_sparse.yaml
arvane_base/data/arvane/worldgen/carver/caves.yaml
arvane_base/data/arvane/worldgen/structure_set/small_ruins.yaml
arvane_base/data/arvane/blueprint/ruins/small_stone_ruin.abp
```

The namespace folder determines the namespace portion of the resource identifier. For example:

```text
arvane_base/data/arvane/worldgen/biome/plains.yaml
```

corresponds to:

```text
arvane:worldgen/biome/plains
```

Where a shorter identifier is accepted by a specific field, the document states that explicitly. Otherwise, worldgen references should use full resource identifiers.

---

## Resource Identifiers

Worldgen files are identified by resource IDs.

A resource ID has this form:

```text
<namespace>:<path>
```

Examples:

```text
arvane:worldgen/dimension/overworld
arvane:worldgen/biome/plains
arvane:worldgen/placed_feature/oak_tree_sparse
arvane:blueprint/ruins/small_stone_ruin
```

The `id` field inside a worldgen file should match the resource ID derived from its pack path.

Example:

```yaml
id: arvane:worldgen/biome/plains
version: 1
```

A file may reference another file by its resource ID:

```yaml
biome_source: arvane:worldgen/biome_source/overworld
```

---

## Common File Fields

Most worldgen files use these fields:

```yaml
id: arvane:worldgen/category/name
version: 1
```

### `id`

The stable resource ID for the data file.

### `version`

The schema version for the data file. Version `1` is the format described by this document.

---

## World Generation Resource Categories

Arvane world generation uses the following data categories:

| Category | Path | Purpose |
|---|---|---|
| Dimension | `worldgen/dimension/` | Top-level world/dimension generation preset. |
| Noise | `worldgen/noise/` | Named noise fields used by terrain, climate, and rules. |
| Terrain | `worldgen/terrain/` | Base terrain shaping configuration. |
| Biome Source | `worldgen/biome_source/` | Maps world positions to biomes. |
| Biome | `worldgen/biome/` | Defines biome terrain modifiers, surfaces, features, tags, and colors. |
| Surface Rule Set | `worldgen/surface_rule_set/` | Reusable surface block layering rules. |
| Carver | `worldgen/carver/` | Terrain removal stages such as caves. |
| Configured Feature | `worldgen/configured_feature/` | Defines what a feature is. |
| Placed Feature | `worldgen/placed_feature/` | Defines where and how often a feature is placed. |
| Structure Set | `worldgen/structure_set/` | Defines deterministic large-structure placement. |
| Blueprint | `blueprint/` | Stores reusable structure/block templates in `.abp` format. |

---

## Conceptual Generation Pipeline

A dimension generator conceptually evaluates the following stages:

1. Resolve dimension data.
2. Resolve biome source and sample climate data.
3. Determine the biome or biome blend for each generated position.
4. Generate base terrain.
5. Fill fluids up to the dimension sea level where applicable.
6. Apply surface rules.
7. Apply carvers.
8. Apply underground features.
9. Apply surface features.
10. Apply structures.
11. Finalize generated chunk metadata.

The exact internal implementation may vary by generator type. The data model, however, preserves a stable conceptual distinction between terrain, biomes, surfaces, features, carvers, and structures.

---

## Generation Stages

Placed features are assigned to named generation stages. Stages define the relative order in which categories of world features are applied.

The standard stages are:

| Stage | Purpose |
|---|---|
| `underground_ores` | Ores and mineral deposits inside terrain. |
| `underground_decoration` | Underground decoration that is not an ore, such as crystals or cave plants. |
| `surface_decoration` | Grass, flowers, trees, rocks, cacti, and other surface features. |
| `underwater_decoration` | Seagrass, kelp, clay deposits, coral, and other underwater features. |
| `structures` | Blueprint-backed generated structures. |

A dimension and a biome may both contribute placed features to a stage.

Dimension-level feature lists describe global generation. Biome-level feature lists describe biome-specific generation.

---

## Determinism and Seeds

World generation is based on the world seed plus explicit salts. The same inputs must produce the same generated result.

### Dimension seed salt

A dimension file defines `seed_salt`:

```yaml
seed_salt: "overworld"
```

The dimension seed salt separates one dimension's generation stream from another dimension's generation stream.

### Noise seed salt

A noise field may define `seed_salt`:

```yaml
seed_salt: "temperature"
```

The noise seed salt separates different noise fields so that two fields do not accidentally sample the same pattern.

### Placement salt

A placed feature or structure set may define `salt`:

```yaml
salt: 918273
```

Placement salts separate deterministic placement streams for features and structures.

---

# Dimension Files

Dimension files are top-level world generation presets.

Path:

```text
data/<namespace>/worldgen/dimension/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/dimension/overworld
version: 1
seed_salt: "overworld"

vertical:
  min_y: -32
  max_y: 128
  sea_level: 32

generator:
  type: arvane:generator/layered_heightmap

biome_source: arvane:worldgen/biome_source/overworld
terrain: arvane:worldgen/terrain/overworld
surface_rule_set: arvane:worldgen/surface_rule_set/overworld

carvers:
  - arvane:worldgen/carver/caves

features:
  stages:
    underground_ores:
      - arvane:worldgen/placed_feature/ore_coal
      - arvane:worldgen/placed_feature/ore_iron
    surface_decoration:
      - arvane:worldgen/placed_feature/grass_patch
    structures:
      - arvane:worldgen/placed_feature/small_ruin

structure_sets:
  - arvane:worldgen/structure_set/small_ruins
```

## Dimension Fields

### `id`

The dimension resource ID.

### `version`

The dimension schema version.

### `seed_salt`

A string salt mixed with the world seed for this dimension.

### `vertical`

Defines vertical generation limits and sea level.

```yaml
vertical:
  min_y: -32
  max_y: 128
  sea_level: 32
```

#### `vertical.min_y`

The lowest valid generated block Y coordinate.

#### `vertical.max_y`

The highest valid generated block Y coordinate.

#### `vertical.sea_level`

The Y coordinate used as the dimension's default sea level.

### `generator`

Selects the high-level dimension generator primitive.

```yaml
generator:
  type: arvane:generator/layered_heightmap
```

#### `generator.type`

The generator primitive resource ID.

The standard generator type is:

```text
arvane:generator/layered_heightmap
```

A layered heightmap generator uses climate sampling, biome selection, heightmap terrain, surface rules, carvers, placed features, and structures.

### `biome_source`

A reference to a biome source file.

```yaml
biome_source: arvane:worldgen/biome_source/overworld
```

### `terrain`

A reference to a terrain file.

```yaml
terrain: arvane:worldgen/terrain/overworld
```

### `surface_rule_set`

A reference to a surface rule set file.

```yaml
surface_rule_set: arvane:worldgen/surface_rule_set/overworld
```

### `carvers`

A list of carver resource IDs.

```yaml
carvers:
  - arvane:worldgen/carver/caves
```

### `features`

A dimension-level feature list grouped by generation stage.

```yaml
features:
  stages:
    underground_ores:
      - arvane:worldgen/placed_feature/ore_iron
    surface_decoration:
      - arvane:worldgen/placed_feature/grass_patch
```

### `structure_sets`

A list of structure set resource IDs.

```yaml
structure_sets:
  - arvane:worldgen/structure_set/small_ruins
```

Structure sets are intended for deterministic, spaced placement of larger structures. Smaller blueprint-backed features may also be referenced through normal placed features.

---

# Noise Files

Noise files define reusable named noise fields.

Path:

```text
data/<namespace>/worldgen/noise/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/noise/overworld_climate
version: 1

fields:
  temperature:
    type: arvane:noise/simplex_2d
    seed_salt: "temperature"
    frequency: 0.0025
    octaves: 3
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

  humidity:
    type: arvane:noise/simplex_2d
    seed_salt: "humidity"
    frequency: 0.003
    octaves: 3
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

  continentalness:
    type: arvane:noise/simplex_2d
    seed_salt: "continentalness"
    frequency: 0.0015
    octaves: 4
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

  erosion:
    type: arvane:noise/simplex_2d
    seed_salt: "erosion"
    frequency: 0.004
    octaves: 4
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5
```

## Noise File Fields

### `id`

The noise resource ID.

### `version`

The noise file schema version.

### `fields`

A map of named noise fields.

Each field name is local to the noise file. Other files reference the noise file and field name together where needed.

## Noise Field Fields

### `type`

The noise primitive type.

Standard noise types are:

```text
arvane:noise/simplex_2d
arvane:noise/simplex_3d
```

### `seed_salt`

A string salt mixed with the world seed and the owning dimension seed.

### `frequency`

The base frequency of the noise field.

Lower frequencies produce larger-scale variation. Higher frequencies produce smaller-scale variation.

### `octaves`

The number of fractal octaves sampled.

### `amplitude`

The base amplitude of the noise field.

### `lacunarity`

The frequency multiplier between octaves.

### `gain`

The amplitude multiplier between octaves.

---

# Terrain Files

Terrain files define the base shape of terrain before surfaces, features, structures, and carvers are applied.

Path:

```text
data/<namespace>/worldgen/terrain/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/terrain/overworld
version: 1
type: arvane:terrain/heightmap

height:
  base: 8
  scale: 28
  detail_scale: 4

noise:
  primary:
    type: arvane:noise/simplex_2d
    seed_salt: "terrain_primary"
    frequency: 0.012
    octaves: 5
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

  detail:
    type: arvane:noise/simplex_2d
    seed_salt: "terrain_detail"
    frequency: 0.045
    octaves: 2
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

biome_modifiers:
  base_height_offset: true
  height_scale_multiplier: true
  roughness_multiplier: true
```

## Terrain File Fields

### `id`

The terrain resource ID.

### `version`

The terrain schema version.

### `type`

The terrain primitive type.

The standard terrain type is:

```text
arvane:terrain/heightmap
```

### `height`

Height configuration for heightmap terrain.

```yaml
height:
  base: 8
  scale: 28
  detail_scale: 4
```

#### `height.base`

The base height offset applied to generated terrain.

#### `height.scale`

The main height scale applied to primary terrain noise.

#### `height.detail_scale`

The height scale applied to detail terrain noise.

### `noise`

Inline noise field definitions used by this terrain file.

For heightmap terrain, the standard fields are:

| Field | Purpose |
|---|---|
| `primary` | Main large-scale terrain height variation. |
| `detail` | Smaller terrain height variation. |

Each noise field uses the noise field format defined in this document.

### `biome_modifiers`

Controls which biome terrain modifiers affect this terrain definition.

```yaml
biome_modifiers:
  base_height_offset: true
  height_scale_multiplier: true
  roughness_multiplier: true
```

#### `biome_modifiers.base_height_offset`

When true, biome `terrain.base_height_offset` modifies terrain height.

#### `biome_modifiers.height_scale_multiplier`

When true, biome `terrain.height_scale_multiplier` modifies the primary terrain height scale.

#### `biome_modifiers.roughness_multiplier`

When true, biome `terrain.roughness_multiplier` modifies detail terrain strength.

---

# Biome Source Files

Biome source files map world positions to biomes.

Path:

```text
data/<namespace>/worldgen/biome_source/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/biome_source/overworld
version: 1
type: arvane:biome_source/climate_map

noise: arvane:worldgen/noise/overworld_climate

sample_scale:
  xz: 4

biomes:
  - biome: arvane:worldgen/biome/deep_ocean
    climate:
      continentalness: [-1.0, -0.75]

  - biome: arvane:worldgen/biome/ocean
    climate:
      continentalness: [-0.75, -0.45]

  - biome: arvane:worldgen/biome/beach
    climate:
      continentalness: [-0.45, -0.35]

  - biome: arvane:worldgen/biome/desert
    climate:
      continentalness: [-0.35, 1.0]
      temperature: [0.45, 1.0]
      humidity: [-1.0, -0.25]

  - biome: arvane:worldgen/biome/forest
    climate:
      continentalness: [-0.35, 1.0]
      temperature: [-0.25, 0.55]
      humidity: [0.25, 1.0]

  - biome: arvane:worldgen/biome/plains
    climate:
      continentalness: [-0.35, 1.0]
      temperature: [-0.35, 0.55]
      humidity: [-0.35, 0.35]

  - biome: arvane:worldgen/biome/mountains
    climate:
      continentalness: [0.1, 1.0]
      erosion: [0.55, 1.0]
```

## Biome Source Fields

### `id`

The biome source resource ID.

### `version`

The biome source schema version.

### `type`

The biome source primitive type.

The standard biome source type is:

```text
arvane:biome_source/climate_map
```

A climate map biome source selects biomes using named climate fields.

### `noise`

A reference to a noise file containing the climate fields used by this biome source.

```yaml
noise: arvane:worldgen/noise/overworld_climate
```

### `sample_scale`

Controls how frequently biome samples are evaluated.

```yaml
sample_scale:
  xz: 4
```

#### `sample_scale.xz`

The horizontal biome sampling scale in blocks.

A value of `1` samples the biome source every block column. A value of `4` samples the biome source on a coarser grid suitable for smoother and cheaper biome lookup.

### `biomes`

A list of biome entries.

Each entry maps a biome to a set of climate ranges.

```yaml
biomes:
  - biome: arvane:worldgen/biome/plains
    climate:
      temperature: [-0.35, 0.55]
      humidity: [-0.35, 0.35]
```

## Biome Entry Fields

### `biome`

A biome resource ID.

### `climate`

A map of climate field names to inclusive ranges.

```yaml
climate:
  temperature: [-0.35, 0.55]
  humidity: [-0.35, 0.35]
  continentalness: [-0.35, 1.0]
```

The field names must refer to fields in the noise file referenced by `noise`.

Common climate fields are:

| Field | Meaning |
|---|---|
| `temperature` | Hot-to-cold biome distribution. |
| `humidity` | Dry-to-wet biome distribution. |
| `continentalness` | Ocean, coast, and inland distribution. |
| `erosion` | Smooth, rough, or mountainous distribution. |

Climate values are normalized by the biome source. The standard range is `-1.0` to `1.0`.

---

# Biome Files

Biome files define biome-specific terrain modifiers, surface rules, placed features, tags, and colors.

Path:

```text
data/<namespace>/worldgen/biome/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/biome/plains
version: 1

tags:
  - arvane:biome/land
  - arvane:biome/temperate
  - arvane:biome/grassy

terrain:
  base_height_offset: 0
  height_scale_multiplier: 0.85
  roughness_multiplier: 0.65

surface_rule_set: arvane:worldgen/surface_rule_set/grassy_land

features:
  stages:
    surface_decoration:
      - arvane:worldgen/placed_feature/grass_patch
      - arvane:worldgen/placed_feature/oak_tree_sparse

colors:
  grass: "#6BAA45"
  foliage: "#4F8A38"
  water: "#3F76E4"
  fog: "#C8D8FF"
```

## Biome Fields

### `id`

The biome resource ID.

### `version`

The biome schema version.

### `tags`

A list of biome tag IDs.

Tags are used by surface rules, feature filters, structure placement, spawn rules, and other worldgen systems.

Example:

```yaml
tags:
  - arvane:biome/ocean
  - arvane:biome/aquatic
```

### `terrain`

Biome-specific terrain modifiers.

```yaml
terrain:
  base_height_offset: 0
  height_scale_multiplier: 0.85
  roughness_multiplier: 0.65
```

#### `terrain.base_height_offset`

A height offset applied when the active terrain definition enables `biome_modifiers.base_height_offset`.

Positive values raise the biome. Negative values lower the biome.

#### `terrain.height_scale_multiplier`

A multiplier applied to the primary terrain height scale when enabled by the active terrain definition.

Lower values produce flatter terrain. Higher values produce taller terrain.

#### `terrain.roughness_multiplier`

A multiplier applied to detail terrain strength when enabled by the active terrain definition.

Lower values produce smoother terrain. Higher values produce rougher terrain.

### `surface_rule_set`

A reference to a surface rule set used by this biome.

```yaml
surface_rule_set: arvane:worldgen/surface_rule_set/grassy_land
```

If both the dimension and biome define surface rule sets, the generator may evaluate biome-specific rules before dimension fallback rules.

### `features`

Biome-specific placed features grouped by generation stage.

```yaml
features:
  stages:
    surface_decoration:
      - arvane:worldgen/placed_feature/oak_tree_sparse
```

Biome features are only considered when the generated position belongs to the biome or matches the biome selection rules of the active generator.

### `colors`

Optional biome color values used by rendering and ambience systems.

```yaml
colors:
  grass: "#6BAA45"
  foliage: "#4F8A38"
  water: "#3F76E4"
  fog: "#C8D8FF"
```

Supported color keys are:

| Key | Purpose |
|---|---|
| `grass` | Grass tint. |
| `foliage` | Leaf and foliage tint. |
| `water` | Water tint. |
| `fog` | Fog tint. |

---

# Biome Tags

Biome tags are resource IDs used to group biomes.

Common biome tags include:

```text
arvane:biome/land
arvane:biome/ocean
arvane:biome/aquatic
arvane:biome/coast
arvane:biome/beach
arvane:biome/hot
arvane:biome/cold
arvane:biome/dry
arvane:biome/wet
arvane:biome/temperate
arvane:biome/grassy
arvane:biome/sandy
arvane:biome/forested
arvane:biome/mountain
arvane:biome/snowy
```

Tags should describe stable biome properties rather than temporary implementation details.

Good tags:

```text
arvane:biome/ocean
arvane:biome/sandy
arvane:biome/forested
```

Poor tags:

```text
arvane:biome/uses_old_tree_generator
arvane:biome/test_flattened
```

---

# Surface Rule Set Files

Surface rule sets define how terrain columns are converted from base material into surface layers.

Path:

```text
data/<namespace>/worldgen/surface_rule_set/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/surface_rule_set/grassy_land
version: 1

rules:
  - when:
      above_sea_level: true
      slope_less_than: 0.75
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
        depth: 3
      - block: arvane:stone
        depth: remaining

  - layers:
      - block: arvane:stone
        depth: remaining
```

## Surface Rule Set Fields

### `id`

The surface rule set resource ID.

### `version`

The surface rule set schema version.

### `rules`

An ordered list of surface rules.

Rules are evaluated in order. The first matching rule supplies the surface layers.

## Surface Rule Fields

### `when`

Optional predicate block. If omitted, the rule is a fallback and always matches.

```yaml
when:
  above_sea_level: true
  slope_less_than: 0.75
```

Supported predicates are:

| Predicate | Value | Meaning |
|---|---|---|
| `above_sea_level` | boolean | Matches terrain above the dimension sea level. |
| `below_sea_level` | boolean | Matches terrain below the dimension sea level. |
| `underwater` | boolean | Matches terrain covered by water. |
| `biome` | resource ID | Matches a specific biome. |
| `biome_tag` | resource ID | Matches a biome containing the tag. |
| `biome_tag_not` | resource ID | Matches a biome that does not contain the tag. |
| `slope_less_than` | number | Matches terrain with slope below the value. |
| `slope_greater_than` | number | Matches terrain with slope above the value. |
| `height_above` | integer | Matches terrain above the Y value. |
| `height_below` | integer | Matches terrain below the Y value. |

Multiple predicates in the same `when` block are combined with logical AND.

### `layers`

A list of block layers applied from the top of the terrain downward.

```yaml
layers:
  - block: arvane:grass_block
    depth: 1
  - block: arvane:dirt
    depth: 3
  - block: arvane:stone
    depth: remaining
```

## Surface Layer Fields

### `block`

The block state or block resource ID used for the layer.

### `depth`

The layer depth in blocks.

`depth` may be an integer or the literal value:

```yaml
depth: remaining
```

The `remaining` layer fills the rest of the generated terrain column affected by the surface rule.

---

# Carver Files

Carvers remove or replace terrain after base terrain and surface application.

Path:

```text
data/<namespace>/worldgen/carver/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/carver/caves
version: 1
type: arvane:carver/noise_caves

enabled: true

noise:
  type: arvane:noise/simplex_3d
  seed_salt: "caves"
  frequency: 0.035
  octaves: 3
  amplitude: 1.0
  lacunarity: 2.0
  gain: 0.5

threshold: 0.62

y_range:
  min: -32
  max: 64

surface_margin: 8
```

## Carver Fields

### `id`

The carver resource ID.

### `version`

The carver schema version.

### `type`

The carver primitive type.

The standard carver type is:

```text
arvane:carver/noise_caves
```

### `enabled`

Boolean controlling whether this carver is active.

### `noise`

An inline noise field used by the carver.

The noise field uses the noise field format defined in this document.

### `threshold`

The noise threshold used by the carver.

Higher values produce fewer carved spaces. Lower values produce more carved spaces.

### `y_range`

The vertical range where the carver may operate.

```yaml
y_range:
  min: -32
  max: 64
```

#### `y_range.min`

Lowest Y coordinate affected by the carver.

#### `y_range.max`

Highest Y coordinate affected by the carver.

### `surface_margin`

Minimum distance below the surface where the carver may freely open terrain.

A positive value helps prevent caves from opening too frequently directly on the surface.

---

# Configured Feature Files

Configured features define what a feature is.

Path:

```text
data/<namespace>/worldgen/configured_feature/<name>.yaml
```

Configured features do not decide where a feature appears. Placement is controlled by placed feature files.

---

## Simple Tree Feature

Example:

```yaml
id: arvane:worldgen/configured_feature/oak_tree
version: 1
type: arvane:feature/simple_tree

trunk:
  block: arvane:oak_log
  height:
    min: 4
    max: 7

leaves:
  block: arvane:oak_leaves
  radius:
    min: 2
    max: 3
```

### Fields

#### `id`

The configured feature resource ID.

#### `version`

The configured feature schema version.

#### `type`

The feature primitive type.

For simple trees:

```text
arvane:feature/simple_tree
```

#### `trunk`

Trunk configuration.

##### `trunk.block`

The block used for the tree trunk.

##### `trunk.height`

The inclusive random trunk height range.

```yaml
height:
  min: 4
  max: 7
```

##### `trunk.height.min`

Minimum trunk height.

##### `trunk.height.max`

Maximum trunk height.

#### `leaves`

Leaf configuration.

##### `leaves.block`

The block used for leaves.

##### `leaves.radius`

The inclusive random leaf radius range.

```yaml
radius:
  min: 2
  max: 3
```

##### `leaves.radius.min`

Minimum leaf radius.

##### `leaves.radius.max`

Maximum leaf radius.

---

## Patch Feature

A patch feature attempts to place many small blocks near a position.

Example:

```yaml
id: arvane:worldgen/configured_feature/grass_patch
version: 1
type: arvane:feature/patch

block: arvane:short_grass
tries: 24

radius:
  xz: 5
  y: 2

replace:
  only_air: true

place_on:
  - arvane:grass_block
```

### Fields

#### `id`

The configured feature resource ID.

#### `version`

The configured feature schema version.

#### `type`

For patch features:

```text
arvane:feature/patch
```

#### `block`

The block placed by the patch.

#### `tries`

The number of placement attempts per patch origin.

#### `radius`

The area around the origin where placement attempts may occur.

```yaml
radius:
  xz: 5
  y: 2
```

##### `radius.xz`

Horizontal placement radius.

##### `radius.y`

Vertical placement radius.

#### `replace`

Replacement behavior for the patch.

```yaml
replace:
  only_air: true
```

##### `replace.only_air`

When true, the patch may only place into air.

#### `place_on`

A list of blocks that may support this patch.

---

## Disk Feature

A disk feature replaces terrain in a roughly circular area. Disks are useful for sand, clay, gravel, mud, and similar deposits.

Example:

```yaml
id: arvane:worldgen/configured_feature/clay_disk
version: 1
type: arvane:feature/disk

block: arvane:clay

radius:
  min: 2
  max: 5

depth: 2

replace:
  blocks:
    - arvane:sand
    - arvane:gravel
    - arvane:dirt
```

### Fields

#### `id`

The configured feature resource ID.

#### `version`

The configured feature schema version.

#### `type`

For disk features:

```text
arvane:feature/disk
```

#### `block`

The block placed by the disk.

#### `radius`

The inclusive random radius range.

```yaml
radius:
  min: 2
  max: 5
```

##### `radius.min`

Minimum disk radius.

##### `radius.max`

Maximum disk radius.

#### `depth`

The vertical depth of the disk in blocks.

#### `replace`

Replacement behavior.

```yaml
replace:
  blocks:
    - arvane:sand
    - arvane:gravel
```

##### `replace.blocks`

A list of blocks the disk may replace.

---

## Ore Vein Feature

An ore vein feature replaces terrain blocks with ore blocks.

Example:

```yaml
id: arvane:worldgen/configured_feature/ore_iron
version: 1
type: arvane:feature/ore_vein

block: arvane:iron_ore

replace:
  blocks:
    - arvane:stone

vein_size:
  min: 4
  max: 9
```

### Fields

#### `id`

The configured feature resource ID.

#### `version`

The configured feature schema version.

#### `type`

For ore veins:

```text
arvane:feature/ore_vein
```

#### `block`

The ore block placed by the vein.

#### `replace`

Replacement behavior.

##### `replace.blocks`

A list of blocks the ore vein may replace.

#### `vein_size`

The inclusive random size range for the vein.

```yaml
vein_size:
  min: 4
  max: 9
```

##### `vein_size.min`

Minimum vein size.

##### `vein_size.max`

Maximum vein size.

---

## Single Block Feature

A single block feature places one block at a valid location.

Example:

```yaml
id: arvane:worldgen/configured_feature/cactus
version: 1
type: arvane:feature/single_block

block: arvane:cactus

replace:
  only_air: true

place_on:
  - arvane:sand
```

### Fields

#### `id`

The configured feature resource ID.

#### `version`

The configured feature schema version.

#### `type`

For single block features:

```text
arvane:feature/single_block
```

#### `block`

The block placed by the feature.

#### `replace`

Replacement behavior.

##### `replace.only_air`

When true, the feature may only place into air.

#### `place_on`

A list of blocks that may support this feature.

---

## Blueprint Feature

A blueprint feature places a `.abp` blueprint into the world.

Example:

```yaml
id: arvane:worldgen/configured_feature/small_ruin
version: 1
type: arvane:feature/blueprint

blueprint: arvane:blueprint/ruins/small_stone_ruin

rotation:
  random_y: true

integrity: 0.85

replace:
  only_air: true
```

### Fields

#### `id`

The configured feature resource ID.

#### `version`

The configured feature schema version.

#### `type`

For blueprint features:

```text
arvane:feature/blueprint
```

#### `blueprint`

A blueprint resource ID.

Blueprint files use the `.abp` format and live under:

```text
data/<namespace>/blueprint/<path>.abp
```

#### `rotation`

Rotation behavior for the blueprint.

```yaml
rotation:
  random_y: true
```

##### `rotation.random_y`

When true, the placed blueprint may be randomly rotated around the vertical axis.

#### `integrity`

A value from `0.0` to `1.0` controlling how much of the blueprint is preserved during placement.

`1.0` means the full blueprint is placed. Lower values may omit some blocks, creating damaged or ruined variants.

#### `replace`

Optional replacement behavior for non-air blueprint blocks.

Blueprint features use the standard configured-feature replacement forms:

```yaml
replace:
  only_air: true
```

or:

```yaml
replace:
  blocks:
    - arvane:grass_block
    - arvane:dirt
```

When `replace` is omitted, non-air blueprint blocks may overwrite the target block. Blueprint air is skipped by default and does not erase existing blocks.

---

# Placed Feature Files

Placed features define where and how often configured features appear.

Path:

```text
data/<namespace>/worldgen/placed_feature/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/placed_feature/oak_tree_sparse
version: 1

feature: arvane:worldgen/configured_feature/oak_tree
salt: 17291

placement:
  stage: surface_decoration

  count:
    type: chance_per_chunk
    chance: 0.35

  height:
    type: surface

  filters:
    - type: biome_tag
      tag: arvane:biome/grassy

    - type: block_below
      blocks:
        - arvane:grass_block
        - arvane:dirt

    - type: slope_less_than
      value: 0.55
```

## Placed Feature Fields

### `id`

The placed feature resource ID.

### `version`

The placed feature schema version.

### `feature`

A configured feature resource ID.

### `salt`

An integer salt used for deterministic feature placement.

### `placement`

Placement rules for the feature.

---

## Placement Fields

### `stage`

The generation stage where this feature is evaluated.

Standard values are:

```text
underground_ores
underground_decoration
surface_decoration
underwater_decoration
structures
```

### `count`

Controls how many placement origins are attempted.

Supported count types are:

```text
chance_per_chunk
uniform
```

### `height`

Controls how the Y coordinate is selected.

Supported height types are:

```text
surface
uniform
trapezoid
```

### `filters`

A list of placement filters. All filters must pass for a placement attempt to succeed.

---

## Count Types

### `chance_per_chunk`

Attempts placement according to a probability per chunk.

```yaml
count:
  type: chance_per_chunk
  chance: 0.35
```

#### `count.type`

Must be:

```text
chance_per_chunk
```

#### `count.chance`

Probability from `0.0` to `1.0`.

### `uniform`

Attempts a random count in an inclusive range.

```yaml
count:
  type: uniform
  min: 6
  max: 12
```

#### `count.type`

Must be:

```text
uniform
```

#### `count.min`

Minimum number of placement attempts.

#### `count.max`

Maximum number of placement attempts.

---

## Height Types

### `surface`

Places the feature at the terrain surface.

```yaml
height:
  type: surface
```

#### `height.type`

Must be:

```text
surface
```

### `uniform`

Chooses a Y coordinate uniformly from an inclusive range.

```yaml
height:
  type: uniform
  min_y: -32
  max_y: 64
```

#### `height.type`

Must be:

```text
uniform
```

#### `height.min_y`

Minimum Y coordinate.

#### `height.max_y`

Maximum Y coordinate.

### `trapezoid`

Chooses a Y coordinate using a trapezoid-like distribution with a preferred peak.

```yaml
height:
  type: trapezoid
  min_y: -24
  max_y: 72
  peak_y: 24
```

#### `height.type`

Must be:

```text
trapezoid
```

#### `height.min_y`

Minimum Y coordinate.

#### `height.max_y`

Maximum Y coordinate.

#### `height.peak_y`

Preferred Y coordinate where placement is most common.

---

## Placement Filters

### `biome`

Requires a specific biome.

```yaml
- type: biome
  biome: arvane:worldgen/biome/plains
```

#### `type`

Must be:

```text
biome
```

#### `biome`

The required biome resource ID.

### `biome_tag`

Requires the active biome to contain a tag.

```yaml
- type: biome_tag
  tag: arvane:biome/grassy
```

#### `type`

Must be:

```text
biome_tag
```

#### `tag`

The required biome tag.

### `block_below`

Requires the block below the placement position to be one of the listed blocks.

```yaml
- type: block_below
  blocks:
    - arvane:grass_block
    - arvane:dirt
```

#### `type`

Must be:

```text
block_below
```

#### `blocks`

A list of allowed blocks below the placement position.

### `replaceable`

Requires the placement position to be replaceable.
This uses the resolved block registry's `replaceable` property, so air and lightweight decorations such as short grass may be replaced while solid terrain is preserved.

```yaml
- type: replaceable
```

#### `type`

Must be:

```text
replaceable
```

### `slope_less_than`

Requires the terrain slope to be below a value.

```yaml
- type: slope_less_than
  value: 0.55
```

#### `type`

Must be:

```text
slope_less_than
```

#### `value`

The maximum allowed slope value.

### `slope_greater_than`

Requires the terrain slope to be above a value.

```yaml
- type: slope_greater_than
  value: 0.75
```

#### `type`

Must be:

```text
slope_greater_than
```

#### `value`

The minimum required slope value.

### `underwater`

Requires the placement position to be underwater or not underwater.

```yaml
- type: underwater
  value: true
```

#### `type`

Must be:

```text
underwater
```

#### `value`

Boolean underwater requirement.

---

# Structure Set Files

Structure sets define deterministic placement for larger world structures.

Path:

```text
data/<namespace>/worldgen/structure_set/<name>.yaml
```

Example:

```yaml
id: arvane:worldgen/structure_set/small_ruins
version: 1
salt: 918273

structures:
  - feature: arvane:worldgen/configured_feature/small_ruin
    weight: 1

placement:
  type: grid
  average_distance_chunks: 32
  separation_chunks: 8

filters:
  - type: biome_tag
    tag: arvane:biome/land

  - type: slope_less_than
    value: 0.35
```

## Structure Set Fields

### `id`

The structure set resource ID.

### `version`

The structure set schema version.

### `salt`

An integer salt used for deterministic structure placement.

### `structures`

A weighted list of configured blueprint features.

```yaml
structures:
  - feature: arvane:worldgen/configured_feature/small_ruin
    weight: 1
```

Each structure entry must reference a configured feature whose type is:

```text
arvane:feature/blueprint
```

### `placement`

Placement configuration for the structure set.

### `filters`

Placement filters applied before a structure is placed.

Structure set filters use the same filter format as placed features.

---

## Structure Entry Fields

### `feature`

A configured feature resource ID.

### `weight`

The relative selection weight for this structure.

---

## Structure Placement Fields

### `type`

The structure placement type.

The standard structure placement type is:

```text
grid
```

### `average_distance_chunks`

The average distance between structure placement regions, measured in chunks.

### `separation_chunks`

The minimum separation between neighboring structure candidates, measured in chunks.

---

# Blueprints

Blueprints are reusable structure templates stored as `.abp` files.

Path:

```text
data/<namespace>/blueprint/<path>.abp
```

Example:

```text
data/arvane/blueprint/ruins/small_stone_ruin.abp
```

Resource ID:

```text
arvane:blueprint/ruins/small_stone_ruin
```

Blueprints are referenced by configured features of type:

```text
arvane:feature/blueprint
```

Blueprints are suitable for:

- ruins
- trees with authored shapes
- rocks and boulders
- small buildings
- island fragments
- dungeon rooms
- decorative landmarks
- generated minigame islands

Blueprint placement should remain deterministic and should respect the replacement behavior of the configured feature and structure system.

---

# Oceans

Oceans should be represented by explicit ocean biomes rather than only by terrain below sea level.

Common ocean biomes include:

```text
arvane:worldgen/biome/ocean
arvane:worldgen/biome/deep_ocean
arvane:worldgen/biome/warm_ocean
arvane:worldgen/biome/cold_ocean
arvane:worldgen/biome/kelp_forest
```

An ocean biome can define:

- lowered terrain modifiers
- underwater surface materials
- water color
- fog color
- underwater decoration features
- aquatic biome tags
- structure filters for ruins, shipwrecks, reefs, or other underwater structures

Example:

```yaml
id: arvane:worldgen/biome/deep_ocean
version: 1

tags:
  - arvane:biome/ocean
  - arvane:biome/aquatic

terrain:
  base_height_offset: -18
  height_scale_multiplier: 0.35
  roughness_multiplier: 0.4

surface_rule_set: arvane:worldgen/surface_rule_set/ocean_floor

features:
  stages:
    underwater_decoration:
      - arvane:worldgen/placed_feature/clay_disk

colors:
  water: "#254D88"
  fog: "#183C66"
```

Ocean-specific surface rule example:

```yaml
id: arvane:worldgen/surface_rule_set/ocean_floor
version: 1

rules:
  - when:
      underwater: true
    layers:
      - block: arvane:gravel
        depth: 1
      - block: arvane:sand
        depth: 2
      - block: arvane:stone
        depth: remaining

  - layers:
      - block: arvane:stone
        depth: remaining
```

---

# Beaches and Coasts

Beaches are best represented as explicit biomes selected by the biome source near the transition between ocean and inland continentalness values.

Example biome source entry:

```yaml
- biome: arvane:worldgen/biome/beach
  climate:
    continentalness: [-0.45, -0.35]
```

Beach biomes usually use:

```text
arvane:biome/coast
arvane:biome/beach
arvane:biome/sandy
```

Example beach biome:

```yaml
id: arvane:worldgen/biome/beach
version: 1

tags:
  - arvane:biome/land
  - arvane:biome/coast
  - arvane:biome/beach
  - arvane:biome/sandy

terrain:
  base_height_offset: -2
  height_scale_multiplier: 0.35
  roughness_multiplier: 0.25

surface_rule_set: arvane:worldgen/surface_rule_set/sandy_land
```

---

# Mountains

Mountain biomes should be selected by climate or erosion-style fields rather than only by high terrain after generation. This allows mountains to influence terrain shape before the terrain is produced.

Example biome source entry:

```yaml
- biome: arvane:worldgen/biome/mountains
  climate:
    continentalness: [0.1, 1.0]
    erosion: [0.55, 1.0]
```

Example biome:

```yaml
id: arvane:worldgen/biome/mountains
version: 1

tags:
  - arvane:biome/land
  - arvane:biome/mountain

terrain:
  base_height_offset: 10
  height_scale_multiplier: 1.65
  roughness_multiplier: 1.4

surface_rule_set: arvane:worldgen/surface_rule_set/mountain

features:
  stages:
    surface_decoration:
      - arvane:worldgen/placed_feature/stone_boulder
```

Mountain surface rules can expose stone on steep slopes and apply snow at high elevations.

Example:

```yaml
id: arvane:worldgen/surface_rule_set/mountain
version: 1

rules:
  - when:
      slope_greater_than: 0.75
    layers:
      - block: arvane:stone
        depth: remaining

  - when:
      height_above: 84
    layers:
      - block: arvane:snow_block
        depth: 1
      - block: arvane:stone
        depth: remaining

  - layers:
      - block: arvane:grass_block
        depth: 1
      - block: arvane:dirt
        depth: 2
      - block: arvane:stone
        depth: remaining
```

---

# Deserts

Deserts are dry, hot land biomes with sandy surfaces and sparse vegetation.

Example biome source entry:

```yaml
- biome: arvane:worldgen/biome/desert
  climate:
    continentalness: [-0.35, 1.0]
    temperature: [0.45, 1.0]
    humidity: [-1.0, -0.25]
```

Example biome:

```yaml
id: arvane:worldgen/biome/desert
version: 1

tags:
  - arvane:biome/land
  - arvane:biome/hot
  - arvane:biome/dry
  - arvane:biome/sandy

terrain:
  base_height_offset: -1
  height_scale_multiplier: 0.55
  roughness_multiplier: 0.45

surface_rule_set: arvane:worldgen/surface_rule_set/desert

features:
  stages:
    surface_decoration:
      - arvane:worldgen/placed_feature/cactus_sparse
```

Example desert surface rule set:

```yaml
id: arvane:worldgen/surface_rule_set/desert
version: 1

rules:
  - layers:
      - block: arvane:sand
        depth: 5
      - block: arvane:sandstone
        depth: 8
      - block: arvane:stone
        depth: remaining
```

---

# Forests

Forests are usually temperate or wet land biomes with high vegetation density.

Example biome:

```yaml
id: arvane:worldgen/biome/forest
version: 1

tags:
  - arvane:biome/land
  - arvane:biome/temperate
  - arvane:biome/grassy
  - arvane:biome/forested

terrain:
  base_height_offset: 1
  height_scale_multiplier: 0.9
  roughness_multiplier: 0.8

surface_rule_set: arvane:worldgen/surface_rule_set/grassy_land

features:
  stages:
    surface_decoration:
      - arvane:worldgen/placed_feature/oak_tree_dense
      - arvane:worldgen/placed_feature/grass_patch
```

The same configured tree can be reused with multiple placed feature files. A sparse tree placement can be used by plains. A dense tree placement can be used by forests.

---

# Replacement Rules

Configured features use replacement rules to control what they may overwrite.

Supported replacement forms are:

## `only_air`

```yaml
replace:
  only_air: true
```

The feature may only place into air.

## `blocks`

```yaml
replace:
  blocks:
    - arvane:stone
    - arvane:dirt
```

The feature may only replace the listed blocks.

Replacement behavior should be explicit for any feature that modifies existing terrain.

---

# Blocks and Tags in Worldgen

Worldgen references blocks by resource ID.

Examples:

```text
arvane:stone
arvane:dirt
arvane:grass_block
arvane:sand
arvane:water
```

Worldgen systems may also use block tags where the specific rule supports tags. Tags should be used when a rule needs to match a category of blocks rather than one exact block.

Useful worldgen block tag concepts include:

```text
arvane:block_tag/natural_stone
arvane:block_tag/soil
arvane:block_tag/sand
arvane:block_tag/gravel
arvane:block_tag/replaceable
arvane:block_tag/replaceable_plant
arvane:block_tag/ore_replaceable
arvane:block_tag/tree_trunk
arvane:block_tag/leaves
```

This document defines block references for the listed surface and feature formats. Additional tag-aware rule forms should be documented before they are used in configs.

---

# Biome Blending

The data model should allow the engine to evolve from single-biome sampling to blended biome sampling.

A simple generator may use one dominant biome per column.

A more advanced generator may compute nearby biome weights and use them to blend terrain parameters such as:

- base height offset
- height scale multiplier
- roughness multiplier
- colors

Surface blocks and features should normally use the dominant biome unless the active generator defines a more specific blending rule.

Worldgen data should avoid assuming that biome borders are always hard chunk or block boundaries.

The standard `arvane:generator/layered_heightmap` generator samples the biome source on its configured `sample_scale.xz`, selects a dominant biome for surfaces and features, and blends nearby biome terrain modifiers for base height, height scale, and roughness. This keeps visible biome identity discrete while smoothing height transitions across biome borders.

---

# Chunk Boundary Behavior

Features and structures may cross chunk boundaries. A tree, ruin, boulder, or blueprint can originate in one chunk and affect neighboring chunks.

Worldgen data should be authored with this in mind:

- features should have deterministic salts
- structures should use deterministic placement
- large structures should use structure sets
- configured features should have clear replacement behavior
- blueprint structures should avoid relying on chunk generation order

The same world seed and pack data should produce the same world even when chunks are generated in a different order.

## Structure Chunk-Border Behavior

Structures are deterministic global candidates. Chunks own their own writes.

When a chunk is generated, structure sets are evaluated by computing a bounded set of nearby grid placement cells that could produce candidates overlapping the chunk. Each candidate derives its placement chunk, local origin, selected structure entry, rotation, and surface anchor deterministically from the world seed, structure set salt, placement cell, and resource identity. Candidate generation must not depend on loaded chunks, generation order, thread scheduling, unordered-map iteration, or previous random calls from other chunks.

Each structure candidate has a transformed world-space block AABB. A generated chunk intersects that AABB with its own chunk-column bounds and applies only the intersecting block region to its local generation buffer. A structure must not directly mutate neighboring chunks during generation, and generation must not acquire neighbor-chunk locks or require the origin chunk to have generated first.

Blueprint-backed structures are applied from the intersection volume, not by scanning the whole blueprint when only a slice intersects the current chunk. The generator converts each intersecting world block back into blueprint-local coordinates, skips blueprint air by default, evaluates configured replacement rules against the current chunk buffer only, and writes only local blocks.

Placement filters for structure candidates use deterministic worldgen-derived terrain data such as surface height, biome, slope, underwater state, and terrain/surface block at the candidate origin. They do not read loaded neighbor chunks. Surface anchoring is likewise derived from the worldgen terrain profile.

Any per-block randomness for blueprint structures, including `integrity` damage/omission, must be position-hashed. The hash should include at least the world seed, structure salt, structure origin, blueprint-local block position, and resource identity. A mutable RNG stream must not decide per-block integrity because the same blueprint-local block must make the same decision regardless of which intersecting chunk is generated first.

Large structures should be represented as deterministic candidates or deterministic candidate pieces with conservative bounds. Candidate search must be bounded by the maximum horizontal extent of the resolved structure set rather than scanning arbitrary large regions.

---

# Validation Expectations

Worldgen data should be validated when packs are loaded.

Invalid worldgen data includes:

- missing required fields
- unknown resource references
- unknown generator, noise, terrain, carver, feature, or biome source types
- invalid numeric ranges
- invalid color strings
- invalid generation stage names
- unknown surface rule predicates
- unknown placement filter types
- configured features referenced by incompatible systems
- structure sets referencing non-blueprint configured features

Validation should report the resource ID and field path that caused the error.

---

# Example Minimal Overworld Set

This section shows a coherent minimal set of worldgen files using only fields documented above.

## Dimension

```yaml
id: arvane:worldgen/dimension/overworld
version: 1
seed_salt: "overworld"

vertical:
  min_y: -32
  max_y: 128
  sea_level: 32

generator:
  type: arvane:generator/layered_heightmap

biome_source: arvane:worldgen/biome_source/overworld
terrain: arvane:worldgen/terrain/overworld
surface_rule_set: arvane:worldgen/surface_rule_set/overworld

carvers:
  - arvane:worldgen/carver/caves

features:
  stages:
    underground_ores:
      - arvane:worldgen/placed_feature/ore_iron
    surface_decoration:
      - arvane:worldgen/placed_feature/grass_patch

structure_sets:
  - arvane:worldgen/structure_set/small_ruins
```

## Climate Noise

```yaml
id: arvane:worldgen/noise/overworld_climate
version: 1

fields:
  temperature:
    type: arvane:noise/simplex_2d
    seed_salt: "temperature"
    frequency: 0.0025
    octaves: 3
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

  humidity:
    type: arvane:noise/simplex_2d
    seed_salt: "humidity"
    frequency: 0.003
    octaves: 3
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

  continentalness:
    type: arvane:noise/simplex_2d
    seed_salt: "continentalness"
    frequency: 0.0015
    octaves: 4
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

  erosion:
    type: arvane:noise/simplex_2d
    seed_salt: "erosion"
    frequency: 0.004
    octaves: 4
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5
```

## Terrain

```yaml
id: arvane:worldgen/terrain/overworld
version: 1
type: arvane:terrain/heightmap

height:
  base: 8
  scale: 28
  detail_scale: 4

noise:
  primary:
    type: arvane:noise/simplex_2d
    seed_salt: "terrain_primary"
    frequency: 0.012
    octaves: 5
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

  detail:
    type: arvane:noise/simplex_2d
    seed_salt: "terrain_detail"
    frequency: 0.045
    octaves: 2
    amplitude: 1.0
    lacunarity: 2.0
    gain: 0.5

biome_modifiers:
  base_height_offset: true
  height_scale_multiplier: true
  roughness_multiplier: true
```

## Biome Source

```yaml
id: arvane:worldgen/biome_source/overworld
version: 1
type: arvane:biome_source/climate_map

noise: arvane:worldgen/noise/overworld_climate

sample_scale:
  xz: 4

biomes:
  - biome: arvane:worldgen/biome/deep_ocean
    climate:
      continentalness: [-1.0, -0.75]

  - biome: arvane:worldgen/biome/ocean
    climate:
      continentalness: [-0.75, -0.45]

  - biome: arvane:worldgen/biome/beach
    climate:
      continentalness: [-0.45, -0.35]

  - biome: arvane:worldgen/biome/desert
    climate:
      continentalness: [-0.35, 1.0]
      temperature: [0.45, 1.0]
      humidity: [-1.0, -0.25]

  - biome: arvane:worldgen/biome/forest
    climate:
      continentalness: [-0.35, 1.0]
      temperature: [-0.25, 0.55]
      humidity: [0.25, 1.0]

  - biome: arvane:worldgen/biome/plains
    climate:
      continentalness: [-0.35, 1.0]
      temperature: [-0.35, 0.55]
      humidity: [-0.35, 0.35]
```

## Plains Biome

```yaml
id: arvane:worldgen/biome/plains
version: 1

tags:
  - arvane:biome/land
  - arvane:biome/temperate
  - arvane:biome/grassy

terrain:
  base_height_offset: 0
  height_scale_multiplier: 0.85
  roughness_multiplier: 0.65

surface_rule_set: arvane:worldgen/surface_rule_set/grassy_land

features:
  stages:
    surface_decoration:
      - arvane:worldgen/placed_feature/oak_tree_sparse
      - arvane:worldgen/placed_feature/grass_patch

colors:
  grass: "#6BAA45"
  foliage: "#4F8A38"
  water: "#3F76E4"
  fog: "#C8D8FF"
```

## Grassy Surface Rules

```yaml
id: arvane:worldgen/surface_rule_set/grassy_land
version: 1

rules:
  - when:
      above_sea_level: true
      slope_less_than: 0.75
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
        depth: 3
      - block: arvane:stone
        depth: remaining

  - layers:
      - block: arvane:stone
        depth: remaining
```

## Oak Tree Configured Feature

```yaml
id: arvane:worldgen/configured_feature/oak_tree
version: 1
type: arvane:feature/simple_tree

trunk:
  block: arvane:oak_log
  height:
    min: 4
    max: 7

leaves:
  block: arvane:oak_leaves
  radius:
    min: 2
    max: 3
```

## Sparse Oak Tree Placement

```yaml
id: arvane:worldgen/placed_feature/oak_tree_sparse
version: 1

feature: arvane:worldgen/configured_feature/oak_tree
salt: 17291

placement:
  stage: surface_decoration

  count:
    type: chance_per_chunk
    chance: 0.35

  height:
    type: surface

  filters:
    - type: biome_tag
      tag: arvane:biome/grassy

    - type: block_below
      blocks:
        - arvane:grass_block
        - arvane:dirt

    - type: slope_less_than
      value: 0.55
```

## Iron Ore Configured Feature

```yaml
id: arvane:worldgen/configured_feature/ore_iron
version: 1
type: arvane:feature/ore_vein

block: arvane:iron_ore

replace:
  blocks:
    - arvane:stone

vein_size:
  min: 4
  max: 9
```

## Iron Ore Placement

```yaml
id: arvane:worldgen/placed_feature/ore_iron
version: 1

feature: arvane:worldgen/configured_feature/ore_iron
salt: 81922

placement:
  stage: underground_ores

  count:
    type: uniform
    min: 6
    max: 12

  height:
    type: trapezoid
    min_y: -24
    max_y: 72
    peak_y: 24

  filters:
    - type: biome_tag
      tag: arvane:biome/land
```

## Cave Carver

```yaml
id: arvane:worldgen/carver/caves
version: 1
type: arvane:carver/noise_caves

enabled: true

noise:
  type: arvane:noise/simplex_3d
  seed_salt: "caves"
  frequency: 0.035
  octaves: 3
  amplitude: 1.0
  lacunarity: 2.0
  gain: 0.5

threshold: 0.62

y_range:
  min: -32
  max: 64

surface_margin: 8
```

## Small Ruin Blueprint Feature

```yaml
id: arvane:worldgen/configured_feature/small_ruin
version: 1
type: arvane:feature/blueprint

blueprint: arvane:blueprint/ruins/small_stone_ruin

rotation:
  random_y: true

integrity: 0.85
```

## Small Ruin Structure Set

```yaml
id: arvane:worldgen/structure_set/small_ruins
version: 1
salt: 918273

structures:
  - feature: arvane:worldgen/configured_feature/small_ruin
    weight: 1

placement:
  type: grid
  average_distance_chunks: 32
  separation_chunks: 8

filters:
  - type: biome_tag
    tag: arvane:biome/land

  - type: slope_less_than
    value: 0.35
```

---

# Stability Notes

Worldgen data should favor stable, explicit composition over implicit behavior.

Preferred:

```yaml
surface_rule_set: arvane:worldgen/surface_rule_set/grassy_land
```

Avoid relying on hidden defaults for core world behavior. Defaults may be useful for editor convenience, but pack-authored worldgen files should be explicit about their terrain, biome source, surface rules, features, carvers, and structures.

Worldgen data should remain readable by humans. If a worldgen file becomes too large, split it into smaller referenced resources instead of expanding one file indefinitely.

The long-term direction is a modular data-driven world generation stack where packs define content and composition while the engine provides deterministic, validated, high-performance primitives.
