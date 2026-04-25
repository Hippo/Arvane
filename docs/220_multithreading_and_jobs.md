# 18. Multithreading and Jobs

## Purpose

Arvane uses multithreading to move expensive work off the main simulation path without allowing uncontrolled shared-state access.

This document defines:

- thread responsibilities
- job ownership rules
- world mutation rules
- chunk handoff patterns
- render-thread boundaries
- forbidden access patterns

The goal is to keep concurrency explicit, predictable, and debuggable.

---

## Core Rule

World state must not be freely mutated from arbitrary threads.

All systems must follow this rule:

> Worker jobs may compute results, but authoritative state mutation happens only through the owning thread or owning system.

For world data, the owning thread is the simulation thread.

For GPU resources, the owning thread is the render thread.

---

## Thread Roles

## Main Thread

The main thread owns application-level control flow.

Responsibilities:

- window events
- platform integration
- input collection
- high-level frame loop
- coordinating simulation and rendering
- submitting jobs to the scheduler
- receiving completed work

The main thread may also act as the simulation thread in early versions.

If simulation and rendering later move to separate threads, the main thread remains the coordinator.

---

## Simulation Thread

The simulation thread owns authoritative game and world state.

Responsibilities:

- fixed tick execution
- world mutation
- entity simulation
- block updates
- liquid updates
- lighting state publication
- chunk load/unload decisions
- applying completed job results
- producing render snapshots or render update commands

Only the simulation thread may directly mutate:

- loaded chunk storage
- block state
- lighting storage
- world entity state
- world tick queues
- chunk lifetime state

Worker jobs must not directly mutate these structures.

---

## Worker Threads

Worker threads execute background jobs.

Worker jobs may perform expensive computation on immutable inputs.

Examples:

- chunk generation
- chunk meshing
- lighting computation
- compression
- decompression
- save serialization
- pathfinding precomputation
- asset processing
- texture atlas building where no GPU calls are required

Worker jobs must receive explicit input data.

Valid worker inputs include:

- immutable snapshots
- copied data
- reference-counted immutable resources
- command payloads
- job-local temporary buffers

Worker jobs must produce explicit outputs.

Valid worker outputs include:

- mesh buffers
- generated chunk data
- lighting deltas
- serialized byte buffers
- decoded asset data
- world commands to be validated by the simulation thread

Worker jobs must not reach back into live world state.

---

## Render Thread

The render thread owns GPU interaction.

In the initial implementation, the render thread may be the main thread.

If a separate render thread exists, it owns:

- OpenGL context usage
- buffer creation
- texture creation
- shader creation
- framebuffer creation
- GPU upload commands
- draw submission
- render resource destruction

Only the render thread may call graphics API functions.

Worker jobs may build CPU-side render data, but they must not upload it directly to the GPU.

Examples:

Allowed:

- meshing job builds vertex/index buffers in CPU memory
- asset job decodes image bytes
- atlas job packs texture rectangles into CPU memory

Forbidden:

- meshing job creates an OpenGL buffer
- asset loader calls `glTexImage2D`
- worker deletes a GPU resource

---

## Job System Model

Arvane uses a job system for asynchronous work.

Jobs are units of computation with:

- explicit inputs
- explicit outputs
- no hidden ownership of world state
- no direct mutation of unrelated systems

Jobs should be small enough to run efficiently but large enough to avoid excessive scheduling overhead.

The scheduler provides execution primitives.

Ownership remains with the systems that submit and consume jobs.

---

## Job Graph Stages

The engine frame/tick may be organized into stages.

Example fixed tick structure:

```text
Collect Input
Begin Tick
Apply Completed Jobs
Run Simulation Systems
Queue World Jobs
Queue Render Updates
End Tick
```

Example render frame structure:

```text
Collect Render Commands
Apply GPU Uploads
Render Opaque World
Render Transparent World
Render Entities
Render UI
Present
```

Example background job flow:

```text
Simulation Thread
    |
    | create immutable snapshot
    v
Worker Thread
    |
    | compute result
    v
Completion Queue
    |
    | validate version / ownership
    v
Simulation or Render Thread applies result
```

---

## Ownership and Handoff Pattern

All cross-thread work should follow this pattern:

1. Owning system creates a job request.
2. Request contains immutable input data.
3. Worker computes result without touching live owner state.
4. Worker submits result to a completion queue.
5. Owning system validates result.
6. Owning system applies result.

For chunk work, validation usually includes:

* chunk coordinate
* world identifier
* source version
* job type
* dependency versions
* whether the chunk is still loaded
* whether the result is still newer than the current accepted result

Stale results must be discarded safely.

---

## Chunk Snapshot Rules

Chunk-related worker jobs must operate on snapshots.

A snapshot may contain:

* chunk coordinate
* chunk version
* block state data
* lighting data
* relevant neighbor boundary data
* biome data if needed
* block registry references
* material references
* meshing configuration

Snapshots must be immutable once submitted.

Worker jobs must not hold raw pointers into mutable chunk storage unless the pointed data is guaranteed immutable for the job lifetime.

