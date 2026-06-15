# Data-Driven In-Hand Item Animations

## Intent

In-hand item animations should be authored as data, not hard-coded per item type.

The engine should provide a small runtime animation system for first-person and third-person held-item presentation. Item definitions select animation clips by name, and clips are loaded from JSON files.

This allows common behavior such as:

- item swing
- sword blocking
- bow draw
- item inspecting
- eating/drinking
- tool use
- custom plugin-defined held-item animations

without requiring every animation to be implemented directly in C++.

## Scope

This document covers presentation animation only.

In-hand item animations do not determine gameplay authority. The server remains authoritative for:

- attack timing
- block breaking progress
- item use validation
- cooldowns
- damage
- inventory state
- interaction results

The client may start predicted presentation animations immediately, but gameplay results must still come from the authoritative simulation.

## Animation File Location

Held-item animations are loaded from:

```text
data/<namespace>/item_animations.json in installed packs
```

The built-in definitions live at
`arvane_home/packs/arvane_base/data/arvane/item_animations.json`.
Additional packs may add definitions or explicitly override existing ones using
the pack data override rules.

## File Shape

```json
{
  "animations": [
    {
      "name": "arvane:swing/default",
      "duration_seconds": 0.28,
      "loop": false,
      "channels": {
        "first_person": {
          "position": [
            { "time": 0.0, "value": [0.0, 0.0, 0.0] },
            { "time": 0.08, "value": [0.25, -0.18, -0.12] },
            { "time": 0.16, "value": [-0.16, 0.10, -0.04] },
            { "time": 0.28, "value": [0.0, 0.0, 0.0] }
          ],
          "rotation_degrees": [
            { "time": 0.0, "value": [0.0, 0.0, 0.0] },
            { "time": 0.08, "value": [22.0, -38.0, 12.0] },
            { "time": 0.16, "value": [-12.0, 18.0, -8.0] },
            { "time": 0.28, "value": [0.0, 0.0, 0.0] }
          ],
          "scale": [
            { "time": 0.0, "value": [1.0, 1.0, 1.0] }
          ]
        }
      }
    }
  ]
}
```

## Animation Identity

Each animation has a stable namespaced identifier:

```text
namespace:path
```

Examples:

```text
arvane:swing/default
arvane:swing/heavy
arvane:block/sword
arvane:inspect/default
arvane:consume/drink
```

Animation names are stable data identifiers and should not depend on runtime IDs.

## Item Definition Integration

Item definitions may reference held-item animations by action name.

Example:

```json
{
  "name": "arvane:iron_sword",
  "display_name": "Iron Sword",
  "model": "arvane:model/item/iron_sword",
  "held_animations": {
    "primary_use": "arvane:swing/default",
    "secondary_use_start": "arvane:block/sword/start",
    "secondary_use_hold": "arvane:block/sword/hold",
    "secondary_use_end": "arvane:block/sword/end",
    "inspect": "arvane:inspect/default"
  }
}
```

For simple bindings, the value may be:

* a string animation id, which plays whenever that action is triggered
* `null`, which explicitly disables animation for that action
* an object with `animation` and `play_when` fields

If `animation` is omitted from an object binding, the engine uses the default held
item swing animation for that action. If `animation` is `null`, the action is
disabled even if a default would otherwise exist.

```json
{
  "name": "arvane:water_bucket",
  "held_animations": {
    "secondary_use_start": {
      "play_when": "placement_succeeds"
    }
  }
}
```

Supported `play_when` values:

* `always`: play as soon as the action is triggered
* `placement_succeeds`: play only when the client predicts that the secondary use
  will place a block or place source fluid

The current engine defaults are:

* `primary_use`: `arvane:swing/default`, `play_when: "always"`
* `secondary_use_start` for block items: `arvane:swing/default`,
  `play_when: "placement_succeeds"`
* `secondary_use_start` for other items: `arvane:swing/default`,
  `play_when: "always"`

Recommended action keys:

```text
primary_use
secondary_use_start
secondary_use_hold
secondary_use_end
consume_start
consume_hold
consume_end
inspect
equip
unequip
idle
```

The engine may define common action keys, but item data should be allowed to reference custom keys for plugin-defined behavior.

## Runtime Model

The held-item animation system should evaluate the active animation clip every render frame.

The evaluated transform is applied on top of the base held-item pose:

```text
final_pose = camera_pose
           * base_held_item_pose
           * active_animation_pose
           * recoil_or_view_bob_pose
```

The exact multiplication order may be adjusted by the renderer, but it must be documented and consistent.

## Channels

An animation clip may define separate channels for:

```text
first_person
third_person
ui_preview
```

The initial implementation only needs `first_person`.

Each channel may contain:

```text
position
rotation_degrees
scale
```

Future optional channels may include:

```text
alpha
emissive_multiplier
texture_frame
sound_events
particle_events
```

## Keyframes

Each transform channel is a list of keyframes.

