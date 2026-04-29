# Chunk Meshing

## Purpose

This document defines exactly how chunk meshing works in Arvane.

The meshing system converts chunk block/state/light data into renderable mesh buffers. It exists to produce stable, efficient chunk geometry without coupling worker threads directly to live world state.

This document standardizes:

- opaque vs transparent mesh separation
- face visibility and culling rules
- neighbor sampling behavior
- ambient occlusion rules
- rebuild triggers and invalidation
- meshing thread model
- GPU upload handoff

This file defines the baseline chunk meshing model. Any implementation must follow these rules unless this document is revised.

---

## Goals

The meshing system must:

- produce correct visible surfaces for chunk contents
- support chunk-local rebuilds with neighbor-aware face decisions
- operate safely off-thread using immutable inputs
- separate opaque and transparent rendering paths
- avoid dependence on live mutable world state during worker execution
- allow lighting and per-vertex shading data to be baked into mesh output
- support partial world churn without requiring global remeshes

The meshing system is not responsible for:

- world mutation
- live block queries against the world from worker threads
- lighting simulation itself
- render ordering policy beyond mesh categorization
- gameplay collision generation

---

## Meshing Overview

Each meshed chunk is built from a snapshot of chunk data and immediate neighbor boundary data.

At a high level:

1. world marks a chunk dirty for meshing
2. world prepares a meshing input snapshot
3. worker thread builds mesh buffers from the snapshot
4. worker returns mesh result with source block and light versions
5. main/world thread validates the result
6. validated mesh is uploaded or handed off to renderer
7. chunk mesh state becomes clean

Meshing is snapshot-driven. Worker threads must not read live chunk state directly.

---

## Chunk Meshing Scope

Meshing is performed per chunk section.

For Arvane, the storage and simulation unit is a 16 x 16 x 16 chunk section, so the meshing unit is also one chunk section. A block edit must not remesh a whole column, whole chunk region, or arbitrary multi-section area.

The meshing unit must have access to:

- its own full block/state data
- enough neighbor data to determine boundary face visibility
- enough lighting data to shade produced vertices correctly

Meshing must not wait for lighting propagation to finish. A section should remesh immediately from the best currently available light data. If that light data is stale, the mesh may be shown temporarily and replaced by a light-only remesh after authoritative lighting updates publish.

---

## Mesh Categories

Chunk meshing produces two logically separate mesh outputs:

1. opaque mesh
2. transparent mesh

These are built from the same block snapshot but emitted into different output buffers.

### Opaque Mesh

Opaque mesh contains faces belonging to blocks whose rendered surfaces are fully opaque and should participate in the opaque render pass.

Typical examples:

- stone
- dirt
- planks
- fully opaque decorative solids

Opaque geometry should be written only to the opaque mesh output.

### Transparent Mesh

Transparent mesh contains faces belonging to blocks that require the transparent render path.

Typical examples:

- water
- glass
- ice-like materials
- translucent decorative blocks

Transparent geometry must never be mixed into the opaque mesh.

### Reason for Split

Opaque and transparent geometry have different render behavior and usually different state requirements.

The split exists so that:

- opaque rendering can use normal depth-write behavior
- transparent rendering can use the engine’s chosen transparency path
- render sorting/state changes stay predictable
- rebuild logic can keep a single authoritative meshing pass while still producing two outputs

---

## Mesh Categories Are Material-Driven

Whether a block face goes to the opaque or transparent mesh is determined by the block state’s render metadata, not by ad hoc checks in the mesher.

Each block state used for meshing must expose enough metadata to answer:

- does this state render any geometry at all
- which render category does its geometry belong to
- which faces can exist for this state
- whether the state fully occludes adjacent faces
- whether the state participates in ambient occlusion as a solid occluder
- whether the state uses cube-face geometry or custom face geometry

The mesher consumes this metadata. It does not hardcode special-case behavior for every block.

---

## Meshing Inputs

Meshing workers operate on immutable inputs.

### Required Inputs

A meshing job must include:

- chunk coordinate
- source block/geometry version
- source light version
- block/state data for the target chunk
- neighbor boundary data
- lighting data required for vertex shading
- any block render metadata needed during mesh generation

### Chunk Block Data

The target chunk snapshot must include all block/state values for the chunk being meshed.

This data must be immutable for the lifetime of the job.

### Neighbor Boundary Data

Neighbor sampling must not read live neighbor chunks on worker threads.

The job input must include enough boundary information from adjacent chunks to evaluate:

- whether a boundary face is visible
- AO samples that reach across the chunk border
- lighting samples at chunk edges
- custom block geometry decisions that depend on adjacent blocks

At minimum, this means the meshing input must provide the six direct neighboring boundaries:

- negative X
- positive X
- negative Y
- positive Y
- negative Z
- positive Z

If AO or lighting rules require corner access beyond a single direct boundary, the prepared input must include whatever edge/corner data is necessary so workers never fall back to live world queries.

### Lighting Data

Lighting included in meshing input must be sufficient to generate final per-vertex light values for every emitted face.

If lighting is separated into channels, such as:

- sky light
- block light

then both channels required by the vertex format must be present in the snapshot.

### Render Metadata Access

Workers must be able to resolve block state render properties without depending on mutable gameplay registries.

This can be achieved by:

- immutable registry data shared safely across threads
- cached meshing descriptors indexed by state ID
- equivalent read-only lookup structures

---

## Meshing Outputs

Worker output must contain:

- chunk coordinate
- source block/geometry version
- source light version
- opaque mesh buffers
- transparent mesh buffers
- any bounds or counts needed by the renderer
- optional mesh statistics for debugging

### Opaque Output

Opaque output contains all opaque vertices/indices generated for the chunk.

### Transparent Output

Transparent output contains all transparent vertices/indices generated for the chunk.

### Empty Meshes

A chunk may validly produce:

- only opaque geometry
- only transparent geometry
- both
- neither

Empty outputs are valid and must be handled without special-case failure.

---

## Vertex Format Requirements

The exact vertex layout is renderer-defined, but the mesher must emit enough information to support stable chunk rendering.

A chunk mesh vertex should generally include:

- local or world-relative position
- face or normal encoding
- texture/material selection data
- per-vertex light values
- ambient occlusion term if used
- any additional packed data required by the renderer

The meshing system should favor compact packed formats where reasonable, but correctness and consistency come first.

The document does not mandate a specific bit layout, only the required semantic content.

---

## Coordinate Convention for Meshing

Meshing uses block-local coordinates within the target chunk plus chunk coordinate context.

Face generation must be deterministic and aligned to the engine-wide coordinate conventions defined elsewhere.

All emitted geometry must be based on canonical block bounds. For standard cube blocks, the canonical bounds are the unit block volume anchored at the block cell coordinate.

Custom block geometry must still obey the same local chunk coordinate space.

---

## Meshing Inputs by Block State

Each block state must expose meshing-relevant properties in a form the mesher can consume quickly.

Examples of required properties:

- render category: none, opaque, transparent
- geometric class: full cube, partial cube, custom model, fluid surface, etc.
- occlusion class
- AO participation
- per-face texture/material mapping
- whether a given face exists for the state
- whether a given face can cull neighboring faces
- whether same-state adjacency suppresses internal faces

These properties should come from registry-authored state definitions, then resolved into efficient runtime meshing descriptors.

---

## Face Generation Rules

Face generation is driven by block-state descriptors and neighbor-state descriptors.

For each block cell in the target chunk:

1. resolve current block state descriptor
2. skip if the state renders no geometry
3. evaluate each candidate face for that state
4. determine whether the face is visible
5. if visible, emit vertices/indices into the correct mesh category

---

## Standard Cube Face Rules

For a standard cube-like state, the mesher evaluates the six axis-aligned faces:

- -X
- +X
- -Y
- +Y
- -Z
- +Z

A face is emitted only if it is visible according to the visibility rules below.

---

## Face Visibility Rules

A face is visible when the current block state says that face exists and the adjacent block state does not fully hide it.

Visibility is decided per face, not per block.

### Face Must Exist on the Current State

Some states do not expose all six faces.

Examples:

- slabs
- stairs
- panes
- custom models
- fluids with special top surfaces

If the current state does not define a given face as renderable, that face must not be emitted.

### Adjacent State Can Hide the Face

The current face is culled if the adjacent state fully occludes that exact face according to the occlusion rules.

This decision must use block-state occlusion metadata, not just “is neighbor non-air”.

### Same-Block Internal Face Rules

Some adjacent block pairs should suppress internal faces even when they are not fully opaque cubes.

Examples include:

- two touching water cells
- two touching glass states if the chosen visual policy hides internal seams
- connected special-case materials that should appear as a continuous volume

This behavior must be descriptor-driven.

The mesher must not assume that all transparent blocks render internal touching faces.

---

## Occlusion Model

Visibility decisions use an explicit occlusion model.

