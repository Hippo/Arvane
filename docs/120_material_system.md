## Material System

The renderer uses a **structured, data-driven material system**.

Materials describe **surface intent**, not low-level GPU details.
They do not directly reference shaders or graphics APIs.

The renderer is responsible for mapping materials to:

* render passes
* shader programs
* pipeline state
* GPU resources

---

## Core Principles

### Renderer Ownership

* Materials do not expose shader handles or GPU state
* Shader selection and pipeline configuration are owned by the renderer
* Materials remain backend-agnostic

---

### Logical Texture Binding

Materials reference **logical texture bindings**, not file paths.

This is required because key textures (such as terrain atlases) are **generated at runtime**.

Example:

```yaml
primary_texture: terrain_atlas
```

Not:

```yaml
primary_texture: textures/atlas/terrain.png
```

---

### Immutable Runtime Usage

At runtime, materials are referenced via:

```cpp
using MaterialID = ID<MaterialTag>;
```

Draw systems only use `MaterialID`, never raw textures or shaders.

---

## Material Classification

Each material is defined by three core properties:

---

### MaterialDomain

Defines the high-level rendering category.

```cpp
enum class MaterialDomain : u8 {
    ChunkOpaque,
    ChunkTransparent,
    EntityOpaque,
    EntityTransparent,
    UI,
    Debug
};
```

This determines:

* render pass participation
* expected vertex format
* fog and lighting behavior

---

### MaterialFamily

Defines the specific shader behavior within a domain.

```cpp
enum class MaterialFamily : u8 {
    ChunkAtlasOpaque,
    ChunkAtlasCutout,
    ChunkWater,
    ChunkGlass,

    EntityLit,
    EntityUnlit,

    UITexture,
    UISolid,

    DebugLine,
    DebugSolid
};
```

This allows specialization without exposing shader details.

---

### SurfaceMode

Defines how the surface interacts with depth and blending.

```cpp
enum class SurfaceMode : u8 {
    Opaque,
    Cutout,
    Transparent
};
```

---

### Pass Routing Rules

Render pass selection is derived from `SurfaceMode`:

* `Opaque` → Opaque pass
* `Cutout` → Opaque pass (alpha test)
* `Transparent` → Transparent pass (OIT or blending)

---

## Material Description

Authored materials use a backend-agnostic structure:

```cpp
struct MaterialDesc {
    std::string name;

    MaterialDomain domain;
    MaterialFamily family;
    SurfaceMode surface_mode;

    TextureBindingName primary_texture;

    Vec4f tint = {1, 1, 1, 1};
    float emissive = 0.0f;
    float alpha_cutoff = 0.5f;

    bool double_sided = false;
    bool receives_fog = true;
};
```

---

## Texture Binding System

### Overview

Textures used by materials are resolved through a **binding system**.

Materials reference textures by name:

```cpp
using TextureBindingName = std::string;
```

Example bindings:

* `"terrain_atlas"`
* `"ui_atlas"`
* `"font_atlas"`

---

### Runtime Resolution

At runtime, bindings are resolved to actual GPU textures:

```cpp
struct TextureBindingRegistry {
    TextureID terrain_atlas;
    TextureID ui_atlas;
    TextureID font_atlas;
};
```

Resolution:

```cpp
TextureID resolve_texture(TextureBindingName name);
```

---

### Atlas Generation

Texture atlases are:

* built at runtime from source textures
* uploaded to the GPU
* registered under a logical binding name

Example:

```cpp
texture_bindings.terrain_atlas = create_terrain_atlas(...);
```

---

### Responsibilities

#### Block System

* defines source texture names per block

#### Atlas Builder

* packs textures into atlases
* generates UV mapping metadata

#### Mesher

* writes final UVs into vertex data

#### Material System

* references the atlas via binding name

#### Renderer

* resolves binding to `TextureID`
* binds texture during draw

---

## Runtime Material Representation

Materials are stored as resolved runtime records:

```cpp
struct MaterialRecord {
    MaterialID id;
    std::string name;

    MaterialDomain domain;
    MaterialFamily family;
    SurfaceMode surface_mode;

    TextureBindingName primary_texture_name;
    TextureID primary_texture;

    Vec4f tint;
    float emissive;
    float alpha_cutoff;

    bool double_sided;
    bool receives_fog;
};
```

---

## Shader Selection

Shaders are selected internally by the renderer.

Selection is based on:

* `MaterialDomain`
* `MaterialFamily`

Example:

* `ChunkOpaque + ChunkAtlasOpaque` → chunk opaque shader
* `ChunkTransparent + ChunkWater` → water shader
* `UI + UITexture` → UI shader

Materials never directly reference shaders.

---

## Pipeline State Rules

Pipeline state is derived from material properties.

### Examples

* Transparent → depth write disabled
* UI → depth test disabled
* Double-sided → face culling disabled
* Cutout → alpha discard enabled

The renderer owns all pipeline configuration.

---

## Material Parameters

Materials expose a small structured parameter set:

* tint
* emissive
* alpha cutoff

These are uploaded to the GPU as uniform data.

---

## Chunk Rendering Rules

Chunk rendering uses a **small fixed set of shared materials**.

Example:

* `chunk/opaque_atlas`
* `chunk/cutout_atlas`
* `chunk/glass`
* `chunk/water`

---

### Important Constraint

There is **not one material per block type**.

Instead:

* block variation comes from atlas UVs and vertex data
* materials define shading behavior only

This is critical for batching and performance.

---

## Material and Meshing Separation

### Mesher Responsibilities

* assigns geometry to opaque or transparent mesh
* generates vertex data (positions, UVs, AO, lighting)

---

### Material Responsibilities

* defines shading behavior
* defines texture source (via binding)
* defines blending and fog behavior

---

## Example Materials

### Opaque Terrain

```yaml
name: chunk/opaque_atlas
domain: ChunkOpaque
family: ChunkAtlasOpaque
surface_mode: Opaque
primary_texture: terrain_atlas
tint: [1.0, 1.0, 1.0, 1.0]
emissive: 0.0
alpha_cutoff: 0.0
double_sided: false
receives_fog: true
```

---

### Water

```yaml
name: chunk/water
domain: ChunkTransparent
family: ChunkWater
surface_mode: Transparent
primary_texture: terrain_atlas
tint: [0.85, 0.92, 1.0, 0.7]
emissive: 0.0
alpha_cutoff: 0.0
double_sided: false
receives_fog: true
```

---

## Correctness Requirements

The material system is correct if:

* materials remain backend-agnostic
* texture bindings resolve deterministically
* shader selection is consistent
* chunk rendering remains batched and efficient
* transparency behavior is stable

---

## Summary

The material system is designed to be:

* structured and predictable
* renderer-controlled
* compatible with runtime-generated atlases
* optimized for voxel terrain rendering

Key properties:

* materials reference logical texture bindings
* renderer resolves all GPU resources
* materials do not expose shader or pipeline details
* chunk rendering uses a small shared material set

---
