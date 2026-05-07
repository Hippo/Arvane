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
    u32 light_face_material_hints;
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

## Packed Lighting, Face, Material Hints (`light_face_material_hints`)

```text
bits  0..3   = block_light_r
bits  4..7   = block_light_g
bits  8..11  = block_light_b
bits 12..15  = sky_light

bits 16..18  = face
bits 19..22  = material_class
bits 23..26  = animation_id
bits 27..31  = material_flags
```

Bits `19..31` are a compact per-vertex material hint payload. They are not
generic ad-hoc flags.

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

### Material Hints

This field is not a full material system. It is a compact render hint used by
the shader/renderer to select lightweight behavior.

Actual material definitions remain in block, material, and fluid data. If a
feature needs more information than these fields can provide, that feature
belongs in material/block/fluid data and should be referenced indirectly.

These hints must not become a dumping ground for arbitrary gameplay state. They
are render-facing hints only.

#### `material_class`

Broad rendering category:

```cpp
enum class VertexMaterialClass : u8 {
    Opaque = 0,
    Cutout = 1,
    Transparent = 2,
    Water = 3,
    Lava = 4,
    Foliage = 5,

    Reserved6 = 6,
    Reserved7 = 7,
    Reserved8 = 8,
    Reserved9 = 9,
    Reserved10 = 10,
    Reserved11 = 11,
    Reserved12 = 12,
    Reserved13 = 13,
    Reserved14 = 14,

    Custom = 15,
};
```

#### `animation_id`

Small enum for shader-supported lightweight animations:

```cpp
enum class VertexAnimationId : u8 {
    None = 0,

    WaterStill = 1,
    WaterFlow = 2,
    WaterSurface = 3,

    LavaStill = 4,
    LavaFlow = 5,

    Reserved6 = 6,
    Reserved7 = 7,
    Reserved8 = 8,
    Reserved9 = 9,
    Reserved10 = 10,
    Reserved11 = 11,
    Reserved12 = 12,
    Reserved13 = 13,
    Reserved14 = 14,

    Custom = 15,
};
```

#### `material_flags`

Boolean render modifiers:

```cpp
enum VertexMaterialFlags : u8 {
    VertexMaterialFlag_None          = 0,
    VertexMaterialFlag_TintFromBiome = 1 << 0,
    VertexMaterialFlag_TintFromFluid = 1 << 1,
    VertexMaterialFlag_DoubleSided   = 1 << 2,
    VertexMaterialFlag_DisableAo     = 1 << 3,
    VertexMaterialFlag_Reserved4     = 1 << 4,
};
```

### Examples

Opaque block:

```text
material_class = Opaque
animation_id   = None
material_flags = None
```

Cutout block:

```text
material_class = Cutout
animation_id   = None
material_flags = optional, depending on material data
```

Water:

```text
material_class = Water
animation_id   = WaterStill / WaterFlow / WaterSurface
material_flags = TintFromFluid | DisableAo
```

Water color is not stored in these 13 bits and is not packed per vertex. The
vertex stores only enough information to say that the face is water and should
receive fluid tint and water animation behavior.

Actual water color is resolved through material, fluid, block, biome, or
environment tint data. Until that lookup path is complete, the shader/material
path may use a clearly marked temporary default water tint.

Water vertices should use:

* `material_class = Water`
* `animation_id = WaterStill`, `WaterFlow`, or `WaterSurface`
* `material_flags` includes `TintFromFluid`
* `material_flags` includes `DisableAo`, unless the visual design explicitly
  wants AO on water

---

## Entity Vertex Layout

```cpp
struct EntityVertex {
    Vec3f position;
    Vec2f uv;
    Vec3f color;
    u32 bone_index;
    u32 flags;
};
```

Supports:

* lit/unlit rendering
* textured meshes
* tinting
* entity bone animation
* player skin base/decor alpha behavior

`flags` is currently used by the player skin path:

* `SkinBaseLayer`: transparent texels are rendered as opaque black
* `SkinDecorLayer`: transparent texels are discarded
* no flag: existing entity cutout behavior is used

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
* packed material hints

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
* decode material class, animation id, and material flags
* apply AO
* apply biome/fluid tint when requested by material flags
* apply shader-supported lightweight animations when requested by animation id
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

## Water

Water is represented by material hints, not a standalone water bit.

* `material_class = Water`
* `animation_id` selects water still, flow, or surface behavior
* `material_flags` includes `TintFromFluid`
* `material_flags` usually includes `DisableAo`

The shader may branch on these hints, but the water color itself comes from the
material/fluid/biome tint path rather than the packed vertex.

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
* compact render-facing material hints
* clean separation of responsibilities

This ensures:

* high performance
* deterministic rendering
* strong compatibility with meshing + materials
* resistance to architectural drift

---
