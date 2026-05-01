# Lighting

## Purpose

This document defines the lighting model for Arvane.

Lighting in a voxel engine is easy to underspecify and very easy to implement incorrectly. This file exists to make the following behavior explicit:

- what light data is stored per block
- how sky light differs from block light
- how propagation works
- what triggers relighting
- how lighting crosses chunk and section boundaries
- which parts of the pipeline may run off-thread
- how lighting results are published safely

This document defines the authoritative lighting rules used by world simulation, meshing, and rendering.

---

## Goals

The lighting system must be:

- correct across chunk boundaries
- stable under incremental world edits
- compatible with threaded world pipelines
- compact in memory
- predictable for meshing and shading
- capable of supporting colored block light

The lighting system does **not** attempt to simulate physically correct global illumination.

Arvane uses a voxel-style flood fill lighting model.

---

## Lighting Model Overview

Arvane stores two categories of light:

- **sky light**
- **block light**

### Sky Light

Sky light represents directional light from the sky.

Sky light is:

- strongest in open air exposed to the sky
- reduced when passing through light-blocking blocks
- propagated downward and sideways through the world
- stored as a single 4-bit channel

Sky light is not colored.

### Block Light

Block light represents light emitted by blocks such as:

- torches
- lamps
- glowing blocks
- other emissive voxel content

Block light is:

- emitted locally from light-emitting blocks
- propagated outward through the world
- attenuated by distance and blockers
- stored as three separate 4-bit color channels:
  - red
  - green
  - blue

Block light is colored.

---

## Storage Format

Each voxel stores lighting in **2 bytes**.

Bit layout:

- 4 bits red
- 4 bits green
- 4 bits blue
- 4 bits sky

This yields:

- `R`: 0..15
- `G`: 0..15
- `B`: 0..15
- `Sky`: 0..15

A canonical packed representation is:

```cpp
using LightWord = u16;
```

Recommended bit layout:

* bits 0..3: red
* bits 4..7: green
* bits 8..11: blue
* bits 12..15: sky

Equivalent helpers:

```cpp
inline u8 light_r(LightWord v)   { return (v >> 0)  & 0xF; }
inline u8 light_g(LightWord v)   { return (v >> 4)  & 0xF; }
inline u8 light_b(LightWord v)   { return (v >> 8)  & 0xF; }
inline u8 light_sky(LightWord v) { return (v >> 12) & 0xF; }
```

And packing:

```cpp
inline LightWord pack_light(u8 r, u8 g, u8 b, u8 sky) {
    return ((r & 0xF) << 0) |
           ((g & 0xF) << 4) |
           ((b & 0xF) << 8) |
           ((sky & 0xF) << 12);
}
```

### Invariants

Lighting values must always satisfy:

* each channel is clamped to `0..15`
* sky light and block light are stored together in the same `u16`
* sky light is independent from RGB block light
* air does not imply nonzero light
* opaque blocks may still store a light value temporarily during propagation work, but finalized world state should reflect the solved result at that voxel

---

## Ownership and Placement

Lighting data is world data.

Each chunk section stores a light array aligned with its block storage.

A section therefore owns:

* block states
* lighting values
* optional derived metadata related to lighting dirtiness

Lighting is not renderer-owned and is not mesher-owned.

The renderer and mesher only consume lighting snapshots or published lighting state.

---

## Channel Semantics

## Sky Light Semantics

Sky light is a scalar channel representing ambient sky contribution.

Rules:

* maximum sky light is `15`
* a voxel fully exposed to the sky may receive sky light `15`
* downward propagation from open sky preserves high sky values until blocked
* lateral propagation attenuates like normal flood-fill light unless explicitly using a special vertical skylight optimization
* opaque blocks stop sky propagation
* transparent or non-solid blocks may allow propagation depending on block light opacity rules

Sky light is conceptually separate from RGB block light even though it is packed into the same word.

## Block Light Semantics

Each emitting block may specify an RGB emission value:

* `emit_r`
* `emit_g`
* `emit_b`

Each emission channel is in `0..15`.

Propagation for block light is performed independently per color channel.

This means:

* red propagates independently of green and blue
* final block light at a voxel is the per-channel max of all contributing emitters
* sky light never mixes into RGB channels

Example:

* a red lamp might emit `(15, 0, 0)`
* a warm lantern might emit `(15, 10, 6)`
* a blue crystal might emit `(0, 4, 15)`

---

## Block Lighting Properties

Each block definition must expose lighting-relevant properties.

At minimum:

