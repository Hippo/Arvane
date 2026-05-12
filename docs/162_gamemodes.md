# Game Modes & Permission System

## Purpose

This document defines:

* The **player permission system** (core authority model)
* How **game modes are built on top of permissions**
* Default **built-in game modes**

Game modes are **presets**, not the source of truth.

The **permission system is the source of truth**.

---

## Core Design Principles

### 1. Permissions Over Modes

All gameplay systems must query **permissions**, not game modes.

```cpp
if (!player.permissions.can_modify_world) return;
```

Never:

```cpp
if (player.mode == Creative) // ❌ forbidden
```

---

### 2. Game Modes Are Presets

Game modes:

* define a **default permission set**
* can be overridden at runtime
* are **not authoritative on their own**

---

### 3. Layered Overrides

Final permissions are the result of layered evaluation:

```
base (game mode)
+ temporary modifiers (effects, admin tools)
+ contextual overrides (regions, plugins)
------------------------------------------
= final resolved permissions
```

---

### 4. Server Authority

* Permissions are **fully server authoritative**
* Clients must not assume permissions
* All actions must be validated server-side

---

## Permission Set

Permissions are a **flat, explicit capability structure**.

```yaml
can_take_damage: bool
can_modify_world: bool
can_interact: bool
can_open_inventory: bool
has_creative_inventory: bool
can_fly: bool
has_collision: bool
is_visible_to_others: bool
```

### Notes

* Keep permissions **simple and orthogonal**
* Avoid derived/implicit permissions
* Add new permissions only when necessary

---

## Permission Resolution

Final permissions are resolved in this order:

1. **Game Mode Base**
2. **Temporary Modifiers**

   * status effects
   * admin overrides
3. **Contextual Overrides**

   * regions (e.g. spawn protection)
   * plugin-defined rules

### Resolution Rules

* Later layers override earlier ones
* Overrides are **explicit (no merging ambiguity)**

Example:

```yaml
base:
  can_modify_world: true

region_override:
  can_modify_world: false

result:
  can_modify_world: false
```

---

## Built-in Game Modes

Built-in modes are **fixed identifiers** and must not be renamed.

They may be referenced by:

* networking
* saves
* plugins

The generic permission framework lives in `engine/arv_player`. The built-in
Arvane presets are registered by `engine/arv_base` during application/runtime
composition and then passed to the server runtime. Generic systems query the
resolved permissions and do not depend on `arv_base`.

`arv_base` registers these built-in IDs:

```text
survival
creative
spectator
lobby
noclip
```

---

### Survival (default)

```yaml
can_take_damage: true
can_modify_world: true
can_interact: true
can_open_inventory: true
has_creative_inventory: false
can_fly: false
has_collision: true
is_visible_to_others: true
```

**Notes:**

* Standard gameplay
* Default health: 20 HP
* Fully participates in all systems

---

### Creative

```yaml
can_take_damage: false
can_modify_world: true
can_interact: true
can_open_inventory: true
has_creative_inventory: true
can_fly: true
has_collision: true
is_visible_to_others: true
```

**Notes:**

* Full building freedom
* Flight enabled
* No noclip

---

### Spectator

```yaml
can_take_damage: false
can_modify_world: false
can_interact: false
can_open_inventory: false
has_creative_inventory: false
can_fly: true
has_collision: true
is_visible_to_others: false
```

**Notes:**

* Observation only
* Invisible to others
* **Collision is enabled (intentional)**
* No interaction of any kind

---

### Lobby

```yaml
can_take_damage: false
can_modify_world: false
can_interact: false
can_open_inventory: true
has_creative_inventory: false
can_fly: false
has_collision: true
is_visible_to_others: true
```

**Notes:**

* Safe zone behavior
* No world interaction
* UI still accessible

---

### NoClip

```yaml
can_take_damage: false
can_modify_world: false
can_interact: false
can_open_inventory: false
has_creative_inventory: false
can_fly: true
has_collision: false
is_visible_to_others: false
```

**Notes:**

* Debug/admin mode
* Full free movement
* Not intended for standard gameplay

---

## Custom Game Modes

The generic registry supports defining new modes via config/plugins.

Example:

```yaml
game_modes:
  builder_plus:
    base: creative
    overrides:
      is_visible_to_others: false
```

### Rules

* Built-in modes cannot be modified or renamed once registered
* Custom modes:

  * must define a base
  * may override any permission
* Stored as **string identifiers**

---

## Implementation Notes

### Storage

* Permissions should be stored as a **compact struct or bitset**
* Game mode stored as:

```cpp
struct PlayerMode {
    ModeId mode;
    PermissionSet resolved_permissions;
};
```

---

### Resolution Strategy

* Recompute permissions:

  * on mode change
  * on entering/leaving regions
  * on effect apply/remove

* Cache the **final resolved permissions**

  * do NOT recompute per check

---

### System Integration

All systems must rely on permissions:

#### Physics

* `has_collision`
* `can_fly`

#### Damage System

* `can_take_damage`

#### Interaction System

* `can_modify_world`
* `can_interact`

#### Inventory/UI

* `can_open_inventory`
* `has_creative_inventory`

#### Networking

* `is_visible_to_others`

---

## Networking

* Server sends:

  * current game mode
  * resolved permissions (optional optimization)

* Client:

  * treats permissions as authoritative
  * uses them for prediction/UI only

---

## Save Format

* Store:

  * game mode ID (string or enum)
* Do NOT store:

  * resolved permissions

Permissions must be recomputed on load.

---

## Explicit TODOs (Agent-Facing)

* Define `PermissionSet` representation (bitmask recommended)
* Implement permission resolution pipeline
* Integrate permission checks across:

  * physics
  * interaction
  * UI
  * networking
* Add region-based override hooks
* Add temporary modifier system (effects/admin)

---

## Future Extensions

This system is designed to support:

* region protection (claims, safe zones)
* status effects (e.g. “no build”, “ghost”)
* plugin-defined rules
* per-player overrides
* scripted gameplay constraints

---
