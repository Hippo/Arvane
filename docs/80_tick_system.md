# Ticking System Specification

## Overview

The Arvane Engine uses a **multi-rate, budgeted tick scheduling system** designed for:

* High-performance voxel simulation
* Server-authoritative multiplayer
* Deterministic and stable world updates
* Configurable behavior across worlds/servers

The system decouples:

1. **Scheduler tick rate (base TPS)**
2. **System update frequency (intervals)**
3. **Work limits (budgets)**

This allows fine-grained control over performance and simulation behavior without sacrificing responsiveness.

---

## Core Concepts

### 1. Base Tick Rate (TPS)

The engine operates on a fixed **base tick rate**:

```
base_tps ∈ {20, 30, 60}
```

Default:

```
base_tps = 60
```

Each tick represents:

```
tick_duration = 1 / base_tps seconds
```

The base tick rate defines:

* Input processing cadence
* Physics resolution
* Network update frequency
* Scheduler granularity

> IMPORTANT: Base TPS is the **scheduler resolution**, not the update frequency of all systems.
> It is not a tuning knob for individual gameplay speeds such as water or lava flow.

Local player movement uses this same fixed tick cadence. After joining a world,
the client seeds its local movement tick from the server-provided initial tick
so outgoing `ServerboundPlayerMove` packets can be correlated with server
diagnostics. Local movement must not advance from render-frame delta time.

---

### 2. Tick Phases

Each tick is divided into ordered phases:

```
1. Input Phase
2. Simulation Phase
3. Post-Simulation Phase
4. Output/Networking Phase
```

#### Input Phase

* Player inputs
* Network packet ingestion

#### Simulation Phase

* Physics
* Entity updates
* Block updates
* Fluids
* AI

#### Post-Simulation Phase

* Cleanup
* Deferred operations
* Event dispatch

#### Output Phase

* Snapshot generation
* Network replication

---

### 3. System Scheduling

Each system defines:

```cpp
struct SystemSchedule {
    uint32_t interval;   // number of ticks between updates
    uint32_t accumulator;
};
```

Execution rule:

```
if (accumulator >= interval):
    run system
    accumulator = 0
else:
    accumulator++
```

---

### 4. System Frequency

Effective system frequency:

```
system_tps = base_tps / interval
```

Example (base_tps = 60):

| System        | Interval | Frequency |
| ------------- | -------- | --------- |
| Physics       | 1        | 60 TPS    |
| Entities      | 1        | 60 TPS    |
| AI            | 2        | 30 TPS    |
| Block Updates | 3        | 20 TPS    |
| Fluid Queue Dispatch | 1 | 60 TPS    |
| Random Ticks  | 20       | 3 TPS     |

---

### 5. Work Budgets

Systems that can grow unbounded must define a **budget**:

```cpp
struct SystemBudget {
    uint32_t max_operations_per_run;
};
```

Examples:

* Fluid propagation
* Lighting updates
* Block event processing
* Pathfinding

Execution:

```cpp
for (i = 0; i < max_operations && !queue.empty(); i++) {
    process(queue.pop());
}
```

> Budgets guarantee **bounded CPU usage per tick**, regardless of world size.

---

## World Tick Policy

Each world defines a `WorldTickPolicy`:

```cpp
struct WorldTickPolicy {
    uint32_t base_tps;

    uint32_t physics_interval;
    uint32_t entity_interval;
    uint32_t ai_interval;
    uint32_t block_update_interval;
    uint32_t fluid_interval;
    uint32_t random_tick_interval;

    uint32_t fluid_budget;
    uint32_t block_event_budget;
    uint32_t lighting_budget;
};
```

`fluid_interval` controls how often due fluid events are drained and submitted to the
bounded simulation worker path. Liquid speed itself is configured on the fluid type
through `flow_interval_ticks`.

---

### Example Configuration (YAML)

```yaml
simulation:
  base_tps: 60
  systems:
    physics:
      interval: 1
    entities:
      interval: 1
    ai:
      interval: 2
    block_updates:
      interval: 3
      budget: 5000
    fluids:
      interval: 1
      budget: 2000
    random_ticks:
      interval: 20
      samples_per_chunk: 3
  fluids:
    water:
      flow_interval_ticks: 10
    lava:
      flow_interval_ticks: 30
```