* whether the block is light blocking
* how much light attenuation it causes
* whether sky light may pass through it
* whether block light may pass through it
* whether the block emits light
* emitted RGB light value, if any

Recommended logical fields:

* `opaque`
* `light_opacity`
* `allows_sky_light`
* `allows_block_light`
* `emission_rgb`

### Light Opacity

For the initial implementation, prefer simple voxel lighting rules:

* fully opaque blocks block all propagation
* non-opaque blocks allow propagation with attenuation of 1 per step
* emissive blocks still obey their own emission rules
* air fully allows propagation

If more advanced block types are added later, light opacity can be generalized, but the baseline implementation should remain simple and predictable.

---

## Neighbor Topology

Lighting propagation uses the six cardinal voxel neighbors:

* +X
* -X
* +Y
* -Y
* +Z
* -Z

Diagonal propagation is not used.

All flood-fill and invalidation logic operates on 6-connected voxel neighborhoods.

---

## Propagation Rules

## General Rule

For any propagated light channel:

* light loses strength as it travels
* a voxel's propagated output to a neighbor is generally `current - attenuation`
* attenuation is usually `1`
* propagation stops when the next value would be `<= 0`
* opaque/light-blocking blocks stop propagation entirely

For the baseline implementation, use attenuation of `1` per voxel step.

## Block Light Propagation

For each RGB channel:

* emitter blocks seed the queue with their emission values
* light spreads to neighboring voxels
* neighbor receives `max(existing, propagated)`
* propagation only continues when the new value is greater than the neighbor's previous value

This is a max-propagation flood fill.

For a channel `C`:

```text
propagated = source_C - 1
if propagated > dest_C and path is open:
    dest_C = propagated
    enqueue(dest)
```

This applies independently to red, green, and blue.

## Sky Light Propagation

Sky light follows similar rules but has separate initialization.

At minimum, sky light must support:

* top-down seeding from sky-exposed voxels
* continued propagation across chunk/section boundaries
* recomputation when terrain changes affect sky exposure

Baseline rule set:

* a sky-exposed voxel is seeded with sky light `15`
* sky light propagates downward and sideways
* each non-special propagation step attenuates by `1`
* opaque blocks stop propagation

An implementation may optimize vertical sunlight columns, but the externally visible result must still match the rules defined here.

---

## Lighting Solve Strategy

Lighting is solved as **world simulation work**, not render work.

Lighting updates are processed asynchronously, but publication into the live world must be synchronized by world ownership rules.

### Authoritative Rule

* the world owns the canonical light state
* background workers may compute lighting updates from immutable job inputs
* workers must not mutate live chunk storage directly
* lighting results must be published back through world-owned commit logic

### Recommended Model

Use queued lighting jobs:

1. world edit occurs
2. affected region is marked lighting-dirty
3. a lighting task is queued
4. worker computes updated lighting data for the affected region using snapshots or guarded access
5. world thread validates applicability
6. world publishes light updates
7. affected chunk meshes are marked dirty

This matches the same general safety model used for meshing.

### Current Section Job Model

Arvane lighting jobs are bounded to one dirty 16 x 16 x 16 section at a time.

Each job receives immutable storage handles for:

* the target section
* the six direct face-neighbor sections that are currently loaded

The target section is rebuilt from scratch inside a job-local dense light buffer. Neighbor sections are read only as boundary seeds:

* sky light from the section above can seed vertical columns without attenuation
* if the section above is missing, open-sky seeding is allowed only from explicit per-column snapshot metadata
* side and lower sky boundaries seed with normal one-step attenuation
* RGB block light boundaries seed with one-step attenuation
* local emissive blocks seed the add queue directly

Dirty neighbor sections are not valid boundary seeds. Their light buffers may contain pre-repair values from an older lighting state, so the worker does not import arbitrary dirty-neighbor light. If the target section already had light on the shared boundary, the worker may preserve that target-owned boundary light as a temporary seed while the neighbor relights. This prevents an intermediate one-section solve from publishing an all-dark section just because a required boundary is still in flight; later boundary continuation still replaces the preserved value with authoritative light.

For generated worlds, the server installs the world seed and generation config into the world runtime so snapshots can classify missing-above columns against terrain height. The snapshot also carries loaded column context from the target section up toward the generated surface section. A worker can therefore seed skylight by checking the actual loaded blocks from the surface down to the target top, instead of trusting stale light values from the immediate upper section. If the target section top is still below generated terrain and the surface-to-target path is blocked or unavailable, digging inside that section must not fabricate skylight just because the section above is not currently loaded. If no generation context exists, the fallback open-sky rule applies only to nonnegative vertical sections with no loaded section above.

