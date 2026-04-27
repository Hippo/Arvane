# Permission Groups & Roles

## Purpose

This document defines:

* **Permission groups (roles)** as reusable permission containers
* **Inheritance between groups**
* How groups integrate into the **permission resolution pipeline**

This system enables:

* admin/mod/player hierarchies
* clean, scalable permission management
* plugin and server configurability

---

## Core Concepts

### What is a Group?

A **group** is a named collection of permissions.

Example:

```yaml
groups:
  admin:
    permissions:
      arvane.*: ALLOW
```

Groups can define:

* core permission overrides
* custom permission nodes
* inheritance from other groups

---

### What is a Role?

A **role** is simply a group assigned to a player.

> In this system, *group = role* (no distinction needed)

---

## Group Definition

### Structure

```yaml
groups:
  <group_id>:
    inherits: [optional list of groups]
    priority: int (optional)
    core:
      can_modify_world: ALLOW
      can_take_damage: DENY
    permissions:
      arvane.command.*: ALLOW
      plugin.shop.buy: DENY
```

---

## Inheritance

Groups may inherit from other groups:

```yaml
groups:
  mod:
    inherits: [player]
    permissions:
      arvane.command.kick: ALLOW

  admin:
    inherits: [mod]
    permissions:
      arvane.*: ALLOW
```

### Rules

* Inheritance is **additive**
* Child groups override parent values
* Circular inheritance is **invalid**

---

## Priority System (Within Groups)

Groups may define a priority:

```yaml
priority: int
```

Higher value = higher priority

Example:

```yaml
player:  priority 0
mod:     priority 50
admin:   priority 100
```

---

## Group Resolution Order

When a player has multiple groups:

1. Collect all groups
2. Expand inheritance tree
3. Sort by priority (lowest → highest)
4. Apply permissions in order
5. Apply **DENY precedence**

---

### Example

```yaml
groups:
  player:
    priority: 0
    permissions:
      plugin.shop.buy: ALLOW

  restricted:
    priority: 100
    permissions:
      plugin.shop.buy: DENY
```

Player has both groups → result:

```yaml
plugin.shop.buy: DENY
```

---

## Integration into Permission Pipeline

Groups are part of **Player Overrides layer**.

Updated priority stack:

```text
5. Plugin/System Overrides
4. Region-Based Permissions
3. Temporary Modifiers
2. Player Overrides
   - Groups (this document)
   - Direct player overrides
1. Game Mode
```

---

## Player Assignment

Players may have:

* one or more groups
* optional direct overrides

Example:

```yaml
player:
  groups: [player, mod]
  overrides:
    arvane.command.fly: DENY
```

---

## Core Permission Handling

Groups may override **core permissions**:

```yaml
core:
  can_modify_world: DENY
```

This integrates seamlessly with:

* game mode base
* region restrictions
* temporary effects

---

## Custom Permission Handling

Groups define node-based permissions:

```yaml
permissions:
  arvane.command.*: ALLOW
  arvane.command.shutdown: DENY
```

All rules from previous doc apply:

* wildcard support
* specificity rules
* DENY precedence

---

## Conflict Resolution

### Within Groups

1. Higher priority group wins
2. If equal priority:

   * DENY wins over ALLOW

---

### Across System

Even after group resolution:

* higher system layers can still override

Example:

```yaml
group:
  can_modify_world: ALLOW

region:
  can_modify_world: DENY
```

Final:

```yaml
can_modify_world: DENY
```

---

## Data Model

### Group Definition

```cpp
struct PermissionGroup {
    std::string id;
    int priority;

    std::vector<GroupId> inherits;

    CorePermissionSet core;
    CustomPermissionMap permissions;
};
```

---

### Player Permissions

```cpp
struct PlayerPermissions {
    std::vector<GroupId> groups;
    CorePermissionSet overrides;
    CustomPermissionMap custom_overrides;
};
```

---

## Resolution Strategy

### Step 1: Resolve Groups

* expand inheritance
* sort by priority
* merge into single group result

### Step 2: Apply Player Overrides

### Step 3: Feed into main pipeline

---

## Validation Rules

* group IDs must be unique
* no circular inheritance
* missing group references = error
* priority defaults to 0 if unspecified

---

## Performance Considerations

* cache resolved group results per player
* invalidate cache on:

  * group change
  * config reload
* pre-expand inheritance trees where possible

---

## Networking

Server may send:

* group IDs (optional)
* resolved permissions (preferred)

Client:

* does not resolve groups
* only consumes final permissions

---

## Save Format

Persist:

* player group assignments
* player-specific overrides

Do NOT persist:

* resolved group results
* temporary group changes (unless intended)

---

## Example Full Configuration

```yaml
groups:
  player:
    priority: 0
    permissions:
      plugin.chat.send: ALLOW

  mod:
    priority: 50
    inherits: [player]
    permissions:
      arvane.command.kick: ALLOW

  admin:
    priority: 100
    inherits: [mod]
    permissions:
      arvane.*: ALLOW

players:
  hippo:
    groups: [admin]
```

---

## Explicit TODOs (Agent-Facing)

* Implement group storage and lookup
* Implement inheritance resolution
* Add priority-based sorting
* Integrate group resolution into permission pipeline
* Add validation (cycle detection)
* Add caching layer

---

## Future Extensions

This system naturally supports:

* timed group membership (e.g. temp admin)
* world-specific groups
* permission syncing across servers
* GUI-based permission editors

---

## Key Takeaway

Groups provide **structure and scalability**.

Permissions provide **truth and control**.

Together, they form a system that is:

* flexible
* performant
* server-ready

---

