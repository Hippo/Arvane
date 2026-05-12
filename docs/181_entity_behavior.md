# Entity Behavior

## Intent

Entity behavior defines how non-player entities decide and act during server simulation.

Behavior is data-driven and ECS-compatible. Entity definitions may attach behavior components, but behavior execution is owned by generic ECS systems, not by per-entity subclasses.

The first-class behavior model is a **Brain** containing prioritized **Goals**.

Behavior trees may be added later, but they are not the baseline behavior model.

---

## Core Rules

* Entity behavior runs on the authoritative server.
* Clients receive replicated entity state; clients do not authoritatively decide AI behavior.
* Entities do not have subclass-specific tick methods.
* Behavior is composed from ECS components and registered behavior primitives.
* Entity definitions reference behavior data; they do not embed C++ logic.
* Goals may read ECS/world state through stable interfaces.
* Goals may request movement, targeting, attacks, animations, sounds, and interaction intent.
* Goals should not directly mutate unrelated systems unless explicitly designed to do so.

---

## Design Model

```text
Entity Definition
  -> ECS Components
  -> Brain Component
      -> Goal Slots
          -> Registered Goal Type
          -> Data Parameters
```

Example:

```yaml
behavior:
  brain:
    tick_rate: 5
    goals:
      - type: arvane:float_in_fluid
        priority: 0

      - type: arvane:melee_attack
        priority: 2
        target: nearest_player
        range: 1.8
        cooldown_ticks: 20

      - type: arvane:wander
        priority: 8
        radius: 12
        speed_multiplier: 0.8
        animation: walk
```

Lower priority numbers run first.

---

## Brain Component

The `Brain` component stores the runtime behavior state for an entity.

Recommended conceptual shape:

```cpp
struct BrainComponent {
    BehaviorID behavior;
    u32 tick_interval;
    u32 next_tick;

    SmallVector<GoalSlot, 8> goals;

    // Runtime state.
    GoalHandle active_goal;
    GoalBlackboard blackboard;
};
```

The exact storage may differ, but the model should preserve:

* behavior asset/reference
* configured goals
* active goal state
* cooldowns/timers
* blackboard memory
* tick throttling

---

## Goal Slot

A goal slot is a configured instance of a registered goal type.

```cpp
struct GoalSlot {
    GoalTypeID type;
    i32 priority;
    GoalParams params;

    GoalRuntimeState state;
};
```

Goal slots are sorted by priority. If two goals have the same priority, ordering is stable and data-defined order wins.

---

## Goal Lifecycle

Each goal type implements the same conceptual lifecycle:

```text
can_start(entity, context) -> bool
can_continue(entity, context) -> bool
start(entity, context)
tick(entity, context)
stop(entity, context, reason)
```

### `can_start`

Determines whether an inactive goal may begin.

Examples:

* target exists
* entity is in fluid
* entity is on ground
* cooldown has expired
* path can be found
* nearby block/entity condition is satisfied

### `can_continue`

Determines whether the currently active goal may keep running.

Examples:

* target is still alive
* path is still valid
* entity has not reached destination
* panic timer has not expired

### `start`

Initializes runtime state.

Examples:

* choose target
* choose destination
* start cooldown
* request path
* set animation state

### `tick`

Executes behavior intent.

Examples:

* request movement
* request look direction
* request attack
* update path following
* update goal timer

### `stop`

Cleans up runtime state.

Examples:

* clear temporary path
* clear movement intent
* clear attack intent
* reset animation flag
* record cooldown

---

## Arbitration

The brain decides which goal is active.

Basic algorithm:

```text
1. If the active goal exists and can_continue returns false:
     stop active goal.

2. Scan goals in priority order.

3. The first goal whose can_start returns true becomes the candidate.

4. If no active goal exists:
     start candidate.

5. If candidate has higher priority than the active goal:
     stop active goal with interrupted reason.
     start candidate.

6. Tick the active goal.
```

