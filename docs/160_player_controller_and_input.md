# Player Controller and Input

## Purpose

This document defines the **input model** and **player controller contract** for Arvane.

It exists to make the following explicit:

- how raw platform input is converted into game input
- how the local player camera behaves
- how movement modes behave
- how block targeting works
- how interaction selection is resolved
- how hotbar selection and held-item use behave

Without this file, implementations tend to invent inconsistent defaults for movement, camera sensitivity, reach, interaction order, and controller logic.

This document defines the intended baseline behavior for the engine and gameplay layer.

---

## Design Goals

The input and player model should prioritize:

- predictable and responsive control
- clear separation between raw device input and gameplay actions
- deterministic simulation-facing movement inputs per tick
- support for both local prediction and server authority
- extensibility for future rebinding and additional devices
- behavior that feels like a voxel game rather than a free-fly scene viewer

The player controller is not a debug camera.

---

## Scope

This document defines:

- input abstraction layer
- player movement intent
- camera look behavior
- walking, jumping, and flying rules
- block targeting behavior
- hotbar selection behavior
- interaction priority rules

This document does **not** define:

- physics internals
- collision internals
- inventory contents beyond hotbar selection behavior
- UI widget interaction details
- networking packet formats

Those belong in their respective documents.

---

## Input Model Overview

Input must be represented in layers:

1. **Platform input**
   - keyboard keys
   - mouse buttons
   - mouse motion
   - mouse wheel
   - future gamepad support

2. **Logical actions**
   - move forward
   - move backward
   - move left
   - move right
   - jump
   - crouch / sneak
   - sprint
   - primary use
   - secondary use
   - pick block
   - hotbar slot selection
   - toggle fly
   - drop item
   - open inventory

3. **Simulation-facing player commands**
   - movement vector intent
   - look rotation intent
   - jump request
   - crouch state
   - sprint state
   - interaction requests
   - selected hotbar slot

The engine must not couple gameplay logic directly to raw keyboard or mouse codes.

---

## Input Abstraction

### Principle

Gameplay systems must consume **abstract actions**, not platform-specific key codes.

For example:

- `MoveForward`
- `MoveBackward`
- `MoveLeft`
- `MoveRight`
- `Jump`
- `Sneak`
- `Sprint`
- `PrimaryInteract`
- `SecondaryInteract`
- `PickBlock`
- `Hotbar1` through `Hotbar9`
- `NextHotbarSlot`
- `PrevHotbarSlot`
- `ToggleFly`
- `OpenInventory`

This allows rebinding and future support for other input devices without rewriting gameplay logic.

### Action Types

Inputs should be classified into:

- **Held actions**
  - active while input remains pressed
  - examples: move, sprint, sneak, primary hold for mining

- **Pressed actions**
  - fires on transition from up to down
  - examples: jump, hotbar number select, inventory open

- **Released actions**
  - fires on transition from down to up
  - examples: stop mining/channeling if needed

- **Analog deltas**
  - examples: mouse delta for camera look, scroll delta for hotbar changes

### Frame Collection and Tick Consumption

Raw input is gathered at platform frame rate, but gameplay simulation operates on fixed ticks.

Input handling must therefore:

- gather device state every frame
- accumulate mouse delta between simulation ticks
- track button/key transitions
- present a stable input snapshot to each simulation tick

This prevents tick-rate dependence on render frequency.

---

## Local Player vs Simulation Input

The local player controller produces a **player command snapshot** for each simulation tick.

This snapshot should include at minimum:

- move intent on X/Z plane
- look yaw delta
- look pitch delta
- jump requested
- sneak held
- sprint held
- primary interact held/pressed
- secondary interact held/pressed
- selected hotbar slot
- optional fly toggle request

The simulation consumes the command snapshot once per tick.

For networking:

- local client may predict movement immediately
- authoritative validation belongs to server/game simulation
- client input should be representable as compact per-tick intent, not raw hardware state

---

## Cursor Capture and Input Focus

### World Control Mode

When the player is actively controlling the character:

- mouse cursor is captured/locked
- mouse motion controls camera
- gameplay input is routed to the player controller

### UI Control Mode

When menus or inventory are open:

- mouse cursor is released
- UI receives pointer input
- player look input is disabled
- gameplay interactions should not fire unless explicitly allowed

### Focus Loss

When the window loses focus:

- movement held states should be cleared or safely neutralized
- mouse capture should be released if appropriate
- no stuck movement or stuck mining state should remain after refocus

---

## Camera Model

## Camera Ownership

The player controller owns the local first-person camera state.

The camera is derived from:

- player position
- eye height
- yaw
- pitch

The camera is not independently simulated from the player body under normal gameplay.

### Coordinate Convention

Camera orientation must follow the engine coordinate conventions defined elsewhere.

At minimum:

- yaw rotates around world up axis
- pitch rotates around local horizontal axis
- roll is not used for normal player control

### Pitch Limits

Pitch must be clamped to prevent camera flipping.

Recommended behavior:

- minimum pitch near straight down
- maximum pitch near straight up
- do not allow full inversion

A typical implementation clamps pitch to a range similar to `[-89°, +89°]`.

### Mouse Look

Mouse motion controls yaw and pitch.

Requirements:

- horizontal mouse delta affects yaw
- vertical mouse delta affects pitch
- sensitivity must be configurable
- optional Y inversion may be supported later
- input should feel raw and direct, without heavy smoothing by default

The baseline controller should favor responsiveness over cinematic smoothing.

### Eye Position

The player camera uses an eye offset from the player body.

This eye offset must be defined in gameplay constants, not scattered through code.

Examples of values that belong in centralized constants:

- standing eye height
- sneaking eye height
- optional transition speeds if eye height interpolation is used

### Camera Interpolation

For rendering between simulation ticks:

- camera position may be interpolated from prior/current simulation state
- camera rotation may also be interpolated if needed
- interpolation must not introduce visible latency or control mushiness

The intended feel is sharp and responsive.

---

## Player Movement Modes

The player controller supports distinct movement modes.

Baseline modes:

- walking
- flying

Additional modes like swimming, climbing, noclip, riding, or spectator can be added later.

---

## Walking Mode

Walking mode is the default grounded movement mode.

### Movement Intent

Walking movement is controlled by directional intent:

- forward/backward
- strafe left/right

Movement intent is relative to camera yaw, not world axes.

Pitch does not affect walking direction.

### Ground Movement

When grounded:

- movement accelerates toward desired horizontal velocity
- collision and resolution follow the physics/collision document
- walking should feel deliberate and game-like, not slippery unless explicitly tuned that way

### Air Movement

When airborne:

- directional control still exists
- air control is weaker or otherwise intentionally limited relative to grounded movement
- exact movement constants belong in gameplay tuning, but behavior must remain consistent and explicit

### Jumping

Jumping behavior:

- jump is triggered by a press event
- jump should only fire when grounded, unless another mode explicitly permits otherwise
- holding jump should not repeatedly trigger jumps unless a separate auto-jump rule is added later

The baseline behavior is one jump per grounded press.

### Sprinting

Sprinting may modify walking speed.

Baseline expectations:

- sprint is a held modifier
- sprint only affects valid movement states
- sprint behavior should be centrally configurable
- sneaking and sprinting should not both fully apply at the same time

Whether sprinting is included in the first gameplay slice is configurable, but the controller should reserve the action.

### Sneaking / Crouching

Sneaking is a held movement modifier.

Baseline sneak behavior:

- reduces movement speed
- engages edge-safe behavior as defined in physics/collision
- eye hight gets lowered slightly
- may alter interaction semantics if needed by gameplay

Sneak is primarily a movement and edge-control state, not just a cosmetic stance.

---

## Flying Mode

Flying mode is a controlled gameplay/debug-style movement mode for cases where flight is enabled.

This is not the same as an unrestricted editor noclip camera unless explicitly configured that way.

### Activation

Flying may be activated by gameplay rules or a controller toggle.

The exact permission model is gameplay-defined, but the controller must support:

- enter flying mode
- exit flying mode

### Behavior

In flying mode:

- gravity does not pull the player down
- vertical movement is explicitly controlled
- collision may remain enabled unless gameplay chooses otherwise

Baseline flying controls:

- move input controls horizontal flight relative to camera yaw
- jump moves upward
- sneak moves downward

### Camera Influence