Dirty lighting sections in a column are selected top-down. Surface or upper-section skylight repairs should publish before lower continuation jobs consume that boundary, which keeps block breaks from flipping lower caves through stale upper light data.

Workers publish at most the target section's light array. They do not mutate neighboring sections and they do not complete cross-section propagation inline. If a published result changes a boundary face, the world thread marks that face neighbor for lighting continuation and `LightMesh` remeshing. Missing neighbors are recorded as boundary deferrals; when those sections later load, normal load-neighbor dirtying repairs the boundary.

This model intentionally turns large relights into multiple small jobs. The soft work budget is tracked per job; a section job that exceeds the configured node budget is suspicious and visible in profiling, while remaining dirty sections or changed boundaries are handled as continuation jobs.

---

## Asynchronous-Only Behavior

Lighting behavior must distinguish between:

* **logical immediacy**
* **execution location**

### Logical Immediacy

Lighting changes must be treated as part of authoritative world state updates.

For example, if a player places or removes a light-blocking block, the world must eventually converge to the correct lighting state and all dependent chunks must be invalidated.

### Execution Location

Lighting propagation must be queued as asynchronous world/server work.

Block placement, block breaking, chunk streaming, rendering, and client code must not run authoritative lighting propagation inline.

Small local edits may produce small section-local jobs, but they are still jobs. Synchronous lighting helpers are allowed only as worker-side routines or test helpers around worker inputs/results; public world/client-facing APIs enqueue or publish work rather than solving it immediately.

### Required External Contract

Regardless of execution strategy:

* lighting results must be version-checked before publish
* stale lighting jobs must be discarded
* chunk mesh rebuilds may consume stale published lighting data temporarily
* border propagation must not be silently dropped

Meshing does not wait for lighting. A block edit queues geometry remeshing immediately, queues lighting separately, and later lighting publication queues `LightMesh` remeshing.

---

## Dirty Region Handling

Lighting should not require full-world, full-region, or whole-column recomputation after every edit.

Instead, world changes mark bounded lighting-dirty chunk sections and any necessary boundary continuation work.

A dirty lighting region represents a voxel-space area whose solved light values may have become invalid.

### Events That Mark Lighting Dirty

Lighting dirtiness must be triggered by changes that affect:

* opacity
* sky exposure
* light emission
* chunk existence at relevant borders

Examples:

* block placed
* block removed
* block state changed from transparent to opaque
* block state changed from opaque to transparent
* emissive block added
* emissive block removed
* chunk loaded with previously unavailable neighbors
* chunk unloaded if neighboring lighting depends on it
* world generation populates a new section
* bulk edits such as fill, paste, or structure placement

### Dirty Region Size

The initial dirty region should cover at least:

* the changed voxel
* its immediate neighbors
* enough boundary margin to allow queue-based propagation to expand naturally

In practice, the relight job may begin from a seed set and expand until convergence instead of relying on a fixed-radius update.

### Mesh Invalidation

Whenever lighting changes are published, all touched chunk sections must increment `light_version` and be marked `LightMesh` dirty.

A mesh must be rebuilt if any voxel affecting its visible faces had light changes.

Lighting publication must only enqueue remesh work. It must not rebuild meshes inline and must not wait for continuation lighting jobs to drain.

---

## Light Removal and Rebuild Strategy

Lighting systems must handle both:

* light addition
* light removal

Addition is easy: flood-fill outward from stronger sources.

Removal is harder: previously lit voxels may need to fall back to weaker contributions from other sources.

A correct implementation must therefore support recomputation, not just additive fill.

### Acceptable Strategies

Either of the following is acceptable:

#### 1. Two-phase removal/addition

* identify voxels invalidated by removed light
* clear dependent propagated values
* re-seed from remaining valid sources
* flood-fill outward again

#### 2. Region rebuild

* identify a bounded affected region
* clear lighting in that region
* re-seed from region borders, sky seeds, and emitters
* solve the region from scratch

For simplicity and correctness, a bounded region rebuild is often preferable in early engine versions.

### Required Property

After any edit, the solved light field must converge to the same result regardless of edit order history.

In other words, lighting must not accumulate permanent stale values.

---

## Chunk and Section Boundary Rules

Lighting must propagate across chunk and section boundaries.

This is not optional.

A chunk's final light state depends on neighboring chunks when:

* sky light enters from adjacent columns
* block light sources are near borders
* terrain changes open or close paths across chunk edges

### Boundary Contract

When solving lighting for a region, the solver must have access to relevant neighbor voxel lighting/block data at boundaries.

