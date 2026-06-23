# Save Format

Arvane save data is designed to be stable across engine versions, registry changes, and long-term iteration.

The save system must not depend on runtime-only identifiers or memory layout.

All persisted data must be:

- versioned
- explicit
- reconstructable
- migratable

---

## Core Principles

- Runtime IDs are not stable and must not be saved directly
- Registries are reconstructed at startup, not serialized
- Save data references stable string IDs indirectly via local tables
- Worlds are independent save units
- Server save owns global state (players, permissions, etc.)
- Derived data is not saved (meshes, caches, etc.)
- Save format must support migration

### Codec Ownership

Engine persistence and config writes should go through `arv::Codec<T>`
backends, but codec migration must not rewrite existing file formats unless a
format migration is explicitly planned.

Current format-preserving backends keep these on-disk shapes:

- YAML runtime config, input binding, and permission files stay YAML
- server lists, save roots, world metadata, player profiles, and entity-region
  metadata stay JSON with their existing pretty-print style
- world region files keep the `.arvr` binary chunk-region layout
- plugin config and persistent records keep their existing `DataDocument`
  YAML/binary storage

---

## Save Root Structure

A save root represents a **server save**, not a single world.

This applies to:

- dedicated servers
- singleplayer (integrated server)

In development, singleplayer server saves live under `arvane_home/saves/`.
The dedicated server uses `arvane_server/` as its fixed server root; its active
world is selected by the top-level `world` property in
`arvane_server/server.yaml`.

Example:

```text
arvane_home/
  saves/
    server-save-1/
      save.json
      players/
        <player_uuid>.dat
      packs/
        arv_perms/
          config/
            config.yaml
            groups.yaml
          data/
            players/
              <PlayerProfileID>/
                permissions.json
      worlds/
        world-a/
          world.json
          regions/
            r.<x>.<z>.arvr
        world-b/
          world.json
          regions/
            r.<x>.<z>.arvr
      server/
        permissions.json
        whitelist.json
        ops.json
arvane_server/
  save.json
  players/
    <player_uuid>.dat
  plugin_data/
    <pack_id>/
      server/
        <safe_key_path>.bin
  player_assets/
    skins/
      <skin_hash>.png
  worlds/
    default/
      world.json
      blueprints/
        <name>.abp
      regions/
        r.<x>.<z>.arvr
  plugin_data/
    <pack_id>/
      world/
      players/
      entities/
      blocks/
      chunks/
```

Notes:

* `arvane_home/saves/<server-save-name>/` is the top-level save boundary
  for singleplayer server saves
* Integrated singleplayer plugin config/data is saved per server save under
  `arvane_home/saves/<server-save-name>/packs/<pack_id>/`
* `arvane_server/` is the top-level save boundary for `arv_dedicated`
* Each world is fully independent
* A “dimension” is simply another world
* Worlds do not nest inside each other
* World-local blueprints are saved as `.abp` documents under
  `worlds/<world>/blueprints/`
* `saves/` contains singleplayer server save directories, not worlds directly
* Players exist at the server level (not per-world)
* Uploaded player skin assets are stored at the server-save level under
  `player_assets/skins/` by server-computed content hash; writes use temporary
  files and atomic rename, and servers enforce the configured total skin storage
  quota before adding new blobs
* Plugin config and plugin persistent data are sidecar records under controlled
  save-root directories. Resource keys are sanitized and mapped by the storage
  service; plugins do not read or write arbitrary filesystem paths.
* Plugin persistent records are binary `DataDocument` files with schema
  versions. Player-scoped records are keyed by stable player identity, not
  transient connection/player handles. Records are preserved when their owning
  plugin is absent.

---

## What Is Persisted

### Server Level

* save format version
* authoritative display name
* global settings
* player profiles
* server configuration
* plugin/mod list (optional but recommended)
* server-scoped plugin persistent data

### World Level

Each world stores:

* world metadata
* seed and generator config
* world spawn location (`x`, `y`, `z`, `yaw_radians`, `pitch_radians`, optional `world`; omitted when implicit)
* world time / tick counter
* simulation settings
* region files
* world-local blueprint documents
* world-scoped plugin config and persistent data sidecars

### Region / Chunk Level

* block state data
* lighting data
* block entities
* scheduled ticks
* biome data (if used)

### Entity Level

* persistent entities
* entity data and components
* non-transient natural entities only

### Inventory Level

* item stacks
* item metadata

### Player Runtime State

