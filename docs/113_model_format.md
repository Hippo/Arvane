# Model Format

## Purpose

This document defines the JSON model format exported from Blockbench and consumed by the Arvane engine.

It applies to both:

* block models
* entity models

The format is intentionally simple, deterministic, and independent of Blockbench-specific concepts.

---

## Core Principles

### 1. Texture Indexing

All textures are defined in a top-level array:

```json
"textures": ["stone.png", "stone_top.png"]
```

Faces reference textures by **index** into this array.

* `0` → first texture
* `1` → second texture
* `-1` → no texture (face is invisible / skipped)

---

### 2. Texture Size Defines UV Space

Each texture has a corresponding logical size:

```json
"texture_sizes": [
  [16, 16],
  [16, 16]
]
```

**Important:**

> `texture_sizes` defines the UV coordinate space — NOT the physical image resolution.

This allows decoupling UV mapping from actual texture resolution.

---

### 3. UV Normalization Rule (Engine-Side)

The engine MUST normalize UVs as follows:

```
normalized_uv = uv / texture_size
```

Example:

```json
"uv": [0, 0, 16, 16]
"texture_size": [16, 16]
```

Result:

```
[0, 0, 1, 1]
```

This means:

* UVs always map correctly regardless of actual image resolution
* A `32x32` texture with `texture_size = [16,16]` behaves identically to a `16x16` texture

---

## Block Model Format

Example: [stairs.json](../arvane_home/assets/models/block/stairs.json)

### Structure

```json
{
  "textures": [...],
  "texture_sizes": [...],
  "boxes": [...]
}
```

---

### Boxes

A block model is composed of one or more axis-aligned boxes.

```json
{
  "min": [x, y, z],
  "max": [x, y, z],
  "north": Face,
  "south": Face,
  "east": Face,
  "west": Face,
  "up": Face,
  "down": Face
}
```

---

### Coordinate Space

* Units are in **model space**
* Blocks are typically within `[0, 16]`
* Partial blocks (stairs, slabs) use subdivisions

---

### Face Definition

```json
{
  "texture": 0,
  "uv": [u0, v0, u1, v1]
}
```

#### Rules

* `texture = -1` → face is ignored (not rendered)
* `uv` is in **texture space**, not normalized
* UV orientation is preserved as exported

---

## Entity Model Format

Example: [pig.json](../arvane_home/assets/models/entity/pig.json)

### Structure

```json
{
  "name": "entity_name",
  "textures": [...],
  "texture_sizes": [...],
  "skeleton": [...],
  "animations": [...]
}
```

---

## Skeleton

Entities are composed of bones.

```json
{
  "name": "bone_name",
  "pivot": [x, y, z],
  "boxes": [...]
}
```

---

### Bone Rules

* `pivot` is the rotation origin
* Boxes are defined in model space (same as blocks)
* No implicit hierarchy transformation is assumed in this format

  * hierarchy is structural, not pre-transformed

---

## Boxes (Entities)

Identical to block boxes:

```json
{
  "min": [...],
  "max": [...],
  "north": Face,
  ...
}
```

---

## Animations

```json
{
  "name": "walking",
  "length": 1.5,
  "loop": "loop",
  "keyframes": [...]
}
```

---

### Keyframes

```json
{
  "time": 0.5,
  "bones": [
    {
      "name": "bone_name",
      "rotation_euler": [x, y, z]
    }
  ]
}
```

---

### Animation Rules

* Time is in **seconds**
* Rotation is **Euler degrees**
* Only animated properties are included
* Missing properties = unchanged

---

## Engine Requirements

### Face Handling

* Faces with `texture = -1` MUST NOT generate geometry
* Faces MUST use the texture index to resolve atlas/texture binding

---

### UV Handling

The engine MUST:

1. Lookup `texture_sizes[texture_index]`
2. Normalize UV:

```
u = uv / texture_size
```

3. Use normalized UVs for rendering

---

### Texture Resolution Independence

The engine MUST NOT assume:

* texture resolution == texture_size

Only `texture_size` matters for UV mapping.

---

### Multi-Texture Support

* Models may use multiple textures
* Faces may reference different textures within the same model
* Texture binding strategy (atlas vs bindless vs array) is engine-defined

---

## Constraints

### Required Fields

* `textures`
* `texture_sizes`

### Validation Rules

* `textures.length == texture_sizes.length`
* All `texture` indices must be:

  * `-1`, or
  * valid index into `textures`

---

## Non-Goals

This format does NOT define:

* material properties
* shaders
* lighting
* texture atlases
* rendering pipeline

Those are handled elsewhere in the engine.

---

## Summary

This format is designed to:

* be simple to export from Blockbench
* be trivial to parse in C++
* support multi-texture models
* be resolution-independent
* remain stable for both blocks and entities

---