Flying direction may be defined in one of two ways:

1. horizontal movement still uses yaw only
2. forward movement follows full camera facing direction

For Arvane’s baseline, prefer:

- **yaw-relative horizontal movement**
- separate explicit vertical ascend/descend controls

This keeps flight predictable and easy to control in a block world.

---

## Movement Constants

All movement-related values must be centralized in clearly named gameplay tuning constants.

Do not scatter literals across controller or physics code.

Examples include:

- walk speed
- sprint speed
- sneak speed
- jump impulse
- gravity scale by mode
- air control factor
- fly speed
- fly vertical speed
- mouse sensitivity
- reach distance
- eye height
- hotbar slot count

The controller implementation must reference named constants/config, not ad hoc numbers.

---

## Interaction Model

The player can interact with the world through the currently selected item and target selection.

The interaction system must distinguish between:

- primary interact
- secondary interact
- continuous hold
- single press

Examples:

- mining / breaking
- placing blocks
- using items
- activating blocks
- picking blocks in creative/debug contexts

The controller should only generate interaction intent.
Actual results are resolved by gameplay systems.

---

## Block Targeting

### Purpose

Block targeting identifies what the player is aiming at for interaction.

The baseline targeting model uses a ray cast from the player camera.

### Ray Origin

The targeting ray originates from the camera eye position.

### Ray Direction

The ray direction follows the current camera forward direction.

### Reach Distance

Targeting uses a fixed maximum reach distance defined in gameplay constants.

This must not be hard-coded in multiple places.

Examples of systems that use reach distance:

- block breaking
- block placement
- block interaction
- entity interaction, if applicable

If different interaction types later need different ranges, those should still be defined centrally.

### Targeting Result

A targeting query should return enough information to support interaction resolution.

At minimum, a block hit should include:

- hit kind
- targeted block position
- hit face
- hit point or parametric distance
- hit distance from eye

If entity targeting is added, a common hit result abstraction should support both block and entity hits.

### Selection Rules

Targeting should choose the nearest valid hit along the ray.

Opaque and interactable voxel geometry must participate consistently with the collision/shape definitions used by gameplay.

This means targeting should not invent a separate block shape interpretation that disagrees with physics or placement rules.

### Visual Feedback

The targeted block may be highlighted by rendering.

If highlight rendering is used:

- it must reflect the resolved targeting result
- it must not guess independently from a separate approximate method

---

## Reach Distance

Reach distance defines how far the player can interact with world targets.

Baseline expectations:

- it is a named gameplay constant
- it applies consistently to targeting-based interactions
- client and server must agree on the effective rules
- authoritative validation must not trust arbitrary client reach

If gameplay later distinguishes between survival/creative/admin reach, those are still centrally defined policy values.

---

## Hotbar Model

### Purpose

The hotbar exposes a small set of quickly selectable item slots.

The player controller only cares about:

- current selected hotbar slot
- slot change input
- mapping held-item use to the selected slot contents

### Slot Count

Hotbar slot count must be defined explicitly and treated as gameplay-configurable.

The baseline voxel-game assumption is a fixed visible hotbar with numbered slots.

### Selection Inputs

Hotbar selection must support:

- direct slot select via number keys
- next slot via scroll forward
- previous slot via scroll backward

### Wraparound

Hotbar scrolling should wrap around by default.

Example:

- scrolling forward from last slot selects first slot
- scrolling backward from first slot selects last slot

### Selection State

The selected hotbar slot is part of the player state and must be simulation-visible.

### Empty Slot Behavior

If the selected slot is empty:

- interaction logic still runs through normal priority rules
- item-specific behavior simply resolves to none

The controller must not special-case empty slots into entirely separate logic paths.

---

## Interaction Priorities

Interaction priorities must be explicit so the agent does not invent inconsistent “use” behavior.

When the player issues an interaction, resolution should follow a clear order.

## Primary Interaction

Primary interaction is generally the destructive or direct action.

Baseline examples:

- begin block breaking
- attack entity
- use tool in a primary way

Recommended baseline priority for primary interaction:

1. valid entity target requiring primary action
2. valid block target requiring primary action
3. no-op if nothing valid is targeted

Exact combat/entity rules can be refined later, but the ordering must remain explicit.