Player save JSON lives under the save root's `players/` directory and stores
the last known position, velocity, rotation, game mode, permissions, inventory,
skin selection, and optional respawn location. The `respawn` field uses the same
compact location shape as world spawn (`x`, `y`, `z`, `yaw_radians`,
`pitch_radians`). When `respawn` is absent, player death respawns at the current
world spawn.

### Player Skin State

Player save/profile JSON may contain:

```json
{
  "selected_skin_hash": "0x...",
  "selected_skin_slim_model": false
}
```

An empty string means no custom skin. The hash points to
`player_assets/skins/<selected_skin_hash>.png`. The hash is lowercase
`0x`-prefixed hex and is computed from the original accepted PNG bytes.
`selected_skin_slim_model` selects the slim player geometry for that profile and
does not affect the stored PNG bytes or hash. The filename is always derived
from this hash; profile ids, display names, client filenames, and
packet-provided paths are never used as skin asset paths.
Future save maintenance should garbage-collect unreferenced skin files according
to server policy.

---

## What Is NOT Persisted

* runtime IDs
* ECS entity handles
* renderer state
* GPU resources
* chunk meshes
* temporary simulation queues
* derived caches
* client-downloaded remote skin caches

All derived data must be rebuilt on load.

---

## Versioning

Every save must declare a version:

```json
{
  "save_format_version": 1,
  "game_version": "0.1.0",
  "name": "My World"
}
```

Subsystems may version independently:

```json
{
  "lighting_version": 1,
  "entity_version": 1,
  "inventory_version": 2
}
```

---

## Registry Policy

Registries are **not serialized**.

They are rebuilt at startup from:

* installed pack data
* built-in `arvane_base` content
* server-required content packs
* explicitly enabled native/plugin systems, when supported

Save data contains only the information required to resolve saved content.

World metadata does not record a required pack set, pack content hash, or
registry hash as a startup requirement. Older save files may still contain
`packs`, `content_lock`, or `registry_refs`; loaders tolerate those fields for
compatibility, but new saves do not emit them. Region, entity, and item data are
reconstructed from stable string IDs such as `arvane:stone` and file-local ID
tables.

---

## Local Save Tables

Local tables are the core compatibility mechanism.

They map compact indices → stable string IDs.

Example:

```text
block_table = [
  "arvane:air",
  "arvane:grass",
  "arvane:stone",
  "arvane:dirt"
]

block_data = [
  0, 1, 1, 2, 3, 2, 1, 0
]
```

Rules:

* indices are local to the save container
* index `0` is reserved for air (for blocks)
* entries are stable string IDs
* runtime IDs are resolved at load time

Load path:

```text
index -> stable string ID -> runtime ID
```

---

## Region-Wide Tables

Tables should exist at the **region level**, not per chunk.

Example:

```text
region:
  block_state_table:
    0 = arvane:air
    1 = arvane:stone
    2 = arvane:grass
    3 = arvane:stairs[facing=north]
    4 = arvane:stairs[facing=south]
```

Benefits:

* avoids duplication
* improves compression
* enables stable remapping

---

## Block State Serialization

Chunks store **block states**, not just block types.

States are represented as stable IDs:

```text
arvane:stairs[facing=north]
```

Chunk sections store indices into the region’s `block_state_table`.

---

## Chunk Serialization

Chunks are divided into vertical sections.
Region files store each section's absolute chunk-section Y coordinate and must
not impose a fixed 16-section world-height window.

Each section contains:

* block states (packed indices)
* lighting
* optional biome data

All-air sections may be omitted.

---

## Block Data Encoding

Each section contains:

```text
4096 entries (16x16x16)
```

Each entry is an index into the block state table.

Bit width depends on table size:

```text
<=16 entries   → 4 bits
<=256 entries  → 8 bits
<=4096 entries → 12 bits
```

Index order:

```text
index = x + z * 16 + y * 256
```

This must be consistent globally.

---

## Lighting Serialization

Lighting is stored separately from block data.

Format:

```text
4 bits red
4 bits green
4 bits blue
4 bits sky
```

Total: 16 bits per block.

Lighting may be invalidated during migration if needed.

---

## Block Entities

Stored separately from block arrays.

Example:

```json
{
  "pos": [4, 12, 9],
  "type": "arvane:chest",
  "version": 1,
  "data": {}
}
```

Rules:

* must match block at position
* invalid entries are skipped with warning

---

## Inventory Serialization

Uses local tables (same concept as chunks).

Example:

```text
item_table = [
  "arvane:air",
  "arvane:stone",
  "arvane:iron_pickaxe"
]
```