---

## Time Abstraction

### Problem

Direct reliance on ticks causes instability when TPS changes.

### Solution

All gameplay systems should use **time-based APIs** instead of raw ticks.

---

### Engine Time Types

```cpp
using Seconds = float;
using Milliseconds = uint32_t;
```

---

### Scheduling APIs

Preferred:

```cpp
every_seconds(0.25, callback);
after_seconds(2.0, callback);
```

Advanced:

```cpp
every_ticks(3, callback);
```

---

### Conversion

Internally:

```
ticks = seconds * base_tps
```

---

## Fluid Simulation Model

### Design Goals

* Deterministic
* Bounded CPU usage
* Visually responsive
* Scalable

---

### Core Structure

```cpp
struct FluidUpdate {
    BlockPos pos;
    TickIndex execute_at;
};
```

System maintains:

```
scheduled_queue<FluidUpdate> pending_updates;
```

---

### Execution

* Runs queue dispatch at `fluid_interval`
* Processes only updates whose `execute_at <= current_tick`
* Processes up to `fluid_budget` due updates per run
* Commits fluid deltas on the simulation thread

Each fluid type defines:

```cpp
struct FluidDefinition {
    uint32_t flow_interval_ticks;
};
```

When a fluid cell changes, follow-up work for that cell and affected neighbors is
scheduled at:

```
current_tick + fluid.flow_interval_ticks
```

Fluid speed changes update cadence, not propagation distance. A faster fluid may
schedule its next step sooner, but it still moves one discrete flow step at a time.

---

### Update Triggering

Fluid updates are triggered by:

* Block placement/removal
* Neighbor changes
* Fluid propagation

---

### Key Rule

> Fluid simulation must be **event-driven**, not full-grid iteration.
> Rendering, meshing, lighting, input, and block placement must not synchronously run
> fluid propagation.

---

## Determinism & Networking

### Server Authority

* Server tick defines canonical state
* Clients interpolate between snapshots

---

### Requirements

* Fixed tick rate per world
* Deterministic system ordering
* No floating-point drift in core logic

---

### Client Awareness

Clients must know:

```
- server base_tps
- snapshot rate
```

---

## Threading Model

### Rules

* Systems may run on worker threads
* World mutation must be synchronized

---

### Recommended Approaches

1. Chunk-level locking
2. Command buffers (preferred)

---

### Example

```cpp
executor.silent_async([&] {
    process_fluid_queue();
});
```

---

## Presets

### Competitive

```yaml
simulation:
  base_tps: 60
  systems:
    ai:
      interval: 1
    fluids:
      interval: 1
      budget: 1500
  fluids:
    water:
      flow_interval_ticks: 8
```

---

### Survival (Default)

```yaml
simulation:
  base_tps: 60
  systems:
    ai:
      interval: 2
    fluids:
      interval: 1
      budget: 2000
  fluids:
    water:
      flow_interval_ticks: 10
    lava:
      flow_interval_ticks: 30
```

---

### Large Server

```yaml
simulation:
  base_tps: 30
  systems:
    ai:
      interval: 2
    fluids:
      interval: 1
      budget: 1000
  fluids:
    water:
      flow_interval_ticks: 10
    lava:
      flow_interval_ticks: 30
```

---

## Anti-Patterns (DO NOT IMPLEMENT)

* Updating all systems every tick
* Unbounded fluid propagation
* Tick-dependent gameplay logic without time abstraction
* Arbitrary floating-point TPS
* Full world iteration per system
* Changing global TPS to tune water, lava, or other individual simulation speeds
* Making a fluid move multiple blocks in one tick to imitate faster flow

---

## Key Principles

1. **60 TPS is a scheduler, not a workload multiplier**
2. **All heavy systems must be budgeted**
3. **Simulation frequency is per-system, not global**
4. **Gameplay should depend on time, not ticks**
5. **Event-driven systems scale better than polling**
6. **Fluid speed is per-fluid cadence, not global TPS**

---

## Summary

The Arvane ticking system provides:

* High responsiveness (60 TPS core)
* Stable performance (budgeted systems)
* Flexibility (per-world configuration)
* Scalability (event-driven updates)

This architecture enables complex voxel worlds while maintaining consistent performance across different server configurations.

---
