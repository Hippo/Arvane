# Key Bind Saves

## Purpose

This document defines how **input bindings are stored, loaded, and managed** in Arvane.

It exists to make the following explicit:

* how logical actions are mapped to physical inputs
* how bindings are persisted across sessions
* how rebinding is handled safely
* how input configurations integrate with the input system
* how defaults and fallbacks are applied

This document complements the input model defined in [player controller and input](160_player_controller_and_input.md)

and should not redefine input behavior—only how bindings are configured and saved.

---

## Design Goals

The key binding system should prioritize:

* separation between **logical actions** and **physical inputs**
* stable and human-readable save format
* safe loading with fallback behavior
* easy rebinding at runtime
* extensibility for future input devices
* deterministic mapping used by simulation-facing input

---

## Core Principle

Bindings map:

```text
Logical Action → One or More Physical Inputs
```

Examples:

```text
MoveForward → [Keyboard: W]
Jump        → [Keyboard: Space]
PrimaryUse  → [Mouse: LeftButton]
```

Gameplay systems **never** reference physical inputs directly.

---

## Logical Action Source of Truth

The set of valid logical actions is defined by the input system (see [player controller and input](160_player_controller_and_input.md)).

The keybind system must:

* only allow binding to **known actions**
* reject or ignore unknown actions on load
* allow new actions to appear without breaking old configs

---

## Binding Structure

Each binding entry represents:

```text
ActionBinding
    action_name
    inputs[]
```

Where each input is:

```text
InputBinding
    device_type
    input_code
    modifiers (optional)
```

---

## Supported Device Types

Initial support:

```text
keyboard
mouse_button
mouse_wheel
```

Future support:

```text
gamepad_button
gamepad_axis
```

The system must be extensible without changing the save format structure.

---

## Input Encoding

Bindings must be stored in a **platform-independent format**.

Examples:

```text
Keyboard: W
Keyboard: Space
MouseButton: Left
MouseWheel: ScrollUp
```

Avoid raw platform scancodes in save files.

The platform layer is responsible for translating OS-specific input into these canonical values.

---

## Save Format

Bindings should be stored in a human-readable format such as **YAML or JSON**.

### Example (YAML)

```yaml
bindings:
  MoveForward:
    - device: keyboard
      key: W

  MoveBackward:
    - device: keyboard
      key: S

  Jump:
    - device: keyboard
      key: Space
  
  Sneak:
    - device: keyboard
      key: LCtrl

  Sprint:
    - device: keyboard
      key: LShift

  PrimaryInteract:
    - device: mouse_button
      button: Left

  SecondaryInteract:
    - device: mouse_button
      button: Right

  NextHotbarSlot:
    - device: mouse_wheel
      direction: Up

  PrevHotbarSlot:
    - device: mouse_wheel
      direction: Down
```

---

## Save Location

Bindings must live inside the **user configuration directory**:

```text
arvane_home/
  config/
    input_bindings.yaml
```

This ensures:

* separation from game data (saves)
* portability between worlds
* persistence across sessions

---

## Default Bindings

The engine must define a **default binding set**.

On startup:

1. Attempt to load user bindings
2. If missing or invalid:

   * fall back to defaults
3. Optionally write defaults to disk if file does not exist

Defaults must be:

* complete (all required actions bound)
* conflict-free where possible
* familiar to voxel/FPS players

---

## Loading Behavior

When loading bindings:

### Validation Rules

* unknown actions → ignored
* unknown inputs → ignored
* malformed entries → skipped
* empty bindings → allowed but may reduce usability

### Fallback Behavior

If a required action has no valid binding:

* assign default binding for that action
* log a warning

The system must never leave the player unable to perform critical actions like movement or interaction.

---

## Runtime Representation

At runtime, bindings should be compiled into efficient lookup structures:

```text
Physical Input → Actions
Action → Physical Inputs
```

This allows:

* fast input resolution during frame collection
* efficient detection of pressed/held/released states

---

## Rebinding

### Requirements

The system must support runtime rebinding.

Typical flow:

```text
User selects action → system enters capture mode → next input assigned
```

### Behavior Rules

* rebinding replaces or appends depending on mode
* duplicate bindings are allowed but may be warned
* invalid inputs are rejected

### Conflict Handling

Conflicts are not strictly forbidden but should be detectable:

Example:

```text
W → MoveForward
W → OpenInventory
```

System should:

* allow it
* warn user/UI
* not crash or behave unpredictably

---

## Multi-Binding Support

An action may have multiple bindings:

```text
Jump → [Space, GamepadA]
```

An input may trigger multiple actions (not recommended but allowed).

---

## Modifiers

Optional modifier support:

```text
Shift + W
Ctrl + Click
```

Representation:

```yaml
- device: keyboard
  key: W
  modifiers: [Shift]
```

Modifiers should:

* be optional
* be consistently evaluated during input resolution

---

## Integration with Input System

The binding system feeds into the **input abstraction layer** defined in [player controller and input](160_player_controller_and_input.md)


Flow:

```text
Platform Input
    ↓
Binding Resolution
    ↓
Logical Actions
    ↓
Per-Tick Player Command Snapshot
```

The binding system must:

* operate before simulation tick consumption
* produce stable logical action states
* support held/pressed/released semantics

---

## Hot Reloading (Optional)

Bindings may support live reload:

* file changes detected → reload bindings
* safe recompile of binding maps
* does not interrupt gameplay

This is useful for development and debugging.

---

## Versioning

Bindings format should support versioning:

```yaml
version: 1
bindings:
  ...
```

Future changes:

* allow migration logic
* preserve backward compatibility where possible

---

## Debugging and Tooling

Useful debug capabilities:

* print current bindings
* visualize active actions per frame
* detect conflicts
* log invalid bindings during load

---

## Required Invariants

Implementations must preserve:

* logical actions are the only interface to gameplay
* bindings are externalized (not hard-coded)
* missing bindings fall back safely
* loading never crashes due to malformed files
* binding resolution is deterministic per frame/tick
* platform-specific input is fully abstracted before binding

---

## Summary

The keybind system is a **configuration layer**, not a gameplay system.

It is responsible for:

* mapping physical inputs to logical actions
* persisting user preferences
* enabling rebinding
* safely handling invalid or missing data

It must remain:

* simple
* robust
* extensible

And most importantly:

**it must never leak platform-specific input details into gameplay logic.**