At minimum, lighting jobs must operate with one of these models:

* direct guarded reads from neighboring loaded chunks
* immutable boundary snapshots captured before the job starts
* a larger multi-section job input that includes border halos

### Required Safety Rule

A worker must never assume missing neighbor data means permanent darkness unless the world explicitly defines that neighbor as absent and non-contributing for the current solve.

### Border Publication

If a lighting update changes border voxels, neighboring sections/chunks must also be considered for:

* lighting continuation
* mesh invalidation
* further queued propagation work

This means a lighting job may publish results that enqueue follow-up work in adjacent chunks.

---

## Cross-Chunk Propagation Policy

The engine must not confine lighting updates to a single chunk if the correct solution crosses chunk boundaries.

The correct policy is:

* lighting updates may start from a local edit
* propagation may continue into neighboring chunks
* crossing chunk boundaries happens through queued work or expanded job coverage
* the engine must converge to the correct multi-chunk result

### Immediate vs Queued Border Crossing

Chunk boundary propagation does **not** need to be completed as a single monolithic synchronous step.

However, it must satisfy:

* boundary changes are detected immediately when publishing results
* adjacent chunks are queued for lighting work without being forgotten
* no chunk border may remain permanently inconsistent because propagation was deferred

So the required contract is:

* border propagation may be queued
* border propagation must be reliable
* stale or missing neighbor updates are not allowed to persist

In the current implementation, a changed boundary face queues a face-neighbor section continuation. If the neighbor is unloaded, the event is counted as a boundary deferral rather than forcing a synchronous load.

---

## Chunk Load and Unload Interaction

Chunk lifetime affects lighting.

### On Chunk Load

When a chunk or section loads, lighting may need to be solved or repaired using:

* local block data
* neighboring chunk data
* sky access above
* nearby emissive blocks across boundaries

A newly loaded chunk must not assume its stored lighting is correct until validated against current neighbors.

Depending on persistence strategy, load behavior may be:

* trust serialized lighting and validate borders
* rebuild lighting entirely
* rebuild only if neighboring topology changed or stored version is incompatible

### On Chunk Unload

Chunk unload should not corrupt remaining loaded chunks.

If unloading creates a missing-neighbor condition, the world must define how lighting treats unloaded space.

Recommended rule:

* unloaded chunks are not simulated
* lighting at loaded chunk borders is only guaranteed correct relative to currently loaded authoritative neighborhood
* when neighbors load, border lighting may be repaired
* skylight from a missing `+Y` neighbor may be seeded only when the lighting snapshot marks that x/z column as sky-exposed
* for generated terrain, loaded surface-to-target column sections may be used as read-only context to prove that sunlight can reach a lower section

For active simulation/view regions, chunks relevant to lighting propagation should generally remain loaded long enough to avoid visible inconsistencies.

In generated worlds, this sky-exposure metadata is derived from the configured terrain height and the loaded column blocks above the target. A lower, dug-out section beneath generated terrain remains dark if the surface path is blocked, and becomes sunlit when a loaded shaft is actually open from the surface section down.

---

## World Generation and Initial Solve

World generation and lighting are separate phases.

Generation produces block contents.
Lighting solves illumination from those contents.

Recommended pipeline:

1. generate chunk blocks
2. ensure required neighbors or border snapshots exist
3. publish chunk blocks
4. queue initial section meshing using current, preserved, or conservative placeholder light data
5. queue initial lighting solve asynchronously
6. publish finalized lighting
7. queue light-only remeshing for affected sections

A chunk section may be meshed before its initial lighting solve has completed. Stale or placeholder lighting is temporarily acceptable; stale geometry is not. When a streamed chunk update is sent while authoritative lighting is still dirty, the server should include the previous light buffer as stale lighting where possible. Clients may mesh from that stale buffer and must treat it as non-final until a later valid-light payload arrives. They should not replace missing lighting with full skylight.

---

## Interaction With Meshing

Meshing consumes the best currently published light data.

The mesher must treat lighting as input, not recompute it.

Meshing may use light data for:

* face lighting values
* vertex interpolation / corner selection
* ambient shading combination
* future shader inputs

### Meshing Contract

A meshing job must read:

* block states
* neighbor block states
* lighting data
* neighbor boundary lighting data if needed for faces on chunk borders

The mesher must not mutate lighting.

If lighting changes after a mesh was built, the chunk section must be marked `LightMesh` dirty and re-meshed asynchronously.

---

## Interaction With Rendering

Rendering consumes lighting-derived mesh data or direct light values, depending on mesh format.

The renderer must not own the lighting simulation.

