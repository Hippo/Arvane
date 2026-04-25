# Event System

## Overview

The event system provides a controlled way for engine modules, gameplay code, and plugins to observe and react to engine activity without directly depending on each other.

Events are used for:

- gameplay reactions
- plugin hooks
- command integration
- diagnostics
- internal decoupling between systems

Events are **not** a replacement for direct system ownership, simulation phases, or job scheduling.

---

## Design Goals

- Keep engine systems decoupled
- Provide stable plugin-facing hooks
- Preserve server authority
- Avoid unsafe cross-thread mutation
- Make event ordering predictable
- Prevent event handlers from becoming hidden control flow chaos

---

## Event Categories

Events are divided into two broad categories:

1. **Immediate events**
2. **Queued events**

---

## Immediate Events

Immediate events are dispatched synchronously at a known point in engine execution.

Use immediate events when:

- ordering matters
- the event is part of a simulation decision
- handlers may cancel or modify the action
- the caller needs a result before continuing

Examples:

- block placement validation
- item use validation
- command execution
- player interaction checks

Immediate events must only be fired from the owning thread of the related system.

---

## Queued Events

Queued events are recorded and dispatched later during a defined event flush phase.

Use queued events when:

- the event is observational
- systems only need to react after the fact
- the event crosses thread boundaries
- batching is useful
- handlers should not affect the original action

Examples:

- block placed
- block broken
- chunk loaded
- entity spawned
- player joined
- inventory changed

Queued events are the default choice unless immediate behavior is required.

---

## Event Phases

The engine defines explicit event phases.

A typical simulation tick may look like:

1. Poll network input
2. Apply player commands
3. Run pre-simulation immediate hooks
4. Simulate worlds and entities
5. Flush queued simulation events
6. Run post-simulation systems
7. Prepare replication packets
8. Submit render/client snapshots

Events must be dispatched only from documented phases.

Plugins must not assume that events fire immediately unless the event is explicitly documented as immediate.

---

## Event Ordering

For events emitted during a single tick:

- events preserve emission order within the same world
- events from different worlds do not have a guaranteed global order
- events from worker jobs are ordered by enqueue time after being returned to the owning thread
- event handlers run in registration order unless priority is specified

The engine may later support handler priorities, but priority should be used sparingly.

---

## Event Context

Every event should carry enough context to be useful without exposing internal engine storage.

Common event fields include:

```cpp
struct EventContext {
    TickID tick;
    WorldID world;
};
```

Some global events may omit `WorldID`.

Events should prefer IDs and value copies over pointers.

---

## Cancellable Events

Some immediate events may be cancellable.

Example:

```cpp
struct BeforeBlockPlaceEvent {
    WorldID world;
    PlayerID player;
    BlockPos pos;
    BlockStateID requested_state;
    bool cancelled = false;
};
```

Rules:

* cancellable events must be immediate
* cancellation must be checked by the caller
* queued events must not be cancellable
* cancellation should only prevent future action, not undo completed mutation

---

## Result-Modifying Events

Some immediate events may allow handlers to modify a requested action.

Example:

```cpp
struct ResolveBlockPlacementEvent {
    WorldID world;
    PlayerID player;
    BlockPos pos;
    BlockStateID requested_state;
    BlockStateID resolved_state;
};
```

This can support behavior such as:

* rotating stairs based on player facing
* replacing placement with a different state
* redirecting interaction behavior

Result-modifying events should be rare and clearly documented.

---

## Observational Events

Most events should be observational.

Example:

```cpp
struct BlockPlacedEvent {
    WorldID world;
    BlockPos pos;
    BlockStateID old_state;
    BlockStateID new_state;
    PlayerID actor;
};
```

Handlers may react, but they must not assume they can alter the completed action.

---

## Threading Rules

Event handlers run on the thread that dispatches the event.

Plugin-facing events should generally dispatch on the simulation thread.

Worker threads may produce events, but they must enqueue them back to the owning thread before dispatch.

Forbidden:

* dispatching plugin events directly from arbitrary worker threads
* mutating world state from an event handler running on a worker thread
* storing raw references from event payloads
* blocking inside event handlers

---

## Handler Rules

Event handlers must be small and predictable.

Handlers may:

* inspect event data
* enqueue commands
* request world mutations through safe APIs
* schedule background work
* emit additional queued events when allowed

Handlers must not:

* directly mutate engine internals
* block on worker jobs
* assume raw pointer stability
* throw exceptions across the plugin boundary
* recursively trigger unbounded event chains

---

## Event Reentrancy

The event system should guard against accidental recursive chaos.

Immediate events may trigger other immediate events only when explicitly allowed.

Queued event flushing should avoid recursively flushing newly generated events in the same pass unless documented.

Default rule:

* events generated while flushing are queued for the next flush phase

This keeps event behavior predictable.

---

## Subscription Model

Plugins and systems subscribe to events through the `EventAPI`.

Conceptually:

```cpp
struct EventAPI {
    EventSubscription subscribe(
        EventTypeID type,
        EventHandlerFn handler,
        EventSubscriptionOptions options
    );

    void unsubscribe(EventSubscription subscription);
};
```

The actual public API should avoid exposing templates across the plugin ABI boundary unless the boundary is C++-only and version-locked.

---

## Event Subscription Options

```cpp
struct EventSubscriptionOptions {
    i32 priority = 0;
    bool receive_cancelled = false;
};
```

Priority rules:

* lower priority runs earlier, or
* higher priority runs earlier

The engine must pick one convention and document it.

Recommendation:

* lower numbers run earlier
* default priority is `0`
* engine-reserved priorities may use negative ranges

---

## Subscription Lifetime

Subscriptions are owned by the plugin or system that created them.

When a plugin unloads:

* all of its subscriptions must be removed
* no handler from that plugin may be called again
* pending queued events must not retain plugin-owned function pointers

The engine should associate subscriptions with the owning plugin handle.

---

## Event Payload Ownership

Event payloads are owned by the dispatcher.

Handlers receive temporary read-only access unless the event is explicitly mutable.

Rules:

* do not store references to event payloads
* copy IDs or values if data is needed later
* use engine APIs to fetch current state later
* mutable payloads are only valid for documented immediate events

---

## Event Type Registration

Engine-defined events have stable event type IDs.

Plugin-defined events may be registered during plugin initialization.

Example:

```cpp
EventTypeID register_event_type(EventTypeDefinition definition);
```

Plugin-defined events are useful for communication between plugins, but they must not become a replacement for documented engine APIs.

---

## Core Engine Events

### World Events

* `WorldCreatedEvent`
* `WorldLoadedEvent`
* `WorldUnloadedEvent`
* `WorldSavedEvent`

### Chunk Events

* `ChunkLoadedEvent`
* `ChunkUnloadedEvent`
* `ChunkGeneratedEvent`
* `ChunkMeshedEvent`
* `ChunkBecameActiveEvent`
* `ChunkBecameInactiveEvent`

### Block Events

Immediate:

* `BeforeBlockPlaceEvent`
* `BeforeBlockBreakEvent`
* `ResolveBlockPlacementEvent`

Queued:

* `BlockPlacedEvent`
* `BlockBrokenEvent`
* `BlockUpdatedEvent`
* `BlockNeighborChangedEvent`

### Entity Events

* `EntitySpawnedEvent`
* `EntityDestroyedEvent`
* `EntityMovedEvent`
* `EntityEnteredChunkEvent`
* `EntityLeftChunkEvent`

### Player Events

* `PlayerJoinedEvent`
* `PlayerLeftEvent`
* `PlayerChangedWorldEvent`
* `PlayerDiedEvent`
* `PlayerRespawnedEvent`

### Inventory Events

* `InventoryChangedEvent`
* `ItemAddedEvent`
* `ItemRemovedEvent`
* `HeldItemChangedEvent`

### Interaction Events

Immediate:

* `BeforePlayerInteractEvent`
* `BeforeItemUseEvent`

Queued:

* `PlayerInteractedEvent`
* `ItemUsedEvent`

### Network Events

Plugin-facing network events should be limited.

Examples:

* `PlayerConnectedEvent`
* `PlayerDisconnectedEvent`
* `ClientSettingsChangedEvent`

Plugins should not receive raw packet streams by default.

---

## Block Event Flow

Block placement should follow a predictable flow:

1. Player requests placement
2. Engine validates reach and permissions
3. `BeforeBlockPlaceEvent`
4. `ResolveBlockPlacementEvent`
5. World mutation occurs
6. Lighting/mesh invalidation is scheduled
7. `BlockPlacedEvent` is queued
8. Event queue flushes later in the tick

This prevents plugins from bypassing core placement rules.

---

## Error Handling

If an event handler fails:

* the engine logs the error
* the failing plugin may be disabled
* other handlers should continue when safe
* exceptions must not cross the plugin ABI boundary

For immediate cancellable events, handler failure should not silently approve dangerous behavior.

The engine may choose fail-closed behavior for security-sensitive events.

---

## ABI Boundary Notes

Plugin-facing event APIs should avoid unstable C++ ABI assumptions.

Preferred ABI-safe concepts:

* plain structs
* explicit size/version fields
* function pointers
* opaque handles
* engine-owned allocation rules
* no STL containers across plugin boundary
* no exceptions across plugin boundary

Example event payload header:

```cpp
struct EventPayloadHeader {
    u32 size;
    u32 version;
    EventTypeID type;
};
```

---

## Non-Goals

The event system does not:

* own simulation order
* replace ECS systems
* replace the scheduler
* expose raw chunk mutation
* expose raw network packets
* guarantee deterministic ordering across worlds
* allow arbitrary thread execution

---

## Summary

The event system should be used as a structured communication layer.

Use:

* immediate events for validation and decision points
* queued events for observation and reactions
* explicit phases for predictable behavior
* IDs and copies instead of raw pointers
* safe APIs for all mutation

This keeps plugins powerful without letting them turn the engine into an unstructured callback maze.
