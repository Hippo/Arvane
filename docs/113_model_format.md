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
  "boxes": [
    {
      "bone": "body",
      "pivot": [0, 12, 0],
      "min": [0, 0, 0],
      "max": [16, 16, 16],
      "inflate": 0,

      "north": { "texture": 0, "uv": [0, 0, 16, 16] },
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

## Boxes

`boxes` is a required array. Each box must be an object with:

* `bone`: required string
* `pivot`: required numeric vec3
* `min`: required numeric vec3
* `max`: required numeric vec3
* `inflate`: numeric, defaulting to `0` when omitted
* `north`, `south`, `east`, `west`, `up`, `down`: face data

Coordinates are authored in sixteenths of a block. Blocks are typically within
`[0, 16]`; entity geometry may extend outside that range.

### Bone Names

`boxes[].bone` is an authored group name used by entity animation binding. It
does not define ownership or hierarchy.

Rules:

* `bone: ""` marks root or static geometry.
* Non-empty `bone` values group boxes for animation.
* Animation keyframes reference the same strings through `keyframes[].bone`.
* Block models may use `bone: ""` for every box.

### Pivots

`boxes[].pivot` is the rotation origin for the box's authored group. Boxes that
share a non-empty `bone` name should use the same pivot.

Root or static boxes still include a pivot so the schema remains uniform.

### Faces

Each face is an object:

```json
{
  "texture": 0,
  "uv": [0, 0, 16, 16]
}
```

Rules:

* `texture == -1` disables the face.
* Other texture indices must reference an entry in `textures`.
* `uv` is a numeric rectangle in authored texture-space units.
* The engine normalizes UVs by dividing by `texture_sizes[texture]`.

## Entity Models

Entity models use the same geometry schema as block models and live under:

```text
assets/<namespace>/models/entity/
```

Entity model files do not contain simulation behavior, hierarchy definitions, or
animation clips. The model-side animation binding metadata is only the box-level
`bone` name and `pivot`.

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
* `boxes` array
* each box has string `bone`
* each box has numeric vec3 `pivot`, `min`, and `max`
* each face has an integer `texture`
* enabled faces have numeric vec4 `uv`

Model files do not contain root-level hierarchy or clip fields.

## Non-Goals

The model format does not define:

* materials
* shaders
* simulation behavior
* animation playback state machines
* editor metadata

Those systems live outside the authored model asset.