Lighting in rendering is purely a presentation of world state.

The shader-visible result may include:

* sky channel contribution
* RGB emissive/block light contribution
* ambient occlusion interaction
* day/night modulation applied to sky light
* material response rules

But these are shading concerns. The authoritative stored light field is defined here.

---

## Day/Night Cycle

A day/night system may modulate the visual intensity of sky light at render time.

Important:

* stored sky light values represent geometric sky exposure and propagation, not the current time-of-day brightness
* time-of-day should usually be applied as a global shading multiplier on sky light contribution
* block light RGB should generally remain unaffected by day/night intensity

This keeps the lighting field stable while allowing dynamic time-of-day rendering.

---

## Off-Thread Safety Rules

Lighting workers may run off-thread only under strict rules.

Workers may:

* read immutable snapshots
* read validated neighbor boundary copies
* compute relight outputs for a bounded region
* produce publishable results

Workers may not:

* mutate live world chunk storage directly
* read world state through unsafe dangling pointers
* publish without version validation
* assume neighbor data stayed unchanged during long jobs

### Versioning

Lighting jobs should carry enough version information to detect staleness.

At minimum include:

* target chunk/section coordinates
* source block/geometry version or generation counter for input data
* possibly neighbor version info if boundary-sensitive

On publish:

* if relevant source data changed since the job began, discard or recompute
* never publish stale lighting over newer world edits

---

## Recommended Internal Data Structures

Exact containers are implementation details, but the engine should conceptually support:

* per-section `LightWord` arrays
* `Lighting` dirty flags
* `LightMesh` dirty flags
* section lighting queues
* per-section `block_version` and `light_version`
* worker job payloads containing:

  * target region
  * block snapshots or references under safe ownership
  * neighbor boundary data
  * source versions

Possible queue categories:

* initial chunk lighting
* incremental local relight
* border continuation relight
* large-area rebuild relight

---

## Debugging Expectations

Lighting bugs are difficult to diagnose without visibility tools.

The engine should support debug views for at least:

* sky light channel
* red channel
* green channel
* blue channel
* combined light visualization
* chunk border lighting state
* lighting dirty regions / queued jobs

These tools are strongly recommended during development.

The profiling/debug UI must expose lighting-specific counters:

* jobs submitted, completed, and continued/rescheduled
* total processed nodes, add nodes, and remove nodes
* boundary deferrals
* sections touched and sections published
* lighting-triggered remeshes
* max queue depth
* p50/p95/p99 lighting job time
* worst lighting event context with section coordinate, job type, nodes processed, continuation count, and neighbor-touch state

Lighting jobs above the soft budget, repeated section continuations, persistent missing-neighbor deferrals, or a steadily growing lighting queue should be treated as suspicious profiler signals.

---

## Baseline Implementation Guidance

For the first stable implementation, prefer the following:

* 6-neighbor flood fill
* per-voxel 2-byte packed light storage
* sky light as a 4-bit scalar
* RGB block light as 3 independent 4-bit channels
* simple opaque vs non-opaque propagation rules
* bounded section-local relight jobs with boundary continuation
* reliable queued cross-chunk propagation
* version-validated asynchronous publish
* `LightMesh` invalidation on lighting publish

Avoid prematurely adding:

* diagonal propagation
* semi-transparent attenuation tables
* colored sky light
* physically based global illumination
* overly clever but fragile incremental optimizations

Correctness and consistency are more important than sophistication in the first version.

---

## Required Engine Invariants

The engine must uphold all of the following:

* lighting is authoritative world state
* each voxel stores exactly 16 bits of light data
* sky light is separate from RGB block light
* lighting propagation crosses chunk boundaries
* block edits can trigger relight
* relight results are computed asynchronously
* relight publication must be synchronized and version-checked
* meshing consumes current published light data and may temporarily bake stale lighting
* meshing never waits for lighting propagation
* stale lighting jobs must not overwrite newer world state
* deferred border propagation must still converge to the correct result
* clients do not generate authoritative light updates

---

## Summary

Arvane uses a compact voxel flood-fill lighting model with:

* `4-bit R`
* `4-bit G`
* `4-bit B`
* `4-bit Sky`

Sky light and block light are separate channels packed into one `u16`.

Lighting belongs to world simulation, not rendering.

Lighting updates are processed asynchronously, but canonical light state is published only through world-owned validated commit steps.

Lighting must propagate across chunk boundaries, must handle both light addition and removal, and must invalidate chunk meshes when finalized values change.

This document defines the authoritative storage and propagation contract for the engine's lighting system.

---