```json
{
  "time": 0.12,
  "value": [0.0, -0.2, 0.1],
  "easing": "ease_out"
}
```

Rules:

* `time` is in seconds.
* keyframes must be sorted by time after loading.
* missing time `0.0` should be treated as the default identity value.
* values after the final keyframe hold the final value unless the clip loops.
* malformed keyframes should produce a clear content error.

Supported easing values:

```text
linear
step
ease_in
ease_out
ease_in_out
```

The initial implementation may support only `linear`, but the JSON schema should reserve the `easing` field.

## Blending

The animation player should support simple blend-in and blend-out fields:

```json
{
  "name": "arvane:block/sword/hold",
  "duration_seconds": 1.0,
  "loop": true,
  "blend_in_seconds": 0.08,
  "blend_out_seconds": 0.10
}
```

Blending prevents abrupt snapping when switching between held-item states.

For the first implementation:

* one active animation is sufficient
* replacing the active animation may blend from the previous sampled pose
* layered animation is not required

Future versions may support animation layers such as:

```text
base idle layer
use/action layer
camera reaction layer
status effect layer
```

## Events

Animations may contain named events.

```json
{
  "events": [
    { "time": 0.07, "name": "whoosh" },
    { "time": 0.12, "name": "impact_window" }
  ]
}
```

Events are presentation hooks unless explicitly consumed by gameplay code.

Examples:

```text
whoosh sound
hand particle
inspection glint
camera impulse
```

Gameplay-critical timing must not rely exclusively on client animation events.

## Swing Animation

The default item swing should be data-driven.

The player controller should request the appropriate held-item action when the local player starts a primary use action.

Example flow:

```text
input primary_use pressed
-> client predicts primary-use presentation
-> held item resolves primary_use animation
-> animation player starts arvane:swing/default
-> client sends interaction intent to server
-> server validates and applies authoritative result
```

The swing animation should not directly imply that an attack or block break succeeded.

## Blocking Animation

Sword blocking or shield blocking should use a start/hold/end sequence.

Example:

```json
{
  "held_animations": {
    "secondary_use_start": "arvane:block/sword/start",
    "secondary_use_hold": "arvane:block/sword/hold",
    "secondary_use_end": "arvane:block/sword/end"
  }
}
```

The hold animation should usually be looping.

The client may play the start animation immediately, then transition into hold while the input remains active. The server decides whether the player is actually blocking.

## Inspect Animation

Inspect animations are cosmetic and client-local by default.

Example:

```json
{
  "held_animations": {
    "inspect": "arvane:inspect/default"
  }
}
```

The inspect action should not be sent to the server unless a plugin explicitly requires it.

## Validation

The loader should validate:

* animation names are present and namespaced
* duration is positive
* keyframe times are finite and non-negative
* keyframe values have the correct component count
* referenced item animations exist
* looping clips have valid duration
* duplicate animation names are rejected or clearly overridden according to content-pack priority rules

Missing optional fields should use safe defaults.

## Defaults

If an item does not specify a held animation for an action, the engine should fall back to sensible defaults.

Recommended defaults:

```text
primary_use -> arvane:swing/default
inspect     -> no animation unless globally configured
equip       -> optional default equip animation
idle        -> optional default idle animation
```

Items may explicitly disable a default animation:

```json
{
  "held_animations": {
    "primary_use": null
  }
}
```

## Implementation Notes

The implementation should introduce a small held-item animation runtime, likely in the client/render-facing layer rather than the world simulation layer.

Suggested responsibilities:

```text
ItemAnimationClip
- loaded immutable animation data

ItemAnimationLibrary
- maps stable animation names to clips

HeldItemAnimationPlayer
- active clip
- elapsed time
- loop state
- blend state
- sampled output pose

HeldItemAnimationResolver
- maps item definition + action key to animation clip
```

The world simulation should not depend on the animation system.

## C++ Data Sketch

```cpp
struct ItemAnimationKeyframeVec3 {
    float time_seconds = 0.0f;
    Vec3 value {};
    ItemAnimationEasing easing = ItemAnimationEasing::Linear;
};

struct ItemAnimationTransformChannel {
    std::vector<ItemAnimationKeyframeVec3> position;
    std::vector<ItemAnimationKeyframeVec3> rotation_degrees;
    std::vector<ItemAnimationKeyframeVec3> scale;
};

struct ItemAnimationClip {
    StableName name;
    float duration_seconds = 0.0f;
    bool loop = false;
    float blend_in_seconds = 0.0f;
    float blend_out_seconds = 0.0f;

    ItemAnimationTransformChannel first_person;
    ItemAnimationTransformChannel third_person;
};

struct HeldItemAnimationPose {
    Vec3 position {};
    Vec3 rotation_degrees {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};
```

## Non-Goals

This system does not replace skeletal animation.

This system does not define entity animation.

This system does not make gameplay timing client-authoritative.

This system does not require a full animation graph.

The goal is a small, deterministic, data-driven transform animation layer for held items.
