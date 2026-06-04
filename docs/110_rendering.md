# Rendering

## Overview

The rendering system is responsible for producing the final frame from world, entity, and UI data.

It is a **GPU-facing system** that consumes immutable inputs and executes all graphics operations on the render thread.

The renderer must be:

* deterministic per frame
* decoupled from simulation state
* thread-safe via strict ownership rules
* efficient for large voxel worlds

---

## Related Documents

The rendering system depends on the following documents for core behavior and data contracts:

* [Material System](120_material_system.md)
  Defines how materials describe surface properties, texture bindings, and how they are resolved by the renderer.

* [Paged Texture Atlases](114_paged_texture_atlases.md)
  Defines the texture-array-backed atlas model, atlas domains, and dynamic
  player skin residency rules.

* [Shading and Vertex Format](130_shading_and_vertex_format.md)
  Defines vertex layouts, packed formats, and the shader interface contract used by all rendering paths.

* [Particle System](172_particles.md)
  Defines client-owned particle simulation and instanced billboard submission.

---

## Core Principles

### Render Thread Ownership

All GPU operations occur on the **render thread only**.

This includes:

* buffer creation and destruction
* texture uploads
* shader compilation
* framebuffer operations
* draw calls

Other threads must never directly interact with GPU resources.

Dynamic texture residency follows the same rule. Disk reads and PNG decoding may
produce CPU-side image data outside the renderer, but `GL_TEXTURE_2D_ARRAY`
creation, sub-image uploads, mipmap generation, and atlas destruction are render
thread work.

---

### Frame Snapshot Model

Rendering operates on a **fully immutable frame snapshot**.

The renderer must never:

* read live world state
* access chunk data directly
* depend on simulation timing mid-frame

---

### Data Flow

```text
Simulation → Meshing → Render Submission → GPU
```

* Simulation updates world state
* Meshing produces CPU mesh buffers
* Renderer consumes uploaded GPU meshes only

---

## Render Pass Pipeline

Rendering is split into ordered passes.

---

### 1. Opaque Pass

* depth write: enabled
* depth test: enabled
* blending: disabled

Renders:

* solid terrain
* opaque entities

Block and authored entity atlases are texture arrays. Block chunk vertices carry
a flat page/layer value beside packed UVs. Authored entity geometry carries
domain and page/layer data per vertex.

Dynamic player skins render through the `PlayerSkins` atlas domain. The player
skin atlas is a texture array, so visible players using different skin atlas
pages remain batchable as long as they share the same entity shader, sampler
state, and material pipeline.

This pass establishes the **depth buffer**.

---

### 2. Transparent Pass

* depth write: disabled
* depth test: enabled
* blending: enabled

Renders:

* water
* glass
* alpha-blended geometry
* particle billboards

Particles render after transparent chunks while depth writes remain disabled.
Alpha particles use the same `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA` blend mode as
transparent chunk geometry; additive particles draw in a separate particle
sub-pass without changing chunk mesh rendering.

---

### 3. Transparency Composite Pass (OIT)

If using weighted blended OIT:

* resolves accumulation + revealage buffers
* outputs final blended transparency

---

### 4. UI Pass

* depth test: disabled
* rendered in screen space

Renders:

* HUD
* menus
* debug overlays

UI draw vertices carry a texture slot and a flat atlas page. The slot selects
the bound texture-array domain, and the page selects the layer inside that
domain.

---

## Transparency Strategy

### Weighted Blended OIT (Primary)

The engine uses **Weighted Blended Order-Independent Transparency**.

Buffers:

* accumulation buffer (RGBA16F)
* revealage buffer (R16F)

Flow:

1. transparent fragments accumulate weighted color
2. revealage tracks visibility
3. composite pass resolves final output

---

### Fallback (Optional)

* chunk-level back-to-front sorting
* less correct but simpler

---

## Camera Model

### Coordinate System

* right-handed
* Y-up
* Z-forward (negative forward in view space)

---

### Render View

```cpp
struct FogParams {
    Vec3f color;
    float density;
    float start;
    float end;
};

struct RenderView {
    Vec3f position;

    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;

    float near_plane;
    float far_plane;

    FogParams fog;
};
```

---

## Chunk Rendering Flow

### Visibility

Chunks are rendered if:

* within render distance
* inside camera frustum

Frustum culling is required.

---

### Mesh Split

Each chunk produces:

* opaque mesh
* transparent mesh

These are submitted separately.

---

### Draw Rules

Opaque:

* order independent (depth-tested)

Transparent:

* OIT: no strict ordering
* fallback: sorted back-to-front

---

## Shader Ownership

Shaders are fully owned by the renderer.

Responsibilities:

* compilation
* lifetime management
* uniform binding

---

### Shader Types

Minimum required:

* chunk opaque shader
* chunk transparent shader
* OIT composite shader
* UI shader

---

## Fog and Distance Handling

Fog is applied in world shaders.

Purpose:

* hide chunk pop-in
* improve depth perception

---

### Requirements

Fog must:

* be consistent across passes
* not break transparency blending
* smoothly match render distance

---

## UI Rendering

### Order

UI is rendered after all world passes.

---

### Space

* screen space
* independent of camera

---

### Features

* quad batching
* texture atlas support
* optional clipping/scissoring

---

## GPU Resource Model

### Creation Rules

* only on render thread
* never during active draw execution

---

### Resources

* vertex/index buffers
* textures
* framebuffers
* shaders

---

## Render Command API

The renderer exposes a **frame submission API**.

Game and world systems do not issue draw calls directly.
Instead, they build a **FramePacket** and submit it.

---

### Frame Packet

```cpp
struct FramePacket {
    RenderView world_view;

    std::vector<ChunkDrawItem> opaque_chunks;
    std::vector<ChunkDrawItem> transparent_chunks;

    std::vector<EntityDrawItem> opaque_entities;
    std::vector<EntityDrawItem> transparent_entities;

    std::vector<UIDrawItem> ui;
    std::vector<DebugDrawItem> debug;
};
```

This represents a complete, immutable frame.

---

### Draw Items

#### Chunk Draw

```cpp
struct ChunkDrawItem {
    MeshID mesh;
    MaterialID material;
    Vec3f world_origin;
    u64 sort_key;
};
```

---

#### Entity Draw

```cpp
struct EntityDrawItem {
    MeshID mesh;
    MaterialID material;
    Mat4 model;
    u64 sort_key;
};
```

---

#### UI Draw

```cpp
struct UIDrawItem {
    MeshID mesh;
    TextureID texture;
    Rect2f clip_rect;
    Mat4 transform;
    u64 sort_key;
};
```

---

### Renderer Interface

```cpp
class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void resize(u32 width, u32 height) = 0;

    virtual void enqueue_chunk_mesh_upload(ChunkMeshUpload upload) = 0;
    virtual void submit_frame(FramePacket packet) = 0;

    virtual void render() = 0;

    virtual void begin_shutdown() = 0;
};
```

---

## Resource Identification

GPU resources are referenced using **strongly typed IDs**.

```cpp
using MeshID = ID<MeshTag>;
using TextureID = ID<TextureTag>;
using MaterialID = ID<MaterialTag>;
using ShaderID = ID<ShaderTag>;
```

Raw GPU handles must never be exposed outside the renderer.

---

## Chunk Mesh Upload Flow

Meshing threads produce CPU mesh data:

```cpp
struct ChunkMeshData {
    ChunkCoord coord;
    u64 source_version;

    std::vector<Vertex> opaque_vertices;
    std::vector<u32> opaque_indices;

    std::vector<Vertex> transparent_vertices;
    std::vector<u32> transparent_indices;
};
```

This is submitted to the renderer:

```cpp
struct ChunkMeshUpload {
    ChunkCoord coord;
    u64 source_version;
    ChunkMeshData mesh_data;
};
```

---

### Renderer Responsibilities

On upload:

* create or update GPU meshes
* track chunk mesh state
* validate version correctness

---

### Chunk Render State

```cpp
struct ChunkRenderState {
    MeshID opaque_mesh;
    MeshID transparent_mesh;

    u64 uploaded_version;
    bool resident;
};
```

---

## Sort Keys

Each draw item contains a `sort_key` used for batching and ordering.

### Opaque

* material
* mesh
* optional spatial grouping

### Transparent

* OIT: material grouping preferred
* fallback: distance-based sorting

---

## Frame Execution Model

Render thread execution:

```cpp
process_resource_uploads();

begin_opaque_pass();
draw_opaque_chunks();
draw_opaque_entities();

begin_transparent_pass();
draw_transparent_chunks();
draw_transparent_entities();

composite_transparency();

begin_ui_pass();
draw_ui();
draw_debug();
```

---

## Correctness Definition

Rendering is considered **correct** if:

---

### Geometry

* no missing faces
* no incorrect culling
* no cracks between chunks

---

### Transparency

* no flickering
* no incorrect ordering artifacts
* stable blending across frames