A block state may be:

- non-occluding
- partially occluding
- face-occluding on selected faces
- fully face-occluding like a solid full cube

The mesher must query whether the adjacent state occludes the current face from the opposite side.

Example:

- when evaluating current block’s +X face, the neighbor at +X is queried for whether its -X side occludes that boundary

This is important for non-cube and directional blocks.

---

## Full-Cube Fast Path

For performance, implementations may use a fast path for common full-cube opaque blocks.

Example policy:

- if current state is a full cube with standard cube geometry
- and neighbor state fully occludes the opposing face
- then cull immediately

This is an optimization only. It must produce identical results to the general occlusion rules.

---

## Transparent Face Rules

Transparent blocks still use the same general visibility model, but with material-specific policies.

Examples:

- glass next to air should render exposed faces
- water next to air should render exposed surfaces
- water next to water usually should not render the shared face
- glass next to the same glass type may or may not hide internal faces depending on authored rules

These rules must come from per-state meshing descriptors, not renderer-side guesswork.

Transparent face generation still happens during chunk meshing, just into the transparent output buffer.

---

## Neighbor Sampling

Neighbor sampling exists to make correct boundary decisions without direct world access.

### Rule

Meshing workers must never query neighboring live chunks directly.

All neighbor information required for face culling, AO, and lighting must be supplied in the meshing snapshot.

### Required Coverage

The snapshot must support reading adjacent block/state data for any sample needed by:

- face visibility at chunk borders
- AO side/corner tests at chunk borders
- lighting interpolation at borders
- custom block face rules at borders

### Out-of-Bounds Sampling

When the mesher samples outside the current chunk bounds, it must read from the prepared neighbor boundary snapshot.

This must behave exactly as if the adjacent chunk data had been present locally.

### Missing Neighbor Data

If a required neighbor boundary is unavailable, the world must choose one of these policies before dispatching the job:

1. do not schedule meshing yet
2. schedule using a conservative fallback snapshot policy

The preferred policy is to avoid meshing until required boundary data is available, because fallback assumptions tend to cause visible popping and unnecessary rebuilds.

If fallback behavior is ever used, it must be consistent and documented engine-wide.

---

## Boundary Consistency Rule

Two adjacent chunks must make compatible visibility decisions across their shared boundary.

This means the meshing input system must ensure both chunks evaluate shared edges from authoritative, consistent state data.

Any system that allows one chunk to mesh against stale neighbor data must also ensure that stale results are invalidated and rebuilt promptly.

---

## Ambient Occlusion

Ambient occlusion is a meshing-time shading input, not a lighting simulation.

AO darkens vertices near blocked corners to improve visual depth on voxel surfaces.

This document assumes vertex AO for block faces.

---

## AO Participation

Only block states marked as AO occluders participate in AO blocking.

Typically:

- solid cube-like blocks participate
- air does not
- many transparent blocks do not
- special thin/custom geometry may opt out
- some partial solids may participate only where they truly occupy the sampled region, depending on implementation detail

AO participation must be data-driven.

The mesher must not assume that “visible block” means “AO blocker”.

---

## AO Sampling Rule

AO is computed per emitted face vertex.

For each vertex of a face, sample the three relevant neighboring cells around that vertex:

- side sample 1
- side sample 2
- corner sample

This is the standard voxel AO pattern.

### Standard Rule

Let:

- `s1` = first side occupied by an AO-blocking state
- `s2` = second side occupied by an AO-blocking state
- `c` = corner occupied by an AO-blocking state

Then:

- if both `s1` and `s2` are occupied, AO is maximum occlusion regardless of `c`
- otherwise AO is based on the count of occupied samples among `s1`, `s2`, and `c`

Equivalent implementations are acceptable so long as they produce the same visual result.

### Output Range

AO may be stored as:

- a small integer level
- a packed 0..3 value
- a normalized shading factor baked per vertex

The exact storage format is implementation-defined.

---

## AO Across Chunk Boundaries

AO sampling must work identically at chunk borders.

If an AO sample falls outside the chunk:

- read from the neighbor boundary snapshot
- do not treat out-of-bounds as empty unless that is actually the authoritative neighbor state

Incorrect border AO is a visible artifact and must be avoided.

---

## AO and Non-Cube Blocks

Non-cube blocks complicate AO.

To keep behavior predictable, the baseline policy is:

- only states explicitly marked as AO occluders contribute to AO
- if a non-cube state does not provide precise AO occupancy semantics, treat it conservatively according to its authored descriptor
- partial/custom shapes should not accidentally behave like full cubes unless explicitly authored that way