```json
{
  "slots": [
    { "item": 1, "count": 64 },
    { "item": 2, "count": 1, "data": { "pack_namespace": { "key": "value" } } }
  ]
}
```

`inventory_version` 2 adds optional per-stack `data`. The root object is keyed
by pack/plugin namespace. Missing `data` is equivalent to an empty object.

---

## Entity Serialization

Entities are saved independently of ECS.

Example:

```json
{
  "uuid": "...",
  "type": "arvane:dropped_item",
  "version": 1,
  "world_id": 1,
  "position": [x, y, z],
  "rotation": [0, 0, 0, 1],
  "velocity": [0, 0, 0],
  "components": {
    "dropped_item": {
      "stack": {
        "item": "arvane:stone",
        "count": 4,
        "data": { "pack_namespace": { "key": "value" } }
      },
      "pickup_delay_seconds": 0.5
    }
  }
}
```

Rules:

* ECS IDs are never saved
* UUID is the persistent identity
* component payloads use stable registry names, not runtime item/entity IDs
* transient natural-spawned entities are skipped even if their entity type is persistent by default

---

## Scheduled Ticks

Must be persisted when gameplay-relevant.

Example:

```json
{
  "type": "arvane:fluid_tick",
  "pos": [x, y, z],
  "due_tick": 123456
}
```

World tick counter must also be saved.

---

## Missing Registry Entries

If a saved ID is missing:

Default behavior: **safe mode**

* replace with:

  * `arvane:missing_block`
  * `arvane:missing_item`
* log warning

Never silently map to another valid block.

---

## Migration Handling

Migrations transform old saves to new formats.

They may:

* rename IDs
* rewrite states
* update data formats
* invalidate lighting

Example:

```text
v2 → v3:
  rename arvane:grass_old → arvane:grass
```

Migration pipeline:

```text
load tables
apply migrations to table entries
resolve to runtime IDs
load data
```

---

## Compression

Region payloads may be compressed.

Recommended:

```text
zstd
```

Compression applies to:

* block data
* lighting
* entity blobs

---

## Region Files

Regions group chunks spatially.

Example:

```text
32x32 chunks per region
```

Region contains:

* header
* tables
* chunk index
* chunk data
* optional entity data

---

## Region File Format

Chunk data is stored inside region files using a structured binary format.

Region files:

- group chunks spatially
- provide fast lookup via fixed indexing
- store region-local tables for registry resolution
- allow independent chunk reads and writes
- support compression and future extensions

The exact byte-level structure is defined in:

- [Region Format](210_region_file_binary_layout.md)

This document defines:

- header layout and magic value
- chunk location and metadata tables
- region-wide table encoding
- chunk block storage format
- compression rules
- block entity and scheduled tick encoding
- append/write strategy

The save format specification defines **what must be stored**.

The region file layout defines **how it is stored on disk**.

### Implementation Rule

All region file reads and writes must conform exactly to the binary layout specification.

No assumptions about layout, padding, or ordering should be made outside of that document.

### Evolution

The region file format is versioned independently via:

- `format_version` in the file header

Changes to the binary layout must:

- increment the format version
- provide migration or compatibility handling
- not silently reinterpret old data

### Safety

Region files must be treated as:

- partially corruptible (per-chunk failures must not crash the engine)
- independently readable (chunks can be skipped if invalid)
- append-friendly (writes should avoid full rewrites where possible)

---

## Atomic Saves

Writes must be crash-safe:

```text
write temp file
flush
rename over original
```

---

## Dirty Tracking

Separate flags:

* block data dirty
* lighting dirty
* entity dirty
* save dirty

Saving is independent of meshing.

---

## Threading Rules

Saving runs on background threads.

Workers operate on **snapshots only**.

They must not:

* access live world state
* mutate ECS directly

---

## Plugin Data

Plugins may store namespaced data:

```json
{
  "plugin_data": {
    "mod:id": {
      "version": 1,
      "data": {}
    }
  }
}
```

Unknown plugin data should be preserved when possible.

---

## Load Pipeline

```text
1. Read save metadata
2. Load registries
3. Open world
4. Open region
5. Read tables
6. Apply migrations
7. Resolve IDs
8. Load chunk data
9. Load entities
10. Rebuild derived data
```

---

## Save Pipeline

```text
1. Collect dirty data
2. Snapshot world state
3. Update tables
4. Encode chunk data
5. Compress
6. Write atomically
```

---

## Summary

* Server save owns global state
* Worlds are independent
* Registries are not saved
* Local tables provide stability
* Runtime IDs are never trusted
* Save format is versioned and migratable

```
