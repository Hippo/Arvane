# Permission Sources & Priority System

## Purpose

This document defines:

* All **sources of permissions**
* How permissions are **combined and resolved**
* The **priority model**
* Support for **custom permission nodes**

This system is the **authoritative layer** that determines what a player can and cannot do.

---

## Core Concepts

### Permission Types

There are two categories of permissions:

### 1. Core Permissions (Engine-Level)

These are hardcoded capabilities used by engine systems:

```yaml
can_take_damage
can_modify_world
can_interact
can_open_inventory
has_creative_inventory
can_fly
has_collision
is_visible_to_others
```

These must remain **fast, fixed, and queryable without string lookup**.

---

### 2. Custom Permissions (Node-Based)

Custom permissions are **string-based nodes**, similar to Minecraft/Spigot:

Examples:

```text
arvane.command.teleport
arvane.command.gamemode
plugin.minigame.join
plugin.shop.buy
```

These are:

* extensible
* plugin-defined
* optional for core systems

---

## Permission Values

Each permission (core or custom) resolves to:

```yaml
ALLOW
DENY
UNSET
```

### Rules

* `DENY` always overrides `ALLOW`
* `UNSET` means “no opinion”
* Final result must never be ambiguous

---

## Permission Sources

Permissions are not defined in one place.

They come from multiple **sources**:

---

### 1. Game Mode (Base Layer)

* Provides default permissions
* Lowest priority
* Always present

---

### 2. Player-Specific Overrides

Examples:

* admin toggles
* debug tools
* manual overrides

---

### 3. Temporary Modifiers

Examples:

* status effects
* power-ups
* scripted events
* the built-in fallback player layer used when `arv_perms` is not initialized

These are:

* time-based
* dynamically applied/removed

When `arv_perms` is absent, the fallback layer grants normal player basics:
chat send, help, list, emote, private message, spawn, and read-only query
subcommands for time, weather, and status effects. It does not grant operator
commands such as give, kill, summon, teleport, gamemode, save, kick, or
world-changing subcommands.

---

### 4. Region-Based Permissions

Examples:

* spawn protection
* claimed land
* minigame zones

Contextual based on:

* player position
* world/dimension

---

### 5. Plugin/System Overrides

Examples:

* minigame rules
* server logic
* custom mechanics

Highest flexibility layer

---

## Priority Order

Permissions resolve using **strict priority (highest wins)**:

```text
5. Plugin/System Overrides      (highest)
4. Region-Based Permissions
3. Temporary Modifiers
2. Player Overrides
1. Game Mode (base)             (lowest)
```

---

## Resolution Algorithm

For each permission:

1. Start from lowest priority (game mode)
2. Move upward through sources
3. Apply first explicit `DENY` or `ALLOW`
4. Continue upward — **DENY always wins**

### Example

```yaml
game_mode:
  can_modify_world: true

region:
  can_modify_world: false

plugin:
  can_modify_world: unset
```

Result:

```yaml
can_modify_world: false
```

---

### DENY Precedence Example

```yaml
game_mode:
  can_modify_world: true

player_override:
  can_modify_world: true

region:
  can_modify_world: false

plugin:
  can_modify_world: true
```

Result:

```yaml
can_modify_world: false
```

> DENY cannot be overridden by lower or equal layers

---

## Custom Permission Nodes

### Format

Custom permissions use **dot-separated namespaces**:

```text
arvane.*
arvane.command.*
arvane.command.gamemode
plugin.shop.buy
```

---

### Wildcards

Support hierarchical matching:

```text
arvane.command.*
```

matches:

```text
arvane.command.gamemode
arvane.command.tp
```

---

### Assignment Example

```yaml
permissions:
  arvane.command.*: ALLOW
  arvane.command.shutdown: DENY
```

Result:

* `gamemode` → ALLOW
* `shutdown` → DENY

---

### Resolution Rules (Custom Nodes)

1. Exact match takes priority over wildcard
2. More specific wildcard beats broader wildcard

Example:

```yaml
arvane.*: ALLOW
arvane.command.*: DENY
```

Result:

* `arvane.command.tp` → DENY
* `arvane.chat.send` → ALLOW

---

## Integration with Core Permissions

Core and custom permissions are **separate systems**, but:

* They share the same **source + priority model**
* They may be used together in systems

Example:

```cpp
if (!player.permissions.can_modify_world) return;

if (!player.permissions.has("plugin.shop.buy")) return;
```

---

## Data Model

### Core Permissions

```cpp
struct PermissionSet {
    TriState can_take_damage;
    TriState can_modify_world;
    TriState can_interact;
    TriState can_open_inventory;
    TriState has_creative_inventory;
    TriState can_fly;
    TriState has_collision;
    TriState is_visible_to_others;
};
```

---

### Custom Permissions

Recommended structure:

```cpp
HashMap<std::string, TriState>
```

Optimizations (optional later):

* prefix tree (trie)
* cached resolution
* bit-packing for known nodes

---

## Caching Strategy

Permission resolution should **not happen per query**.

Instead:

* Resolve on:

  * mode change
  * region change
  * modifier update
* Cache final results:

```cpp
ResolvedPermissions {
    CorePermissions core;
    CustomPermissions custom;
}
```

---

## Performance Considerations

* Core permissions must be:

  * **branch-light**
  * **cache-friendly**
* Custom permissions:

  * optimized for lookup
  * wildcard resolution cached if needed

---

## Networking

Server may send:

* resolved core permissions (optional optimization)
* relevant custom permissions (if needed by client)

Client:

* treats everything as **authoritative**
* uses for UI/prediction only

---

## Save Format

Store:

* game mode
* persistent player overrides
* persistent custom permissions

Do NOT store:

* resolved permissions
* temporary modifiers

---

## Explicit TODOs (Agent-Facing)

* Implement `TriState` enum (ALLOW / DENY / UNSET)
* Build resolution pipeline with strict priority order
* Implement wildcard matching for custom permissions
* Add caching layer for resolved permissions
* Hook region system into permission resolution
* Hook status/effect system into permission resolution

---

## Future Extensions

This system enables:

* permission groups/roles (e.g. admin, mod, player)
* inheritance trees
* time-based permissions
* per-world permission scopes
* network filtering (e.g. hidden players)

---

## Key Takeaway

Game modes define **defaults**.

Permissions define **reality**.

Everything in the engine must respect the **resolved permission state**, not assumptions.

---