Preferred approaches:

* copy compact chunk data into a job-owned buffer
* use immutable shared chunk snapshot objects
* use versioned read-only views with clear lifetime guarantees

---

## Meshing Jobs

Meshing jobs are worker-safe.

Input:

* immutable chunk snapshot
* neighbor boundary snapshots
* lighting snapshot
* block shape/material lookup data

Output:

* CPU-side vertex buffers
* CPU-side index buffers
* mesh metadata
* chunk coordinate
* source chunk version

Meshing jobs must not:

* mutate chunk data
* read live world state
* upload GPU buffers
* assume the chunk is still loaded when complete

The simulation/render owner validates the result before publishing it.

---

## Lighting Jobs

Lighting work may run on workers, but application of lighting results is owned by the simulation thread.

Lighting jobs may compute:

* light propagation results
* dirty-region updates
* relight plans
* border propagation requests

Lighting jobs must output either:

* a complete replacement lighting buffer for a chunk
* a validated lighting delta
* a relight command list

The simulation thread applies accepted lighting results.

Lighting jobs must not directly mutate live chunk lighting arrays.

---

## Chunk Generation Jobs

Chunk generation jobs may run on workers.

Input:

* world seed
* world generation settings
* chunk coordinate
* registry references
* generator configuration

Output:

* generated chunk block/state data
* biome data
* initial lighting hints if available
* population/decorator requests if needed

Generated chunks are not automatically inserted into the world.

The simulation thread inserts generated chunks after validating:

* the world still exists
* the chunk is still requested
* no newer chunk data already exists
* save data did not arrive first

Save-loaded data takes priority over generated data.

---

## Save and Load Jobs

Disk IO and serialization may run on workers.

Save jobs may receive immutable copies or snapshots of data to serialize.

Load jobs may decode region files and produce loaded chunk data.

Workers may perform:

* file reads
* file writes
* compression
* decompression
* binary encoding
* binary decoding

Workers must not directly insert loaded chunks into the live world.

Loaded data is submitted back to the simulation thread.

---

## Asset Jobs

Asset loading may be split between worker and render thread work.

Worker-safe:

* reading files
* decoding images
* parsing JSON/TOML/etc.
* building CPU-side atlas data
* compiling asset metadata
* preparing shader source strings

Render-thread-only:

* OpenGL texture creation
* OpenGL buffer creation
* shader program creation
* framebuffer creation
* GPU resource destruction

Asset jobs output CPU-side data to the render resource system.

---

## Completion Queues

Cross-thread results should be passed through completion queues.

Completion queues should be owned by the receiving system.

Examples:

* `CompletedChunkMeshes`
* `CompletedChunkLoads`
* `CompletedChunkGenerations`
* `CompletedLightingJobs`
* `CompletedAssetLoads`
* `PendingGpuUploads`

Completion queues should contain value-like result objects or owning smart pointers.

Avoid queues containing raw pointers into worker-local memory.

---

## Versioning and Stale Results

Any job operating on mutable world-derived data must include source version metadata.

Examples:

* chunk version
* lighting version
* mesh generation version
* registry version
* asset version
* world generation version

When a result returns, the owner must compare versions before applying it.

A result must be discarded if:

* the chunk was unloaded
* the chunk was regenerated
* the relevant block data changed
* the lighting data changed in a way that invalidates the result
* the target world was destroyed
* the result depends on an outdated registry/material configuration

Discarding stale jobs is normal behavior.

It must not be treated as an error.

---

## Data Access Rules

## Allowed

Worker jobs may access:

* their own local data
* immutable snapshots
* immutable registry data
* immutable asset metadata
* thread-safe queues
* thread-safe logging
* explicitly provided job context

## Forbidden

Worker jobs must not:

* mutate live chunks
* mutate live entities
* mutate world tick queues
* mutate registries
* access OpenGL
* access renderer-owned GPU resources
* store raw pointers to temporary owner data
* call back into arbitrary systems
* block waiting for the simulation thread while holding locks
* perform hidden global state mutation

---

## Locking Policy

Prefer ownership transfer and immutable snapshots over locks.

Locks are allowed for:

* short-lived queue operations
* logging
* cache lookup with clear ownership
* low-contention shared services

Locks should not be used as the default solution for world access.

Avoid:

* long-held locks around chunk data
* nested locks across systems
* worker jobs blocking on simulation-owned resources
* simulation thread blocking on worker completion during normal ticks
* render thread waiting on world locks

The engine should be designed so that most expensive work does not require holding a lock.

---

## Atomics Policy

Atomics may be used for:

* job counters
* cancellation flags
* simple state flags
* generation counters
* queue internals
* statistics

Atomics should not be used to make complex world mutation “sort of thread-safe.”

If a system requires multiple related fields to update together, use ownership transfer, command queues, or a dedicated synchronized structure.

---

## Cancellation

Jobs should support logical cancellation where useful.

Cancellation may be represented by:

* version mismatch
* chunk unload
* world shutdown
* explicit cancellation flag
* job generation counter