A lower priority number means higher importance.

Example:

```yaml
goals:
  - type: float_in_fluid
    priority: 0
  - type: panic
    priority: 1
  - type: melee_attack
    priority: 2
  - type: wander
    priority: 8
```

In this example, `float_in_fluid` can interrupt `melee_attack`, and `melee_attack` can interrupt `wander`.

---

## Goal Categories

Goals may optionally declare a category so incompatible goals do not run together.

Recommended categories:

```cpp
enum class GoalCategory : u32 {
    Movement = 1 << 0,
    Look     = 1 << 1,
    Target   = 1 << 2,
    Attack   = 1 << 3,
    Utility  = 1 << 4,
};
```

Initial implementation may allow only one active goal. A later implementation may allow multiple active goals if their categories do not conflict.

Example:

```text
look_at_target     -> Look
move_to_target     -> Movement
melee_attack       -> Attack
acquire_target     -> Target
float_in_fluid     -> Movement
```

This allows an entity to look at a target while moving, but prevents two movement goals from fighting over velocity.

---

## Blackboard

The blackboard stores temporary behavior memory.

Examples:

```yaml
blackboard:
  target_entity
  home_position
  path_request
  current_path
  panic_until_tick
  attack_cooldown_until_tick
  wander_destination
```

The blackboard is runtime state. It is not necessarily saved unless explicitly required.

Rules:

* Use blackboard state for temporary behavior memory.
* Use ECS components for durable entity state.
* Do not store large world snapshots in the blackboard.
* Do not store raw pointers to chunks, entities, or world data.
* Use entity handles/IDs and validate them before use.

---

## Behavior Context

Goals should receive a limited context object instead of direct access to every engine subsystem.

Conceptual shape:

```cpp
struct BehaviorContext {
    WorldView& world;
    EntityRegistry& registry;
    PathfindingService& pathfinding;
    RandomSource& random;
    TickInfo tick;
};
```

The goal context should expose stable operations such as:

```text
is_solid(block_pos)
get_fluid(block_pos)
find_nearest_player(pos, radius)
has_line_of_sight(a, b)
request_path(entity, destination)
set_movement_intent(entity, intent)
set_look_intent(entity, intent)
request_attack(entity, target)
emit_event(event)
```

Avoid giving goals unrestricted ownership over world/chunk internals.

---

## Movement Integration

Behavior goals do not directly teleport or integrate physics for normal movement.

Instead, goals write movement intent:

```cpp
struct MovementIntent {
    Vec3 desired_direction;
    float speed_multiplier;
    bool jump;
    bool sprint;
};
```

The movement/physics systems consume this intent during the fixed simulation tick.

This preserves the separation between:

```text
AI decision making
movement control
physics resolution
collision response
replication
```

---

## Targeting

Targeting should be reusable and data-driven.

Example:

```yaml
- type: arvane:nearest_target
  priority: 3
  target_groups: [arvane:player]
  radius: 24
  require_line_of_sight: true
  memory_ticks: 100
```

Target goals may write `target_entity` into the blackboard or into a dedicated target component.

A target must be revalidated before use.

Invalid targets include:

* despawned entity
* dead entity
* entity outside tracking radius
* entity outside loaded/simulated area
* entity no longer visible when visibility is required

---

## Pathfinding

Pathfinding is a service used by goals, not owned by goals.

Rules:

* Goals may request paths.
* Pathfinding may complete asynchronously.
* Goals must tolerate pending, failed, or stale path requests.
* Paths must be invalidated when relevant terrain changes.
* Goals should not block simulation waiting for pathfinding.

Example:

```text
move_to_target:
  if no path:
    request path
  if path pending:
    use simple steering or wait
  if path ready:
    follow path
  if path stale:
    request replacement
```

---

## Tick Rate

Brains may tick slower than the fixed simulation rate.

Example:

```yaml
behavior:
  brain:
    tick_rate: 5
```

This means behavior decisions update every 5 simulation ticks.

Physics and movement still run every fixed tick.

Recommended rule:

```text
AI decision making may be throttled.
Movement intent consumption should not be throttled.
```

---

## Events

Goals may react to events through blackboard updates or event components.

Examples:

* damaged
* saw target
* heard sound
* collided
* entered fluid
* attacked successfully
* path failed

Example:

```yaml
events:
  on_damage:
    set_blackboard:
      attacker: event.source
      panic_until_tick: now + 80
```

Initial implementation can keep event handling simple. Avoid building a full scripting language too early.

---

## Serialization

Entity definitions store behavior configuration.

Runtime save data may store:

* current active goal
* important cooldowns
* persistent blackboard keys
* home position
* persistent target/team/faction state

Runtime save data should not store:

* raw pointers
* pathfinding internals
* temporary movement intent
* transient event queues
* chunk references

---

## Networking

Behavior is not replicated as decision logic.

Server replicates the resulting entity state:

* position
* velocity
* rotation/look direction
* animation state
* health/state components
* relevant interaction state

Clients may predict/interpolate visual movement, but server remains authoritative.

Debug builds may optionally expose brain/goal state to developer UI.

---

## Behavior Registry

Goal types are registered by ID.

Example:

```cpp
entity::EntityBehaviorRegistry behavior_registry;
base::register_base_entity_behaviors(behavior_registry);

auto entity_registry = entity::EntityRegistry::build(
    definitions,
    {.behaviors = &behavior_registry});
```

Rules:

* Goal IDs are stable namespaced identifiers.
* Runtime lookup may map IDs to dense integer IDs.
* Unknown goal IDs should fail validation with a clear error.
* Invalid parameters should fail data loading, not crash simulation.
* Generic entity registries own loading and validation.
* `engine/arv_base` contributes built-in `arvane:*` behavior IDs.
* Entity definitions live in pack data unless a definition has compiled
  invariants, such as the player entity.

---

## Built-In Goals

Built-in goals should stay small and reusable.

The current `arv_base` registration set is:

```text
arvane:idle
arvane:wander
arvane:float_in_fluid
```

Additional reusable goals can be added behind stable IDs:

```text
arvane:look_at_target
arvane:nearest_target
arvane:move_to_target
arvane:melee_attack
arvane:flee
arvane:avoid_hazard
arvane:despawn_when_far
```

Avoid adding entity-specific goals like:

```text
arvane:zombie_tick
arvane:cow_tick
arvane:skeleton_tick
```

Prefer reusable behavior primitives with parameters.

---

## Behavior Trees

Behavior trees are a future extension, not the initial baseline.

If added, they should integrate as a goal type:

```yaml
- type: arvane:behavior_tree
  priority: 3
  tree: arvane:boss_behavior
```

This keeps the brain as the top-level arbitration model.

The brain decides when the tree may run. The tree decides detailed behavior while active.

---

## Debugging

The debug UI should be able to inspect:

* current active goal
* goal priority list
* why goals cannot start
* current target
* current path status
* blackboard values
* recent interrupts/stops
* behavior tick interval

This is important because AI bugs are often arbitration bugs rather than movement bugs.

---

## Non-Goals

This document does not define:

* entity definition schema
* animation graph format
* pathfinding implementation details
* client-authored player movement rules
* combat damage formulas

Those should be covered by separate documents.

---

## Recommended First Implementation

Implement the following first:

```text
BrainComponent
Goal registry
Goal lifecycle
Single active goal arbitration
Blackboard
MovementIntent output
Target selection goal
Wander goal
Move-to-target goal
Melee attack goal
Float-in-fluid goal
Debug inspection hooks
```

Defer:

```text
multiple simultaneous goals
behavior trees
advanced event graph
complex NPC schedules
boss behavior
```