---

### Fog and Lighting

* consistent across chunk boundaries
* no visible discontinuities

---

### Performance

* no per-frame allocations in hot paths
* minimal state changes
* effective batching

---

### Stability

* no GPU resource leaks
* no invalid buffer usage
* no threading violations

---

## Render Frame Mesh and Resource Contracts

The renderer consumes renderable data produced by simulation, meshing, asset, and UI systems.

The renderer should not directly inspect or mutate world chunk storage.

Chunk meshing, entity extraction, and UI layout should produce explicit render-frame inputs that the renderer can consume safely.

A render frame is built from a snapshot of renderable state, not from uncontrolled access to gameplay state.

## Render Frame Responsibilities

A render frame should define:

* active camera data
* visible chunk mesh references
* visible entity render data
* active material/texture references
* lighting/fog/environment parameters
* UI draw data
* debug draw data, if enabled

The exact implementation may evolve, but the boundary must remain clear:

```text
simulation / world / ECS / UI state
        ↓
render extraction / mesh publishing / draw data generation
        ↓
render frame
        ↓
renderer backend
        ↓
GPU
```

The renderer backend should consume prepared render data. It should not perform gameplay queries.

## Chunk Mesh Publish Contract

Chunk meshing jobs produce CPU-side mesh data.

A completed chunk mesh result should include:

```text
ChunkMeshResult
    world_id
    chunk_coord
    source_chunk_version
    opaque_mesh_data
    transparent_mesh_data
    material_references
    bounds
```

The mesh result must identify which chunk version it was generated from.

When the result is published, the owning world or render extraction layer must validate that the chunk has not changed since the mesh job began.

If the source version is stale, the mesh result must be discarded.

The renderer should only receive validated mesh data.

## Mesh Data Ownership

CPU mesh data produced by meshing workers is temporary handoff data.

Ownership flow:

```text
meshing worker
    produces CPU mesh result

world/render handoff
    validates version

render thread
    creates or updates GPU buffers

renderer
    owns GPU-side chunk mesh resources
```

Workers must not create OpenGL resources.

Workers must not call OpenGL.

Workers must not mutate renderer-owned GPU resources.

## Opaque and Transparent Mesh Split

Chunk mesh output should keep opaque and transparent geometry separate.

At minimum, chunk mesh data should distinguish:

```text
opaque_vertices
opaque_indices
transparent_vertices
transparent_indices
```

Opaque mesh data is rendered during the opaque world pass.

Transparent mesh data is rendered during the transparent world pass.

This split must be preserved across the CPU mesh result, render upload, and draw submission layers.

## Resource References in Render Data

Render-frame data should reference materials, textures, meshes, and shaders using renderer-managed handles or stable resource identifiers.

Render data should not contain raw OpenGL object IDs unless it is already inside the renderer backend.

Preferred boundary:

```text
ChunkMeshHandle
MaterialHandle
TextureHandle
ShaderHandle
FontAtlasHandle
UiMeshHandle
```

The renderer may internally map these handles to OpenGL objects.

Gameplay, world, UI, and meshing systems should not depend on OpenGL object IDs.

## Frame Lifetime

Render-frame data is valid for the frame or frames explicitly owned by the renderer.

Systems submitting render data must not assume the renderer immediately consumes or destroys it.

The renderer may use buffering internally, such as double or triple buffering.

Any data crossing from simulation to rendering must either be:

* copied
* immutable
* reference-counted
* handle-based with clear lifetime rules

Raw references to mutable world, chunk, ECS, or UI state must not be stored in render-frame data.

## Render Extraction Boundary

Render extraction is the process of converting engine state into renderer-friendly data.

Render extraction may read:

* camera state
* visible chunk mesh handles
* entity transforms and render components
* UI layout output
* debug draw queues
* lighting and environment state

Render extraction must not:

* mutate simulation state
* generate chunk meshes directly
* perform blocking asset loads
* create OpenGL resources from non-render threads
* depend on unstable pointers into world storage

If extraction needs data that may change concurrently, it should use snapshots or synchronized handoff structures.

---

## Renderer GPU Resource Lifetime

The renderer owns GPU resources.

GPU resources include:

* OpenGL buffers
* vertex arrays
* textures
* framebuffers
* renderbuffers
* shader programs
* uniform buffers
* sampler objects
* font atlases
* material GPU state
* mesh GPU allocations

No non-renderer system should directly create, delete, or mutate OpenGL objects.

## Render Thread Ownership

All OpenGL calls must occur on the render thread or the thread that owns the active OpenGL context.