Jobs are not required to stop immediately.

It is acceptable for a job to finish and have its result discarded.

Cancellation must be safe and must not leave partially applied state.

---

## Command Queues

When worker jobs need to request world changes, they must emit commands.

Example commands:

* place generated structure
* request neighbor relight
* request mesh rebuild
* spawn entity from generated feature
* enqueue save write
* request asset upload

Commands are validated by the owning system before execution.

Workers do not directly execute world mutations.

---

## Simulation Integration

At the start of a tick, the simulation thread may drain completed job queues.

Typical order:

1. Apply completed chunk loads.
2. Apply completed chunk generation.
3. Apply completed lighting results.
4. Apply completed simulation-safe commands.
5. Mark affected chunks dirty.
6. Queue required mesh rebuilds.
7. Continue normal simulation.

This keeps mutation points predictable.

---

## Render Integration

Render integration should use explicit render commands or upload queues.

Simulation may submit:

* chunk mesh publish commands
* chunk mesh remove commands
* entity render state snapshots
* debug draw commands
* camera state
* lighting/render environment data

The render thread applies GPU-side changes.

GPU uploads should not occur from simulation or worker threads unless the graphics backend explicitly supports it and the engine design has been updated to allow it.

For the OpenGL backend, GPU operations are render-thread-only.

---

## Frame/Tick Decoupling

Simulation and rendering may run at different rates.

Simulation:

* fixed tick
* authoritative state
* world mutation
* deterministic-enough gameplay order

Rendering:

* variable frame rate
* interpolated state
* GPU resource ownership
* presentation

Data passed from simulation to rendering should be snapshot-based or command-based.

Rendering must not read live mutable simulation state directly.

---

## Registry Access

Registries are reconstructed at startup and treated as stable during normal gameplay.

Worker jobs may read registry data only if it is immutable for the duration of the job.

Runtime IDs may be used inside loaded runtime state, but jobs that serialize or persist data must follow save-format rules.

Registries must not be mutated while worker jobs are reading them.

If hot reload or dynamic registry changes are introduced later, registry versioning and job invalidation rules must be expanded.

---

## Error Handling

Job failures must be reported through structured results.

A failed job should return:

* job type
* target world/chunk/asset identifier
* error code or error object
* optional diagnostic message

Worker jobs should not crash the engine for recoverable failures.

Examples of recoverable failures:

* missing asset
* failed region read
* corrupted chunk data
* canceled job
* stale result
* invalid decoded data

Fatal errors should be reserved for violated invariants or unrecoverable engine state.

---

## Debugging Requirements

The job system should expose enough information to debug scheduling issues.

Useful debug data:

* queued job count by type
* running job count by type
* completed job count by type
* discarded stale result count
* average job duration
* worst job duration
* queue latency
* worker utilization
* chunk rebuild frequency

Debug views should help identify:

* job starvation
* excessive mesh rebuilds
* lighting storms
* save/load bottlenecks
* render upload stalls
* simulation thread blocking

---

## Forbidden Patterns

The following patterns are forbidden:

```cpp
// Forbidden: worker directly mutates world chunk data.
worker_pool.enqueue([&world, chunk_pos] {
    world.get_chunk(chunk_pos).set_block(...);
});
```

```cpp
// Forbidden: worker uploads GPU resources.
worker_pool.enqueue([mesh] {
    glBufferData(...);
});
```

```cpp
// Forbidden: worker reads live mutable chunk storage.
worker_pool.enqueue([chunk_ptr] {
    build_mesh_from_live_chunk(*chunk_ptr);
});
```

```cpp
// Forbidden: render thread reads live simulation entities directly.
for (Entity e : world.entities()) {
    renderer.draw(world.transform(e));
}
```

```cpp
// Forbidden: long world lock around expensive meshing.
std::lock_guard lock(world_mutex);
build_chunk_mesh(chunk);
```

Correct pattern:

```cpp
// Simulation thread:
ChunkMeshSnapshot snapshot = world.create_mesh_snapshot(chunk_pos);
jobs.submit(MeshChunkJob{snapshot});

// Worker thread:
ChunkMeshResult result = build_chunk_mesh(snapshot);
completed_meshes.push(std::move(result));

// Simulation/render owner:
if (world.accepts_mesh_result(result.chunk_pos, result.source_version)) {
    render_upload_queue.push(std::move(result.mesh_data));
}
```

---

## Invariants

These invariants must hold:

* live world mutation is owned by the simulation thread
* GPU mutation is owned by the render thread
* workers operate on immutable inputs
* worker outputs are validated before use
* stale results are safely discardable
* chunk jobs carry version metadata
* render code does not read live simulation state directly
* simulation code does not perform OpenGL calls
* locks are not used as a substitute for ownership design

---

## Summary

Arvane’s multithreading model is based on ownership, snapshots, jobs, and validated handoff.

Workers compute.

Owners mutate.

The simulation thread owns world truth.

The render thread owns GPU truth.

All cross-thread communication must be explicit.

---
