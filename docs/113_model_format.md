# Model And Animation Formats

## Purpose

This document defines the JSON model and animation formats exported from
Blockbench and consumed by Arvane.

Block models and entity models use the same authored model schema. Entity model
files are geometry-only assets; animation clips live in separate animation
assets.

Examples:

```text
arvane:model/block/stone      -> arvane_home/packs/arvane_base/assets/arvane/models/block/stone.json
arvane:model/entity/pig       -> arvane_home/packs/arvane_base/assets/arvane/models/entity/pig.json
arvane:texture/block/stone    -> arvane_home/packs/arvane_base/assets/arvane/textures/block/stone.png
arvane:texture/entity/pig     -> arvane_home/packs/arvane_base/assets/arvane/textures/entity/pig.png
arvane:animation/pig          -> arvane_home/packs/arvane_base/assets/arvane/animations/pig.json
```

## Authored Model Schema

The same schema is used for `assets/<namespace>/models/block/*.json` and
`assets/<namespace>/models/entity/*.json`.

```json
{
  "textures": [
    "pig.png"
  ],
  "texture_sizes": [
    [64, 64]
  ],
  "bones": {
    "body": {},
    "head": {
      "parent": "body",
      "pivot": [0, 12, 0],
      "rotation": [0, 15, 0]
    }
  },
  "boxes": [
    {
      "bone": "head",
      "min": [0, 0, 0],
      "max": [16, 16, 16],
      "origin": [8, 8, 8],
      "rotation": [0, 45, 0],
      "inflate": 0,

      "north": { "texture": 0, "uv": [0, 0, 16, 16], "rotation": 90 },
      "south": { "texture": 0, "uv": [0, 0, 16, 16] },
      "east":  { "texture": 0, "uv": [0, 0, 16, 16] },
      "west":  { "texture": 0, "uv": [0, 0, 16, 16] },
      "up":    { "texture": 0, "uv": [0, 0, 16, 16] },
      "down":  { "texture": 0, "uv": [0, 0, 16, 16] }
    }
  ]
}
```

### Textures

`textures` is a required array of strings. Each face references a texture by
index into this array.

Texture entries may be full asset IDs:

```json
"textures": ["arvane:texture/entity/pig"]
```

Short filenames are also accepted in authored models and resolve within the
model kind:

```json
"textures": ["pig.png"]
```

Input texture slots are written as `$N`. Blocks resolve them from the block
definition's `model.textures` array. The built-in player model uses `$0` for
the selected player skin.

```json
"textures": ["$0"]
```

`texture_sizes` is a required array of numeric vec2 arrays. It defines the UV
coordinate space, not the physical image resolution.

## Bones

`bones` is a required object keyed by authored Blockbench group name. Each bone
entry is an object and may contain:

* `parent`: optional string naming another declared bone
* `pivot`: optional numeric vec3 copied from the Blockbench group origin
* `rotation`: optional numeric vec3 copied from the Blockbench group rotation

Omitted pivots and rotations default to `[0, 0, 0]`. Empty bone names are
invalid. Parent references must name another declared bone and must not form a
cycle.

## Boxes

`boxes` is a required array. Each box must be an object with:

* `bone`: required string referencing a declared bone
* `min`: required numeric vec3
* `max`: required numeric vec3
* `origin`: optional numeric vec3 copied from the Blockbench cube origin
* `rotation`: optional numeric vec3 copied from the Blockbench cube rotation
* `inflate`: numeric, defaulting to `0` when omitted
* `north`, `south`, `east`, `west`, `up`, `down`: face data

Coordinates are authored in sixteenths of a block. Blocks are typically within
`[0, 16]`; entity geometry may extend outside that range.

`boxes[].bone` defines the bone that owns the cube. Animation keyframes
reference the same bone names through `keyframes[].bone`.

### Faces

Each face is an object:

```json
{
  "texture": 0,
  "uv": [0, 0, 16, 16],
  "rotation": 90
}
```

Rules:

* `texture == -1` disables the face.
* Other texture indices must reference an entry in `textures`.
* `uv` is a numeric rectangle in authored texture-space units.
* `rotation` is optional UV rotation in degrees, normally `0`, `90`, `180`, or `270`.
* The engine normalizes UVs by dividing by `texture_sizes[texture]`.

## Entity Models

Entity models use the same geometry schema as block models and live under:

```text
assets/<namespace>/models/entity/
```

Entity model files do not contain simulation behavior or animation clips. Bone
hierarchy and rest-pose transforms live in the model's top-level `bones` object.

## Animation Assets

Animations live under:

```text
assets/<namespace>/animations/
```

Example asset ID:

```text
arvane:animation/pig
```

Animation file schema:

```json
{
  "animations": [
    {
      "name": "walk",
      "length": 1.0,
      "loop": true,
      "keyframes": [
        {
          "time": 0.0,
          "bone": "head",
          "channel": "rotation",
          "value": [0.0, 0.0, 0.0],
          "interpolation": "linear"
        }
      ]
    }
  ]
}
```

### Animation Fields

`animations` is a required array. Each clip has:

* `name`: required string
* `length`: required numeric seconds
* `loop`: required boolean or accepted loop value (`loop`, `once`, `hold`, `none`)
* `keyframes`: required array

Each keyframe has:

* `time`: required numeric seconds
* `bone`: required string matching `boxes[].bone`
* `channel`: required string, one of `rotation`, `position`, `scale`
* `value`: required numeric vec3
* `interpolation`: optional string, currently `linear`

Unsupported channels are invalid.

## Entity Presentation Binding

Entity definitions bind presentation assets through `presentation`:

```json
"presentation": {
  "model": "arvane:model/entity/pig",
  "animation": "arvane:animation/pig"
}
```

`presentation.animation` is optional. Entities with only a model still render.

## Validation

Model validation requires:

* root object
* `textures` array of strings
* `texture_sizes` array of numeric vec2 arrays
* `bones` object
* `boxes` array
* each bone entry is an object with optional numeric vec3 `pivot` and `rotation`
* bone `parent` values reference declared bones
* each box has string `bone` referencing a declared bone
* each box has numeric vec3 `min` and `max`
* box `origin` and `rotation`, when present, are numeric vec3 arrays
* each face has an integer `texture`
* enabled faces have numeric vec4 `uv`
* face `rotation`, when present, is numeric UV rotation in quarter turns

Model files do not contain clip fields.

## Non-Goals

The model format does not define:

* materials
* shaders
* simulation behavior
* animation playback state machines
* editor metadata

Those systems live outside the authored model asset.