This includes:

* buffer creation
* buffer deletion
* texture creation
* texture deletion
* shader compilation
* framebuffer creation
* vertex array setup
* draw calls
* debug labeling
* GPU timer queries

Worker threads may prepare CPU-side data, but they must hand that data to the render thread for upload.

## GPU Upload Queue

Data prepared off-thread should be transferred to the renderer through an explicit upload queue.

Examples of upload requests:

```text
CreateChunkMeshGpuResource
UpdateChunkMeshGpuResource
DestroyChunkMeshGpuResource
CreateTexture
UpdateTexture
DestroyTexture
CreateShader
DestroyShader
CreateFontAtlas
UpdateFontAtlas
```

The render thread drains this queue at a defined point in the frame.

Upload requests should contain CPU-side data or renderer-safe handles, not raw pointers to mutable world data.

## Chunk Mesh GPU Lifetime

Chunk mesh GPU resources are owned by the renderer and referenced by chunk mesh handles.

A chunk mesh GPU resource may be created when:

* a validated mesh result is published
* a chunk becomes visible for the first time
* a chunk mesh is rebuilt

A chunk mesh GPU resource may be destroyed when:

* the chunk unloads
* the world unloads
* the mesh is replaced
* the renderer shuts down

Replacing a mesh should not leave stale GPU buffers alive indefinitely.

Destruction may be deferred if needed to avoid deleting resources still in use by the current frame.

## Texture and Atlas Lifetime

Textures and atlases are renderer-owned GPU resources.

The asset/resource system may load image bytes and describe texture metadata, but the renderer owns the OpenGL texture object.

Texture atlas construction may involve CPU-side packing and image assembly outside the render thread, but final GPU upload must occur on the render thread.

The material system should reference textures through texture or atlas handles, not OpenGL IDs.

## Shader Lifetime

Shader source files are assets.

Compiled shader programs are renderer-owned GPU resources.

The renderer is responsible for:

* compiling shader stages
* linking programs
* validating shader interfaces
* reporting compile/link errors
* destroying shader programs

Other systems may request a shader by stable name or material reference, but they should not compile OpenGL shader programs directly.

## Resource Destruction Policy

GPU resource destruction should be explicit and safe.

The renderer should support deferred destruction so resources are not deleted while still referenced by an in-flight frame.

A common pattern is:

```text
mark resource for destruction
wait until no in-flight frame can reference it
delete on render thread
```

Early milestones may use a simpler immediate destruction model if rendering is single-threaded, but the ownership model should still assume render-thread-only deletion.

## Buffer Upload Policy

Chunk mesh uploads must avoid driver-visible allocation churn.

Current rules:

* mesh uploads are queued and budgeted per frame
* newer uploads replace stale pending uploads for the same chunk
* GPU buffers keep capacity and grow geometrically instead of reallocating on every upload
* uploads use sub-data updates after warmup
* draw submission is profiled with CPU Tracy zones and GPU queue zones when Tracy is enabled

Future persistent-mapped upload arenas should keep the same public handoff model: workers produce CPU mesh data, the render owner performs GPU writes, and publication still validates chunk versions.

## Frame Pacing

The client supports normal swap-interval vsync and manual frame pacing.

When manual pacing is enabled:

* swap interval vsync is disabled
* the frame sleeps for the bulk of the remaining target interval
* the final configurable spin window tightens presentation timing
* the wait is visible in profiling as GPU/frame wait time

## Shutdown Order

Renderer shutdown must destroy GPU resources before the OpenGL context is destroyed.

Recommended shutdown order:

1. Stop accepting new render submissions.
2. Drain or discard pending upload requests.
3. Finish or synchronize in-flight GPU work if necessary.
4. Destroy renderer-owned GPU resources.
5. Destroy the OpenGL context/window.

No system should attempt to destroy GPU resources after the renderer has shut down.

## Debugging and Validation

In debug builds, renderer resources should be easy to trace.

Where practical, renderer-owned resources should include:

* stable debug names
* owning system labels
* source asset names
* world/chunk coordinates for chunk mesh resources
* creation frame or generation number

This makes stale mesh uploads, leaked buffers, invalid texture references, and mismatched shader contracts easier to diagnose.

---

## Summary

The rendering system is built around:

* strict render-thread ownership
* immutable frame submission (`FramePacket`)
* typed draw lists instead of generic command streams
* chunk-driven rendering
* OIT-based transparency

This design ensures:

* correctness
* performance
* scalability
* clean separation from simulation systems

---
