# Regions & Zones

## Purpose

Regions and zones define **spatial gameplay rules**.

They are used for:

* spawn protection
* lobby areas
* minigame arenas
* protected builds
* PvP areas
* scripted gameplay spaces
* world-specific behavior overrides

Regions integrate directly with the permission system as a **contextual permission source**.

---

## Core Concepts

### Region

A **region** is a named spatial volume with rules attached.

Examples:

```text
spawn
market
arena_01
red_team_base
```

Regions may define:

* permission overrides
* gameplay flags
* priority
* enter/exit behavior

---

### Zone

A **zone** is a broader grouping of regions or rules.

Examples:

```text
lobby_zone
survival_world_zone
minigame_zone
```

Zones are useful for organizing related regions and applying shared defaults.

A region may belong to one or more zones.

---

## Spatial Shape

Initial supported region shape:

```yaml
shape: aabb
min: [x, y, z]
max: [x, y, z]
```

Example:

```yaml
regions:
  spawn:
    shape: aabb
    world: overworld
    min: [-64, 0, -64]
    max: [64, 255, 64]
```

AABB regions are sufficient for the first implementation and easy to query efficiently.

Future shapes may include:

* cylinders
* spheres
* polygonal 2D footprints with height ranges

---

## Region Rules

Regions may define core permission overrides:

```yaml
core:
  can_modify_world: DENY
  can_interact: DENY
```

And custom permission nodes:

```yaml
permissions:
  arvane.command.spawn: ALLOW
  plugin.shop.buy: ALLOW
```

Rules use the same `ALLOW / DENY / UNSET` model as the main permission system.

---

## Priority

Regions have explicit priority:

```yaml
priority: 100
```

Higher priority regions override lower priority regions.

Example:

```yaml
regions:
  spawn:
    priority: 10
    core:
      can_modify_world: DENY

  spawn_build_area:
    priority: 100
    core:
      can_modify_world: ALLOW
```

If a player is inside both regions, `spawn_build_area` takes priority.

`DENY` still wins when priorities are equal.

---

## Zones

Zones provide shared rule bundles.

```yaml
zones:
  lobby:
    core:
      can_modify_world: DENY
      can_interact: DENY
    permissions:
      plugin.queue.join: ALLOW
```

Regions may opt into zones:

```yaml
regions:
  hub_spawn:
    zones: [lobby]
    shape: aabb
    world: lobby
    min: [-128, 0, -128]
    max: [128, 255, 128]
```

### Zone Rules

* Zones do not have shapes by themselves
* Zones apply through regions
* Regions may inherit from multiple zones
* Region-local rules override zone rules
* Zone priority applies before region priority unless overridden

---

## Resolution Order

Region and zone rules belong to the **Region-Based Permissions** layer.

Within this layer:

```text
1. Zone rules
2. Region rules
3. Higher priority region rules
```

The full permission pipeline remains:

```text
5. Plugin/System Overrides
4. Region-Based Permissions
3. Temporary Modifiers
2. Player Overrides / Groups
1. Game Mode
```

---

## Region Entry & Exit

The server tracks which regions a player is currently inside.

When region membership changes:

* recompute resolved permissions
* fire region enter/exit events
* update gameplay state as needed

Examples:

```text
player_enter_region
player_exit_region
```

Events are useful for:

* showing titles/messages
* starting minigames
* applying temporary effects
* enabling/disabling UI prompts

---

## World Scope

Regions are scoped to a world/dimension.

```yaml
world: overworld
```

A player can only be inside regions belonging to their current world.

Cross-world checks must never accidentally apply another world’s region rules.

---

## Interaction with Game Modes

Regions do not change a player’s game mode by default.

Instead, they override permissions.

Example:

A Creative player entering spawn may still have:

```yaml
can_modify_world: DENY
```

while remaining in Creative mode.

This keeps game mode identity separate from contextual permissions.

---

## Interaction with Lobby Mode

Lobby mode is useful for players who should globally behave as lobby players.

Regions are useful for locations that should behave like lobbies.

Both systems can coexist.

Example:

* A lobby server may set all players to `Lobby`
* A survival server may use a `spawn` region to restrict edits near spawn

---

## Region Querying

The world/server should provide a region query service:

```cpp
struct RegionQuery {
    WorldId world;
    Vec3 position;
};

std::vector<RegionId> find_regions_at(RegionQuery query);
```

Systems should not manually scan region lists directly.

---

## Performance Strategy

Initial implementation may use simple linear scans per world if region counts are low.

For larger servers, use spatial acceleration:

* chunk-to-region maps
* uniform grids
* interval trees
* bounding volume hierarchies

Recommended first scalable approach:

* map region AABBs to chunk coordinates
* store region IDs per chunk
* when a player moves chunks, update candidate regions

---

## Region Storage

Regions may be loaded from server/world config.

Example:

```yaml
regions:
  spawn:
    world: overworld
    priority: 100
    shape: aabb
    min: [-64, 0, -64]
    max: [64, 255, 64]
    core:
      can_modify_world: DENY
      can_interact: DENY
    permissions:
      plugin.shop.buy: ALLOW
```

Persistent world-owned regions should be saved with the world/server save data.

Plugin-owned temporary regions may be runtime-only unless explicitly persisted.

---

## Data Model

```cpp
struct Region {
    RegionId id;
    WorldId world;
    int priority;

    RegionShape shape;

    std::vector<ZoneId> zones;

    CorePermissionSet core;
    CustomPermissionMap permissions;
};
```

```cpp
struct Zone {
    ZoneId id;
    int priority;

    CorePermissionSet core;
    CustomPermissionMap permissions;
};
```

```cpp
struct PlayerRegionState {
    std::vector<RegionId> active_regions;
};
```

---

## Validation Rules

* Region IDs must be unique within a world
* Zone IDs must be globally unique or namespaced
* Region bounds must be valid
* Region world must exist
* Missing zone references are errors
* Circular zone inheritance is invalid if zone inheritance is added later

---

## Networking

Region logic is server authoritative.

Clients may receive:

* current region names for UI
* enter/exit notifications
* resolved permissions

Clients must not decide region membership authoritatively.

---

## Explicit TODOs

* Implement AABB region shape
* Implement per-world region registry
* Implement region query service
* Track player active region membership
* Fire enter/exit events
* Integrate region rules into permission resolution
* Add config loading for regions/zones
* Add tests for overlapping regions and priority behavior

---

## Future Extensions

Regions and zones can later support:

* claim systems
* PvP flags
* biome-like gameplay areas
* scripted minigame arenas
* admin selection tools
* temporary event regions
* visual region debugging
* polygonal regions

---

## Key Takeaway

Game modes define a player’s default behavior.

Groups define persistent authority.

Regions and zones define spatial context.

The final permission result comes from combining all of them through the shared permission pipeline.