The first implementation should prioritize consistent authored rules over trying to infer geometric occupancy from arbitrary models.

---

## AO and Face Triangulation Consistency

When emitting quad faces as two triangles, implementations must choose triangle orientation consistently to avoid visible AO seams.

If per-vertex AO levels differ asymmetrically across the quad, the implementation should use a deterministic diagonal selection policy that minimizes shading artifacts.

This must be consistent for all chunk meshes.

---

## Lighting Sampling

Lighting is separate from AO.

The mesher must sample lighting data from the provided lighting snapshot and bake the needed light values into vertices.

Lighting policy may include:

- per-face light
- per-vertex light
- packed sky/block light
- smoothed corner sampling

The exact interpolation strategy belongs to the rendering/lighting design, but the meshing system must use only snapshot-provided lighting inputs and must not query live world state.

---

## Face Generation for Custom Shapes

Some block states are not simple cubes.

Examples:

- slabs
- stairs
- cross-plane foliage
- panes
- fluids
- authored custom geometry

The mesher must support state-driven face generation rules for these.

### Rule

Custom geometry generation must still obey the same broad pipeline:

- read state descriptor
- determine candidate surfaces for the state
- evaluate visibility rules against neighbors
- emit geometry to the proper mesh category
- compute AO/light where applicable

Custom-shape support should be implemented via per-state geometry builders or equivalent descriptor-driven logic, not giant block-type switch statements spread throughout the mesher.

---

## Internal Faces for Custom Shapes

Custom blocks may define special adjacency behavior.

Examples:

- connected panes may suppress overlapping internal geometry
- stairs may expose or hide certain sub-faces based on neighbor stair state
- fluid surfaces may merge visually with adjacent fluid cells

These rules belong in authored state meshing descriptors and specialized generators.

The mesher core should provide the framework; the state descriptor defines the specifics.

---

## Rebuild Triggers

A chunk mesh must be rebuilt whenever visual output may have changed.

At minimum, meshing rebuild triggers include:

- block/state change within the chunk
- lighting change affecting the chunk
- neighbor block/state change that alters boundary face visibility
- neighbor lighting change that alters boundary vertex lighting
- load/unload or availability change of a required neighbor boundary
- render-relevant metadata change during reload/dev workflows

### Block Change

If any block/state changes in a section, that section must be marked `Geometry` and a mesh rebuild should be queued immediately.

The same edit must separately mark `Lighting` if opacity, emission, sky exposure, or other lighting inputs may have changed. The geometry rebuild does not wait for the lighting job.

### Boundary Neighbor Change

If a block/state changes on a section boundary, neighboring sections sharing that boundary must also be marked `Geometry` if the change could affect:

- visible boundary faces
- AO samples
- vertex lighting at the border

This rule is mandatory. Border changes are not local to one chunk visually.

### Lighting Change

If lighting data used by the mesher changes for a section, that section must be marked `LightMesh`.

If the lighting change affects border samples used by a neighbor, that neighbor must also be marked `LightMesh`.

Received light snapshots are compared before invalidation. Interior light-only changes remesh only the owning section; boundary light-only changes remesh only face neighbors that sample the changed boundary light. They must not be promoted into geometry dirtiness.

---

## Mesh Invalidation

Mesh invalidation is the act of declaring an existing chunk mesh outdated.

A mesh becomes invalid when its source data no longer matches the chunk’s current meshing-relevant version.

### Causes of Invalidation

A chunk mesh is invalidated by:

- block/state edits
- lighting updates
- neighbor boundary changes
- chunk replacement or regeneration
- registry/dev hot reload that changes render descriptors

### Invalidation Behavior

Invalidation should not immediately destroy GPU buffers unless required by memory policy.

Instead, the chunk should move into a state such as:

- mesh dirty / stale
- rebuild pending
- upload pending for replacement result

This allows the renderer to keep displaying the old mesh until a new validated result is ready, if that is the chosen visual policy.

### Visual Policy

Preferred visual policy:

- stale mesh may remain visible until replacement mesh is published
- once replacement is validated and uploaded, swap atomically
- if no valid mesh exists, chunk simply renders nothing

This reduces visible flicker during rebuild churn.

---

## Dirty Flag Integration

Chunk dirty flags are the authoritative scheduling entry point.

Meshing integrates with the world dirty-flag model.

Example:

```cpp
enum class ChunkDirtyFlags : u32 {
  None             = 0,
  NeedsSave        = 1 << 0,
  Geometry         = 1 << 1,
  Lighting         = 1 << 2,
  LightMesh        = 1 << 3,
  NeedsFluidStep   = 1 << 4,
};
```

### Rule

Any block-shape, opacity, model, texture, fluid geometry, or boundary geometry change must set `Geometry`.

Any published light change affecting vertex lighting must set `LightMesh`.

`Lighting` is separate; it means authoritative lighting propagation is required, not that the mesh must wait.

The world update loop is responsible for:

* collecting sections with `Geometry` or `LightMesh`
* preparing valid meshing snapshots
* dispatching worker jobs
* receiving results
* validating block/geometry versions
* clearing `Geometry` and/or `LightMesh` only when the accepted mesh was built from the current corresponding versions

---

## Meshing Thread Model

Meshing is a background worker task driven by immutable snapshots.

### Worker Thread Rules

Worker meshing code:

* may read only immutable meshing input
* must not mutate world state
* must not query live chunk containers
* must not talk directly to rendering APIs
* must only produce CPU-side mesh output

### Main/World Thread Rules

The world or main thread is responsible for:

* deciding when to schedule a meshing job
* building the snapshot
* assigning source block and light versions
* receiving worker result
* validating result against the current block/geometry version
* handing validated mesh to renderer
* updating chunk mesh state

### Renderer Thread Rules

If rendering has its own thread/context, GPU upload must occur only on the thread/context that owns rendering resources.

Worker threads produce CPU buffers only. They do not allocate or fill GPU resources directly unless the rendering architecture explicitly supports safe deferred upload objects.

---

## Snapshot-Based Meshing

Snapshot-based meshing is required.

A meshing snapshot must represent a stable view of all meshing inputs for the duration of the job.

This avoids:

* data races
* partial reads during block updates
* mismatched lighting/block state
* world-lock contention from worker threads

### Snapshot Contents

A snapshot should include:

* chunk coordinate
* source block/geometry version
* source light version
* target chunk block/state data
* required neighbor boundaries
* required lighting data
* compact render descriptors or lookup handles

Snapshot creation should avoid deep-copying whole chunk payloads by default.

When the runtime already stores chunk data in immutable or shareable payload objects, a meshing snapshot should usually capture shared read-only handles or views instead of copying the full chunk contents.

If the live chunk later mutates while those handles are still in use by a worker, the runtime should detach/clone on write so the in-flight meshing snapshot keeps observing its original stable data.

### Source Versions

Each meshing job must carry the block/geometry and light source versions it was built from.

The block/geometry version changes whenever geometry-relevant input changes.

At minimum this includes:

* local block/state changes
* required neighbor-boundary changes that affect the chunk’s mesh

The light version changes whenever local or boundary lighting used by the section mesh changes.

---

## Source Version Validation

When a worker finishes, its result is not automatically accepted.

The world must compare the returned source block/geometry version against the section’s current block/geometry version.

### Accept Result

If the block/geometry version matches, the result may be published even if the light version is stale.

If the light version is stale, the accepted mesh keeps the section `LightMesh` dirty for a future light-only remesh.

### Reject Result

If the block/geometry version does not match, the result is stale and must be discarded.

This prevents late worker results from overwriting newer geometry while still allowing stale lighting to be corrected asynchronously.

---

## Rebuild Queue Behavior

Chunks needing rebuild are scheduled through a rebuild queue managed by the world.

### Queue Requirements

The rebuild queue must:

* avoid enqueuing duplicate active jobs for the same section/block-version/light-version tuple
* allow prioritization of visible or near-player chunks
* support rescheduling when dependencies become available
* drop or ignore stale completed results safely

### Recommended Priority Policy

Higher priority should generally go to:

* chunks near active cameras/players
* chunks newly entering view
* chunks with no current mesh
* chunks whose stale mesh is visibly incorrect

Lower priority can go to:

* far-away chunks
* chunks already displaying an acceptable stale mesh
* chunks outside effective render range

### Deduplication

If a chunk already has a meshing job in flight for its current block/light versions, the world should not enqueue another identical job.

If the block/geometry version changes while a job is in flight, the old job may complete and be discarded, while a new current-version job is scheduled.

---

## Dependency Readiness for Scheduling

A chunk should be considered ready for meshing only when all required meshing inputs are available.

At minimum this means:

* target chunk data exists
* a current or stale lighting snapshot exists
* required neighbor boundaries are obtainable under current policy