## Secondary Interaction

Secondary interaction is generally the contextual/use/place action.

Recommended baseline priority:

1. if selected item has a valid use on the targeted block face or target context, perform item use
2. otherwise if targeted block has a valid interact behavior, perform block interaction
3. otherwise if selected item can be placed into the world, attempt placement
4. otherwise no-op

This ordering is important.

It gives held items first opportunity to define contextual use while still allowing blocks to expose interact behavior when the item does not consume the action.

### Placement Rules

For block placement:

- placement should usually target the face of the currently targeted block
- resulting placement position is derived from hit face
- placement validity must be checked against world/block rules
- placement must not succeed inside invalid occupied space

### Mining / Break Hold

Breaking a block is typically a hold-driven action.

Baseline expectations:

- primary press begins break attempt if target is valid
- continued hold may continue break progress
- releasing primary cancels active break attempt unless gameplay rules specify otherwise
- changing target should reset or revalidate break progress

---

## Item Use and Controller Responsibility

The player controller does **not** decide gameplay outcomes like:

- whether a block can be broken
- whether an item can be placed
- whether a door opens
- whether a tool is effective

The player controller only provides:

- target selection
- selected slot
- interaction intent
- movement/look intent

Gameplay systems resolve actual outcomes.

This separation is important for clean architecture and for server-authoritative behavior.

---

## Input Contexts

The input system should support multiple active contexts, such as:

- gameplay world control
- inventory/menu UI
- chat input
- console/debug overlay

Context routing rules must be explicit.

Examples:

- while chat is focused, movement keys do not move the player
- while inventory is open, mouse controls UI rather than camera
- debug overlays should not accidentally consume gameplay input unless intended

This prevents input conflicts and “stuck between UI and gameplay” behavior.

---

## Controller State

The player controller should maintain explicit state for at least:

- yaw
- pitch
- movement mode
- selected hotbar slot
- active break target if applicable
- previous tick input snapshot if needed for transitions

This state should be small, explicit, and easy to serialize or inspect in debug tooling.

---

## Networking Considerations

The input/player model must be compatible with server-authoritative simulation.

Implications:

- the client sends intent, not final truth
- movement and interaction requests may be predicted locally
- server validates reach, collision legality, and interaction legality
- the controller must not rely on hidden client-only state for core gameplay correctness

For example:

- block targeting may be computed client-side for responsiveness
- server must still validate the target and reach before accepting an action

---

## Debug and Tooling Expectations

The engine should expose enough debug visibility to inspect controller behavior.

Useful debug outputs include:

- current movement mode
- grounded state
- sprint/sneak state
- look yaw/pitch
- selected hotbar slot
- current target block
- current reach hit distance
- active break state

This is especially useful for tuning “feel” and resolving client/server disagreement.

---

## Baseline Intended Feel

The intended default feel for Arvane is:

- responsive first-person camera
- deliberate voxel-style grounded movement
- sharp block targeting
- predictable hotbar behavior
- explicit interaction precedence
- gameplay controller first, debug camera second

The default player should feel like they are inhabiting a world with rules, not moving a spectator camera through a rendering demo.

---

## Required Invariants

Implementations must preserve the following invariants:

- gameplay code consumes abstract actions, not raw key codes
- simulation consumes stable per-tick command snapshots
- camera pitch is clamped
- movement intent is relative to player look yaw
- reach distance is centrally defined
- hotbar selection is explicit player state
- interaction ordering is defined and stable
- controller logic does not directly decide gameplay outcomes that belong to world/game rules
- control constants are centralized, not hard-coded across systems

---

## Player-Owned Camera Contract

The camera used for normal gameplay is owned by the local player controller on the client.

The server owns authoritative player simulation state, but the client owns camera presentation.

This distinction is important because the camera must feel responsive locally while still deriving from authoritative or reconciled player state.

## Camera Ownership

The client-side player controller owns:

* camera position used for rendering
* camera rotation used for rendering
* local pitch/yaw input accumulation
* view bob or camera presentation effects
* interpolation between simulation states
* local-only camera smoothing
* camera mode selection
* block targeting ray origin and direction

The server owns:

* authoritative player position
* authoritative player velocity
* authoritative player world ID
* authoritative player collision state
* authoritative player game mode
* authoritative permission to interact with the world

The camera must not be treated as a server-authoritative entity.

The player entity is authoritative. The camera is a local presentation view derived from the player entity.

## Camera State Inputs

The gameplay camera may use the following inputs:

```text
authoritative or predicted player transform
local mouse/gamepad look input
client-side interpolation state
camera mode
player eye height
player stance
world ID
partial frame alpha
```

The renderer should receive a finalized camera snapshot for the frame.

The renderer should not query player input directly.

The renderer should not query mutable player controller state directly.

## Camera Snapshot

Each rendered frame should use an explicit camera snapshot.

A camera snapshot should include:

```text
CameraSnapshot
    world_id
    position
    rotation
    view_matrix
    projection_matrix
    near_plane
    far_plane
    field_of_view
    viewport_size
    partial_tick_alpha
```

The exact structure may evolve, but the boundary should remain stable:

```text
player controller / prediction / interpolation
        ↓
camera snapshot
        ↓
render frame
        ↓
renderer
```

The renderer consumes the camera snapshot as immutable frame data.

## Eye Position

The camera position should normally be derived from the player entity position plus an eye-height offset.

Eye height should come from player movement or stance constants, not scattered hard-coded values.

Examples of stance-dependent camera inputs:

```text
standing_eye_height
sneaking_eye_height
swimming_eye_height
flying_eye_height
```

These values should be centralized in player movement/controller configuration.

## Camera Rotation

Camera yaw usually affects the player’s facing direction.

Camera pitch usually affects only the view direction and interaction ray.

The server may need the player’s intended look direction for validation, interaction, or entity aiming, but the server does not need to own frame-by-frame camera presentation details.

The client may send compact look direction or input commands as part of player command replication.

## Interaction Ray

Block and entity targeting should use the camera snapshot from the current client frame.

The interaction ray should be derived from:

```text
camera world position
camera forward direction
configured reach distance
current world ID
```

Client-side targeting may be used for responsiveness and preview rendering.

Server-side validation must still confirm that interactions are legal.

The server should validate:

* player world
* player position
* reach distance
* target block/entity
* line-of-sight rules, if required
* interaction permissions
* held item rules

The client may predict the result of interactions, but the server remains authoritative.

## Camera Modes

The system should support multiple camera modes without changing renderer ownership.

Initial modes may include:

```text
first_person
third_person_back
third_person_front
free_debug
```

Normal gameplay uses `first_person` unless configured otherwise.

Debug/free cameras should be clearly separated from player-owned gameplay cameras.

A debug camera may bypass normal player movement, but it must not accidentally become the authoritative player transform.

## Prediction and Reconciliation

The camera should follow predicted or interpolated local player state to avoid visible latency.

When server reconciliation adjusts the player entity, camera correction should avoid harsh snapping where possible.

Acceptable techniques include:

* short smoothing windows
* interpolation from previous corrected state
* separating visual camera smoothing from collision body correction
* applying corrections to the player body while preserving local look input

Camera smoothing must not hide serious simulation errors during debugging. Debug builds should provide a way to inspect raw authoritative and predicted positions.

## World Changes

When the player changes worlds, the camera snapshot world ID must change with the player session.

The renderer must not render the camera against the wrong world.

World transfer should reset or invalidate:

* old world camera-relative chunk visibility
* old world targeting ray results
* old interpolation buffers
* stale render frame references

The camera may preserve look rotation across world transfers unless the server or game rules request a different orientation.

## Threading and Lifetime

Camera state belongs to the client gameplay/input layer.

Render frames receive copied camera snapshots.

The renderer must not store raw pointers or references to mutable camera/controller objects.

If rendering and simulation/input run on different threads, camera snapshots must be copied or synchronized through an explicit handoff.

---

## Minimum Baseline Behavior

At minimum, a valid implementation must support:

- keyboard movement input abstraction
- mouse look
- first-person camera with yaw/pitch
- walking
- jumping
- flying mode
- block ray targeting
- fixed reach distance
- hotbar slot selection by keys and scroll
- primary and secondary interaction intents
- explicit interaction priority order

Anything beyond this can be layered on later, but this baseline must remain stable.