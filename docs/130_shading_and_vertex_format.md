# Shading and Vertex Format

The renderer, mesher, and shader system must share a **strict interface contract**.

This contract defines:

* vertex layouts
* attribute meanings
* per-frame uniforms
* per-draw uniforms
* material parameter layout

The goal is to ensure that:

* meshing output is directly consumable by the renderer
* shaders can rely on stable inputs
* draw submission remains cheap and deterministic

---

## Core Principles

### Fixed Layout Per Render Domain

Each render domain uses a defined vertex layout.

The engine must not treat vertex formats as loosely structured blobs.

At minimum, the renderer defines separate layouts for:

* chunk world geometry
* entity geometry
* UI geometry
* debug geometry

---

### Shader Inputs Are Contractual

Shaders must only consume attributes and uniforms defined by the contract.

The mesher and submission code must not rely on ad hoc shader-specific conventions.

---

### No Live Interpretation of World Data

Shaders operate only on:

* vertex attributes
* per-draw data
* per-frame data
* material parameters
* bound textures

Shaders do not access world or chunk state directly.

---

# Vertex Layouts

---

## Chunk Vertex Layout

Chunk geometry uses a **packed voxel-optimized format**.

```cpp
struct ChunkVertex {
    u32 pos_ao;
    u32 uv;
    u32 light_face_flags;
};
```

Total size: **12 bytes per vertex**

---

## Packed Position and AO (`pos_ao`)

```text
bits  0..4   = local_block_x
bits  5..9   = local_block_y
bits 10..14  = local_block_z

bits 15..19  = model_x
bits 20..24  = model_y
bits 25..29  = model_z

bits 30..31  = ao
```

### Semantics

#### Local Block Position

* chunk-local coordinates
* valid v1 range: `0..15`

#### Model Position

* stored in **1/16 block units**
* examples:

  * `[0,0,0]` → min corner
  * `[8,8,8]` → center
  * `[16,16,16]` → max corner

#### AO

* range: `0..3`
* converted to shading multiplier in shader

---

### Position Reconstruction

```text
local_position = block_position + (model_position / 16.0)
world_position = chunk_world_origin + local_position
```

---

## Packed UV (`uv`)

```text
bits  0..15  = atlas_u
bits 16..31  = atlas_v
```

### Semantics

* stored in **atlas texel space**
* not normalized
* already resolved by mesher

Shader reconstruction:

```text
uv = vec2(atlas_u, atlas_v) / vec2(atlas_width, atlas_height)
```

---

## Packed Lighting, Face, Flags (`light_face_flags`)

```text
bits  0..3   = block_light_r
bits  4..7   = block_light_g
bits  8..11  = block_light_b
bits 12..15  = sky_light

bits 16..18  = face
bits 19..31  = flags
```

---

### Lighting

* 4 bits per channel
* RGB block light + sky light
* range: `0..15`

---

### Face Encoding

```text
0 = +X
1 = -X
2 = +Y
3 = -Y
4 = +Z
5 = -Z
```

Used for:

* normal derivation
* directional shading

---

### Flags

Reserved for documented features:

* cutout hint
* water hint
* animation flags
* future extensions

---

## Entity Vertex Layout

```cpp
struct EntityVertex {
    Vec3f position;
    Vec3f normal;
    Vec2f uv;
    Vec4f color;
};
```

Supports:

* lit/unlit rendering
* textured meshes
* tinting

---

## UI Vertex Layout

```cpp
struct UIVertex {
    Vec2f position;
    Vec2f uv;
    Vec4f color;
};
```

Supports:

* quads
* text
* batching

---

## Debug Vertex Layout

```cpp
struct DebugVertex {
    Vec3f position;
    Vec4f color;
};
```

---

# Vertex Layout Rules

### Stability

Layouts are fixed and versioned by code.

---

### Compatibility

* `Chunk*` materials → `ChunkVertex`
* `Entity*` materials → `EntityVertex`
* `UI*` materials → `UIVertex`

---

### Backend Binding

Renderer owns attribute bindings.
No GL attribute indices in gameplay code.

---

# Shader Interface Structure

Shader inputs are divided into:

* per-frame
* per-draw
* per-material
* per-vertex

---

## Per-Frame Data

```cpp
struct FrameUniforms {
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;

    Vec3f camera_position;
    float time_seconds;

    Vec4f fog_color;
    Vec4f fog_params;
};
```

---

## Per-Draw Data

### Chunk

```cpp
struct ChunkDrawUniforms {
    Vec3f world_origin;
    float pad0;
};
```

---

### Entity

```cpp
struct EntityDrawUniforms {
    Mat4 model;
};
```

---

### UI

```cpp
struct UIDrawUniforms {
    Mat4 transform;
};
```

---

## Per-Material Data

```cpp
struct MaterialUniforms {
    Vec4f tint;
    float emissive;
    float alpha_cutoff;
    u32 flags;
    float pad0;
};
```

---

# Per-Vertex Data Rules

Shaders must interpret packed data exactly as defined.

Chunk shaders assume:

* chunk-local positions
* precomputed UVs
* precomputed AO
* precomputed lighting

No inference from world data.

---

# Chunk Shader Contract

Chunk shaders operate purely on packed vertex + uniforms.

They may assume:

* UVs are atlas-resolved
* lighting is precomputed
* AO is valid
* face encoding is valid

They must NOT:

* access block data
* access chunk data
* perform lighting propagation

---

## Chunk Shader Responsibilities

* unpack vertex data
* reconstruct positions
* normalize UVs
* decode lighting
* apply AO
* apply tint
* apply fog

---

## Opaque

* no blending
* full depth write
* standard shading

---

## Cutout

* alpha discard using `alpha_cutoff`
* still in opaque pass

---

## Transparent

* no depth write
* uses OIT or blending
* fog must match opaque pass

---

# Entity Shader Contract

* uses model transform
* uses normals directly
* no chunk assumptions

---

# UI Shader Contract

* screen space
* no fog
* no world lighting

---

# Debug Shader Contract

* minimal transform + color
* optional fog

---

# Texture Interface Contract

Textures are resolved via **material bindings**.

* chunk → `terrain_atlas`
* UI → `ui_atlas`

Shaders never care about texture origin.

---

# Atlas UV Contract

* atlas built at runtime
* mesher resolves UVs
* shader only samples

No tile lookups in shader.

---

# Packing and Precision Rules

### Chunk Positioning

* chunk-local only
* world offset applied per draw

---

### Lighting

* always packed
* always precomputed

---

### Face Encoding

* replaces normals for chunk meshes

---

# Compatibility Rules

A draw is valid only if:

* vertex format matches shader
* material matches domain
* textures are resolved
* uniforms are provided

---

# Correctness Requirements

System is correct if:

* chunk meshes render consistently
* UV sampling is stable
* AO/lighting decode matches meshing
* fog matches across passes
* no hidden shader assumptions

---

# Summary

The rendering pipeline uses:

* strict vertex layouts per domain
* a packed 12-byte chunk vertex
* runtime-resolved atlas UVs
* precomputed AO and lighting
* clean separation of responsibilities

This ensures:

* high performance
* deterministic rendering
* strong compatibility with meshing + materials
* resistance to architectural drift

---