Readiness must not require pending authoritative lighting propagation to finish. Meshing should use the best published light data currently available.

If neighbor availability is a hard requirement, the chunk remains dirty but unscheduled until the inputs are ready.

This is preferable to producing low-quality border meshes that immediately become invalid.

---

## GPU Upload Handoff

Workers do not perform direct rendering API work.

After validation, the world hands the completed CPU mesh buffers to the rendering system.

### Handoff Responsibilities

The handoff layer must:

* receive validated mesh data
* create or update GPU buffers on the correct render thread/context
* replace previous mesh resources atomically from the renderer’s perspective
* free or recycle old CPU/GPU resources safely

### Publish Order

Preferred publish order:

1. worker result arrives
2. world validates source block version and records source light version
3. renderer upload request is created
4. renderer performs GPU upload
5. renderer swaps active mesh handle
6. chunk mesh state becomes clean

A mesh should not be considered fully published until the renderer has accepted the new buffers.

Renderer upload queues are budgeted. A frame may upload only a limited number of
chunk section meshes, and any remaining validated CPU meshes stay queued for a
later frame. Buffer updates should avoid synchronizing with in-flight GPU reads;
for OpenGL-style backends this usually means orphaning or otherwise replacing
buffer storage before copying the new CPU data.

---

## Mesh State Model

Each chunk should track an explicit mesh state.

Example conceptual states:

* clean
* dirty
* queued
* building
* waiting_for_upload
* ready
* failed

Exact enum naming is implementation-defined, but the logic must distinguish:

* outdated mesh needing rebuild
* job in progress
* validated result waiting for GPU upload
* current renderable mesh

This avoids ambiguous behavior during high update rates.

---

## Failure Handling

Meshing failures must not corrupt world state.

If a meshing job fails:

* existing valid mesh may remain active if present
* chunk should remain dirty or enter retry state
* failure should be logged in debug/development builds
* renderer must never receive partially formed invalid buffers

Worker exceptions, allocation failures, or malformed descriptors must be treated as job failure, not as silent success.

---

## Performance Expectations

The mesher should be optimized around common visible-world workloads.

Expected practices include:

* iterate chunk cells linearly
* use compact state descriptors
* avoid per-face heap allocation
* reserve buffer capacity where possible
* use fast paths for common full-cube cases
* keep branchy special-case logic out of the hottest common path where possible

Performance optimizations must not change correctness.

---

## Determinism of Mesh Generation

Given the same meshing snapshot and the same registry descriptors, the mesher must produce the same mesh result every time.

This is important for:

* debugging
* version validation reasoning
* predictable rebuild behavior
* testing

Non-deterministic face ordering is discouraged unless the renderer explicitly does not care and the implementation still preserves correctness.

---

## Debugging and Validation

The engine should support validating meshing behavior in development builds.

Useful debug checks include:

* no out-of-bounds local reads
* no live world access from worker meshing code
* source block-version mismatch discard counts
* stale-light mesh acceptance counts
* face/vertex count statistics
* border mismatch diagnostics between neighboring chunks
* AO sample correctness visualization
* missing descriptor assertions

These checks are strongly recommended even if compiled out in release builds.

---

## Required Behavioral Summary

Any compliant meshing implementation must satisfy all of the following:

* meshing uses immutable snapshot inputs
* workers do not access live world state
* opaque and transparent geometry are emitted separately
* face visibility is decided using state-driven occlusion rules
* neighbor sampling works correctly across chunk boundaries
* AO is computed per face vertex using neighbor occupancy rules
* boundary edits invalidate both affected chunks as needed
* worker results carry source block and light version information
* stale block/geometry results are rejected before publish
* stale light versions may be accepted temporarily and must leave a light-only remesh queued
* GPU upload occurs through a render-safe handoff
* chunk mesh state is updated only after successful validation/publish

---

## Non-Goals

This document intentionally does not define:

* the exact vertex binary layout
* the exact renderer transparency technique
* the exact lighting interpolation formula
* content authoring format for block geometry
* gameplay collision mesh generation
* special meshing compression schemes

Those may be defined elsewhere, but they must remain compatible with the rules in this document.

---

## Final Rule

Chunk meshing is a per-section, snapshot-driven, neighbor-aware, version-validated background build pipeline that produces separate opaque and transparent render meshes from block/state/light data.

It must never rely on live worker-thread world access. It must rebuild immediately for geometry changes using current available light data, then rebuild again for light-only changes after authoritative lighting publishes.
