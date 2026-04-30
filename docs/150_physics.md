# Physics and Collision

This document defines the core physics and terrain collision model for Arvane.

Its purpose is to make movement, collision response, gravity, and player-ground interaction explicit and stable across the engine.

Without this document, implementations tend to drift into inconsistent behavior such as:

- camera-style free movement instead of game-style grounded movement
- unstable corner and slope behavior
- inconsistent step-up handling
- incorrect ledge behavior while sneaking
- physics code that cannot be shared between client prediction and server authority

This file defines the baseline rules for:

- collision primitives
- terrain collision queries
- movement resolution
- gravity and grounded behavior
- step-up and sneak behavior
- fixed-timestep simulation

---

## Goals

The physics system must prioritize:

- predictable movement
- stable terrain interaction
- clean integration with voxel worlds
- behavior suitable for server-authoritative simulation
- logic that feels like a game character controller, not a noclip camera

The default movement model is intentionally simple and robust.

Arvane does **not** begin with a full rigid-body physics system.

It begins with:

- entity kinematic movement
- voxel terrain collision
- deterministic-enough fixed-tick simulation
- explicit grounded movement rules

More advanced behavior such as pushed bodies, dynamic rigid bodies, moving platforms, or continuous collision against arbitrary convex shapes can be added later without redefining the baseline terrain controller.

---

## Simulation Model

### Fixed Tick Rate

Physics simulation runs on a fixed timestep of:

- **60 Hz**
- **1 tick = 1 / 60 second**

All gameplay-relevant movement and collision simulation must run on this fixed tick.

This includes:

- gravity
- jump impulses
- grounded checks
- terrain collision resolution
- step-up logic
- sneak edge behavior
- velocity integration
- friction / drag rules for grounded movement

Rendering must not drive simulation.

Rendering may interpolate between previously simulated states, but physics state advances only on fixed simulation ticks.

### Why 60 Hz

Arvane uses 60 Hz because it provides:

- better movement feel than lower-frequency simulation
- more stable gravity and grounded transitions
- smoother collision response for player-sized entities
- simpler integration with modern client framerates
- a better baseline for game-feel than a coarse 20 Hz controller

This is especially important for:

- walking along voxel edges
- jumping
- stepping over terrain
- future combat / knockback / projectiles

---

## Scope of the Physics System

The baseline physics system is responsible for:

- integrating entity velocity
- resolving entity movement against terrain
- maintaining grounded state
- applying gravity
- handling jump and fall motion
- supporting player-specific behaviors such as step-up and sneak edge prevention

The baseline physics system is **not** responsible for:

- general rigid body stacks
- impulse solving between arbitrary dynamic bodies
- soft bodies
- ragdolls
- arbitrary physics joints
- high-speed continuous collision detection for all entity types

Those systems may be added later as separate layers.

---

## Coordinate and Unit Conventions

Physics follows the conventions defined in [core types and conventions](60_core_types_and_conventions.md).

Baseline assumptions:

- world is 3D
- Y is up
- one block occupies a 1×1×1 world-space unit
- entity transforms are represented in world space
- simulation uses fixed-step integration

Physics logic must use the engine’s canonical numeric types.

Collision and movement code must not invent alternate coordinate conventions.

---

## Collision Primitives

### Supported Baseline Primitives

The baseline physics system supports the following collision primitive:

- **AABB** (axis-aligned bounding box)

This is the default primitive for:

- players
- most world entities
- item pickups
- simple mobs
- temporary gameplay objects that do not require rotated collision

### Why AABB

AABBs are chosen because they are:

- simple
- fast
- stable against voxel terrain
- easy to query against block collision shapes
- appropriate for server-authoritative simulation

Voxel worlds are naturally aligned to an axis-aligned collision model.

This makes AABB-vs-voxel collision the default and preferred terrain interaction path.

### Entity Collision Shape

Each collidable entity must expose a collision shape descriptor that includes at minimum:

- collision primitive type
- local bounds
- whether the entity collides with terrain
- whether the entity participates in entity-vs-entity collision
- step-up capability if applicable

For baseline entities, the collision primitive is an AABB defined by:

- half extents or min/max offsets in local space
- a world-space origin convention
- a resolved world-space AABB each tick

### Player Collision Primitive

The player uses an **upright AABB**.

The player collision box:

- remains axis-aligned at all times
- does not rotate with view direction
- is resolved against terrain using per-axis movement

This is deliberate. View direction and collision shape are independent.

### Future Extension

Additional primitives may be added later, such as:

- capsule
- sphere
- swept volume tests
- custom authored shapes

However, those do not replace the baseline AABB terrain contract.

The engine should assume AABB support is always present.

---

## Terrain Collision Model

### Terrain as Voxel Collision Geometry

Terrain collision is based on voxel block collision data.

Each block state defines collision information through the block/state registry.

At minimum, a block state must expose whether it is:

- empty / non-colliding
- fully solid cube
- partially colliding through one or more AABBs

This allows support for shapes such as:

- full blocks
- slabs
- stairs
- panes
- fences
- plants with no collision
- custom modded voxel-aligned shapes

### Collision Shape Source

Terrain collision queries must use block **state** collision data, not only block ID.

This is important because collision may vary by state, for example:

- stair orientation
- slab top vs bottom
- open vs closed shapes
- custom authored block variants

### Collision Shape Representation

The baseline terrain collision representation is:

- zero or more local-space AABBs per block state

A block state with no collision boxes is non-solid for terrain collision.

A full cube block is represented as one unit cube AABB.

More complex states may expose multiple AABBs.

This keeps the terrain collision system simple and compatible with meshing/state definitions.

### Solid vs Non-Solid

For physics purposes, terrain is not treated as a single boolean “solid” everywhere.

Instead, movement code resolves against the actual collision boxes returned by block states.

However, convenience helpers may still exist for common queries such as:

- is full solid cube
- blocks movement
- blocks light
- supports stepping
- can stand on top

These helpers must remain derived from registry/state data, not ad hoc hardcoded rules.

---

## Terrain Collision Queries

### Broad Query Model

Entity movement resolution must query the world for relevant nearby block collision geometry.

The broad-phase terrain query should:

1. compute the swept or expanded region the entity may enter during the tick
2. identify all touched voxel cells
3. gather collision boxes for colliding block states in that region
4. test movement against those shapes

### Query Ownership

Physics must not read raw chunk internals directly unless it is operating inside world-owned simulation code with safe access.

Preferred model:

- physics depends on a collision query interface
- world provides voxel collision data through that interface
- collision queries return the block collision shapes relevant to a requested region

This prevents physics from depending directly on chunk container implementation details.

### Missing Data / Unloaded Chunks

Collision behavior at chunk boundaries and unloaded regions must be explicit.

Default rule:

- **unloaded or unavailable terrain is treated as solid for authoritative simulation unless a higher-level system explicitly says otherwise**

This prevents:

- falling through chunk holes
- stepping into missing terrain during streaming
- disagreement between server safety and world availability

Client-side prediction may use temporary approximations, but authoritative world simulation must use safe rules.

### Off-Thread Safety

Background systems must not query mutable live world collision state directly.

If collision is needed off-thread, it must use one of:

- immutable snapshots
- copied boundary data
- explicit read-safe query layers provided by world ownership rules

The live movement simulation itself should normally occur on the owning world thread.

---

## Movement Resolution Model

### Baseline Resolution Strategy

Movement resolves using **discrete per-axis AABB collision resolution**.

Default order:

1. X movement
2. Y movement
3. Z movement

The axis order must be fixed and consistent.

This keeps movement behavior predictable and prevents subtle divergence between implementations.

### Per-Axis Resolution

For each axis:

1. compute desired delta for that axis
2. test the entity AABB against candidate terrain collision shapes
3. clamp the delta to the furthest non-penetrating value
4. move the entity by the clamped delta
5. if clamped, record collision on that axis

This repeats for each axis in order.

### Why Per-Axis Resolution

Per-axis resolution is chosen because it is:

- robust for voxel terrain
- easy to reason about
- easy to debug
- consistent with classic block-game movement
- sufficient for upright AABB actors

It also naturally supports:

- sliding along walls
- standing on floors
- hitting ceilings
- corner interaction

### Collision Result

Movement resolution must produce a structured result containing at least:

- attempted delta
- applied delta
- whether collision occurred on X
- whether collision occurred on Y
- whether collision occurred on Z
- whether downward movement was blocked
- whether the entity ended grounded

This result is important for higher-level movement systems.

### Penetration Policy

The baseline system is **preventive**, not penetration-tolerant.

Normal movement should stop before interpenetration occurs.

If an entity begins a tick already intersecting terrain due to teleportation, chunk edits, or world corrections, a separate recovery path may resolve this. That recovery path must not redefine ordinary movement behavior.

---

## Velocity Integration

### Velocity-Based Motion

Entities move by integrating velocity on each fixed tick.

Baseline flow:

1. gather control/input intent
2. update desired horizontal velocity or acceleration
3. apply gravity and other forces
4. compute movement delta from velocity
5. resolve movement against terrain
6. update final velocity based on collision results
7. update grounded state

### Collision Response and Velocity

When movement is blocked on an axis, velocity on that axis must be adjusted accordingly.

Baseline rule:

- if movement is clipped on X, X velocity becomes 0
- if movement is clipped on Y, Y velocity becomes 0
- if movement is clipped on Z, Z velocity becomes 0

This applies unless a higher-level controller explicitly replaces the response for special cases.

Examples:

- hitting the ground clears downward velocity
- hitting the ceiling clears upward velocity
- walking into a wall clears lateral velocity on that axis

---

## Grounded State

### Definition

An entity is grounded when:

- it is supported by blocking terrain beneath it
- and its downward motion was prevented by terrain collision during the current movement step or confirmed by a stable ground check

Grounded must not be inferred solely from vertical velocity being zero.

### Ground Contact Rules

Grounded state should reflect whether the entity is standing on valid support geometry.

Grounded is important for:

- jumping
- friction
- step-up behavior
- sneak edge prevention
- fall state transitions

### Support Geometry

A surface counts as ground support when:

- the entity can stand on it
- it blocks downward motion
- it is not flagged as non-supporting collision

This distinction allows future special blocks such as:

- climbables
- fluids
- bounce surfaces
- soft surfaces
- non-standing triggers

---

## Gravity Rules

### Default Gravity

Gravity is applied every fixed physics tick to entities affected by gravity.

Gravity acts on the negative Y axis.

### Gravity Behavior

Baseline gravity rules:

- gravity applies while not grounded
- gravity does not continue accumulating downward motion while firmly grounded
- jump or knockback can override grounded vertical state until terrain contact is re-established

### Terminal Velocity

The physics system should support a configurable maximum downward velocity.

This prevents runaway fall speeds and keeps collision stable.

### Floating / Special Entities

Not all entities are affected by gravity.

Entity physics configuration must support:

- affected by gravity
- ignores gravity
- custom gravity scale

This allows support for:

- players
- mobs
- projectiles
- floating entities
- creative / noclip modes

---

## Jumping

### Jump Model

Jumping is an impulse or immediate vertical velocity assignment applied when:

- the entity is grounded
- jump input is valid
- the movement controller permits jumping

### Ground Exit

Once jump velocity is applied:

- grounded becomes false
- gravity resumes on following ticks
- upward motion continues until collision or gravity reversal dominates

### Ceiling Contact

If upward motion is blocked by terrain:

- upward velocity is cleared
- the entity begins falling on subsequent ticks

---

## Friction and Horizontal Motion

### Grounded Motion

Grounded horizontal movement should feel responsive and controlled.

The movement controller may use:

- acceleration toward a target velocity
- grounded friction / deceleration
- surface-dependent modifiers later

### Air Motion

Air movement should be more limited than grounded movement by default.

The baseline physics layer does not hardcode player tuning constants, but it must support a controller model where:

- grounded acceleration differs from air acceleration
- grounded deceleration differs from air deceleration
- horizontal control persists in air at reduced influence

These values belong to movement/controller configuration, not the collision core.

---

## Step-Up Behavior

### Purpose

Step-up allows the player to move smoothly over small terrain height changes without jumping.

Without it, voxel movement feels snaggy and unnatural.

### Applicability

Step-up is primarily intended for:

- players
- possibly mobs or similar walking actors

It is not automatically applied to all entities.

### Step Height

Collidable entities that support stepping must define a maximum step height.

For the player, this is a controller-defined value.

### Step-Up Conditions

Step-up may occur when:

- horizontal movement is blocked by terrain
- the obstruction is low enough to step onto
- there is enough free space above the higher position for the entity AABB
- the resulting position has valid support or is otherwise acceptable per controller rules

### Step-Up Resolution Model

Preferred step-up strategy:

1. attempt normal horizontal movement
2. if blocked, test whether the entity can be raised by at most step height
3. test whether, from that raised position, the horizontal movement can succeed
4. test whether the entity can then settle safely onto the stepped surface
5. accept the stepped movement only if the path is valid and non-penetrating

This must be treated as a controlled movement feature, not a general teleport.

### Stability Requirement

Step-up must not allow:

- clipping into ceilings
- climbing obstacles taller than allowed step height
- stepping through diagonal corner gaps that do not actually fit the actor
- bypassing sneak edge restrictions where they should apply

---

## Sneak Behavior

### Purpose

Sneak behavior prevents the player from unintentionally walking off block edges while sneaking.

This is a gameplay movement rule layered on top of terrain collision.

### Scope

Sneak edge prevention applies only to entities/controllers that opt into it, typically the player.

It is not a universal physics rule.

### Sneak Edge Rule

When sneak is active and the entity is grounded:

- horizontal movement that would leave the entity without supporting ground beneath its standing footprint is restricted

In practice, this means the player can approach a ledge but not walk off it while sneaking.

### Support Check

Sneak logic must perform a support test against the entity’s post-move standing area.

If the attempted horizontal move would result in insufficient support beneath the entity, that move is clamped or rejected.

### Grounded Requirement

Sneak edge prevention only applies while grounded.

It does not prevent falling once support is already lost, and it does not apply as an air-control mechanic.

### Interaction With Step-Up

Sneak and step-up must coexist cleanly.

The controller should not:

- block legitimate small upward steps while sneaking
- allow edge bypass through step-up side effects
- produce jitter when moving along ledges

---

Here’s a clean, drop-in section for your physics doc that stays consistent with your existing movement + AABB model and avoids introducing weird coupling:

---

## Sprinting

Sprinting is a movement modifier applied to player-controlled entities that increases horizontal movement speed while active.

It does **not** change the core collision or physics model. Instead, it scales the intended movement velocity before collision resolution.

---

### Activation Rules

Sprinting is controlled at the **player controller/input layer**, not the physics system.

The physics system receives sprinting as a simple boolean flag:

```cpp
bool is_sprinting;
```

Typical activation conditions (implemented outside physics):

- Forward movement input is active
- Sprint key is held (or toggled)
- Player is not obstructed in a way that prevents sprinting
- Player has sufficient stamina (if stamina is implemented later)

Physics does **not** validate these rules.

---

### Movement Behavior

When sprinting is active:

- Horizontal movement speed is increased by a configurable multiplier
- Vertical motion (gravity, jumping, falling) is unchanged

Example:

```cpp
float base_speed = player.walk_speed;
float speed = is_sprinting
    ? base_speed * player.sprint_multiplier
    : base_speed;
```

This `speed` is used to compute the desired movement vector before collision resolution.

---

### Collision Interaction

Sprinting does **not** modify collision handling:

- AABB collision resolution remains identical
- Per-axis movement stepping is unchanged
- No tunneling behavior should be introduced

If higher speeds increase the risk of tunneling:

- The physics system should rely on existing safeguards (e.g., smaller step sizes or swept checks if implemented)

---

### Directional Constraints

By default:

- Sprinting only applies when moving **forward**

Optional (controller-level decision):

- Disable sprinting when moving backward or strafing
- Reduce multiplier when not moving forward

Physics remains agnostic to direction—it only consumes the final velocity.

---

### Jump Interaction

Apply a small horizontal boost when jumping while sprinting

This should be implemented in the player controller, not physics.

---

### Extensibility

Sprinting is designed to compose cleanly with future systems:

- Stamina systems (enable/disable sprint)
- Status effects (speed boosts/slows)
- Surface modifiers (ice, mud, etc.)

All such systems should modify the **input velocity**, not the collision system.

---

### Summary

- Sprinting is a **velocity modifier**, not a physics rule
- Physics remains deterministic and unchanged
- All logic lives in the controller layer except final velocity application

---

Drop-in section:

---

## Sneaking / Crouching

### Overview

Sneaking is a player movement state that reduces movement speed, shortens the player collision box, lowers the camera, and enables safe-walk behavior near ledges.

Unlike sprinting, sneaking affects both the controller and the physics representation because the player’s effective collision height changes.

---

### Responsibilities

The **player controller** owns:

- Sneak input state
- Whether the player wants to sneak
- Sneak speed multiplier
- Camera target height
- Safe-walk movement intent

The **physics system** owns:

- Applying the shorter collision box
- Validating whether the player can safely stand back up
- Resolving movement against blocks using the active AABB

---

### Collision Box Height

When sneaking is active, the player AABB height is reduced.

Example configuration:

```cpp
struct PlayerPhysicsConfig {
    float standing_height = 1.8f;
    float sneaking_height = 1.5f;
    float width = 0.6f;
};
```

The bottom of the AABB remains anchored at the player’s feet. Only the top of the AABB changes.

This means:

- Sneaking does not move the player downward
- The player can fit under lower ceilings
- Standing back up requires a clearance check

---

### Standing Up Clearance

When the player releases sneak, physics must check whether the standing-height AABB would collide with the world.

If the standing AABB overlaps solid blocks:

- Keep the player sneaking
- Keep the shorter AABB
- Keep the lowered camera target

If the standing AABB is clear:

- Restore standing height
- Restore normal movement speed
- Restore standing camera target

The controller may request unsneak, but physics has final authority over whether it is valid.

---

### Camera Height

Sneaking lowers the player camera.

The camera should smoothly interpolate toward the sneaking eye height instead of snapping instantly.

Example configuration:

```cpp
struct PlayerCameraConfig {
    float standing_eye_height = 1.62f;
    float sneaking_eye_height = 1.35f;
    float eye_height_lerp_speed = 12.0f;
};
```

Camera height is visual only and must not drive collision behavior.

The collision box determines physics; the camera follows the resolved player state.

---

### Movement Speed

Sneaking reduces horizontal movement speed using a configurable multiplier.

Example:

```cpp
float speed = is_sneaking
    ? walk_speed * sneak_speed_multiplier
    : walk_speed;
```

Vertical movement, gravity, and jumping are unchanged unless explicitly handled by the player controller.

---

### Safe Walk

Safe walk prevents a sneaking player from walking off ledges.

When sneaking and grounded, horizontal movement should be clamped so the player remains supported by solid ground beneath their AABB.

This behavior is part of player movement resolution, not world collision itself.

Safe walk should:

- Apply only while sneaking
- Apply only while grounded
- Affect horizontal movement only
- Prevent movement that would leave the player unsupported
- Still allow falling if the block beneath the player is removed

---

### Safe Walk Support Check

Before accepting horizontal movement while sneaking, test whether the resulting AABB would still have support below it.

A simple implementation:

1. Compute the candidate horizontal movement.
2. Move the player AABB horizontally by that amount.
3. Check a small distance below the candidate AABB feet.
4. If no supporting solid block exists, reduce or cancel the movement.

The support check should use a small downward epsilon rather than relying on exact floating-point contact.

Example:

```cpp
constexpr float support_check_depth = 0.05f;
```

---

### Axis Clamping

Safe walk should clamp movement per axis instead of simply cancelling all movement.

For example, if diagonal movement would walk off a ledge:

- Try X movement independently
- Try Z movement independently
- Keep any axis that remains supported
- Reject the axis that would leave support

This preserves smooth edge movement and allows sliding along ledges.

---

### Summary

- Sneaking shortens the player AABB
- The feet remain anchored; only the top changes
- Standing back up requires a clearance check
- Camera height follows the resolved sneak state
- Safe walk clamps grounded horizontal movement near ledges
- Safe walk is controller/player movement behavior, not a general collision rule

---

## Terrain Edges and Corner Behavior

### Wall Sliding

When movement along one horizontal axis is blocked but the other is not, the entity should continue along the unblocked axis.

This produces natural sliding against walls and corners.

### Corner Consistency

The fixed axis order must be used consistently so the same terrain produces the same resolution behavior every tick.

If special handling is added later for corner smoothing, it must remain compatible with the baseline per-axis contract.

### Narrow Gaps

An entity may only enter a gap if its AABB fits without penetration after full movement resolution.

The engine must not allow visual-only or epsilon-based slipping through impossible spaces.

---

## Collision Tolerances

### Numerical Robustness

Physics code may use small internal tolerances to avoid floating-point instability or fixed-point edge chatter.

However, tolerances must not become gameplay-visible cheats.

The collision system must not:

- allow walking through thin walls
- allow standing while actually unsupported
- create inconsistent ground contact from frame to frame

### Stable Contact

Repeated standing on flat terrain should remain stable.

Entities should not accumulate tiny downward corrections each tick while idle on solid ground.

---

## Entity vs Entity Collision

### Baseline Policy

Entity-vs-entity collision is not required for the baseline terrain controller.

The initial focus is terrain collision.

If entity-vs-entity collision is enabled later, it should be treated as a separate layer with explicit policy, such as:

- pushable
- overlap-only trigger
- fully non-colliding
- special-case player-body interaction

This document does not require full dynamic body solving between entities.

---

## World Edits and Physics Interaction

### Block Changes

When terrain changes near an entity, future physics ticks must use the updated collision data.

This includes:

- placing blocks
- breaking blocks
- world generation changes
- structure placement
- fluid/interactive block state changes if they affect collision

### Invalid Embedded States

If a terrain edit places collision into an entity’s occupied space, the engine must define a correction policy elsewhere, such as:

- prevent placement
- push entity out
- allow temporary overlap and resolve next tick
- server-authoritative correction

The baseline expectation for normal gameplay is to avoid creating invalid intersections where possible.

---

## Networking and Authority

### Authoritative Simulation

Physics and collision are part of authoritative world simulation.

The server or owning world simulation determines the true result of:

- movement
- collision
- grounded state
- jumping
- terrain interaction

### Client Prediction

Clients may run the same movement rules for prediction.

This document is intentionally explicit so the same logic can be shared or mirrored.

Prediction correctness depends on matching:

- tick rate
- axis order
- step-up behavior
- sneak edge rules
- terrain collision queries
- gravity rules

---

## Threading Model

### Ownership

Live physics simulation should run on the owning world/simulation thread.

Physics must not mutate chunk data directly.

Physics reads terrain collision through world-owned query interfaces.

### Off-Thread Restrictions

The following are not safe by default off-thread:

- mutating entity physics state
- reading mutable live chunk state directly
- resolving movement against world data without explicit read-safe access

Background or async systems may only perform physics-related work using:

- snapshots
- copied data
- immutable query layers
- explicit world-approved jobs

This mirrors the ownership model used by world, meshing, and lighting systems.

---

## Required World Query Interface Expectations

The physics system requires the world layer to support collision-related queries roughly equivalent to:

- query block state at world position
- gather collision AABBs intersecting a world-space region
- determine whether support exists below a region
- determine whether queried terrain data is loaded / valid

Exact API shapes are implementation details, but the semantic contract must remain stable.

Physics should not need to know:

- chunk container layout
- storage compression format
- region paging internals
- meshing state

It only needs collision-relevant world answers.

---

## Default Player Controller Contract

The default player controller assumes:

- collision primitive is an upright AABB
- gravity is enabled
- movement is resolved per axis against voxel collision
- grounded motion differs from air motion
- step-up is enabled
- sneak edge prevention is enabled while sneaking and grounded
- jump requires grounded state
- simulation runs at 60 Hz fixed tick

This is the baseline “game-feeling” controller for Arvane.

It should feel like a grounded voxel character, not a free camera.

---

## Non-Goals

This document does not define:

- exact player width and height
- exact jump strength
- exact run speed
- exact friction coefficients
- exact knockback formulas
- swimming physics
- ladders and climbables
- rigid-body stacks
- vehicle physics

Those may be defined in later gameplay/controller documents.

This file defines the **physics and collision contract**, not final movement tuning.

---

## Invariants

The following invariants must hold:

- physics simulation advances on a fixed 60 Hz tick
- baseline terrain collision uses voxel block-state collision data
- baseline collidable entity primitive is AABB
- player collision uses an upright AABB
- movement resolves per axis in a fixed order
- terrain collision queries operate on block state collision shapes, not just block IDs
- gravity acts downward on Y
- grounded state is derived from terrain support / blocked downward movement
- step-up is an explicit controller feature, not accidental behavior
- sneak edge prevention is an explicit controller feature, not a side effect
- physics depends on world collision queries, not raw chunk internals
- physics and movement behavior must use named constants or configuration, not undocumented hard-coded values
- no important physics behavior should depend on undocumented magic numbers embedded directly in movement or collision code.

---

## Configuration and Constants

Physics behavior must not rely on scattered hard-coded numeric values.

Any gameplay-relevant or simulation-relevant constants must be defined through clearly named configuration values or centralized engine constants.

This includes at minimum:

- gravity magnitude
- terminal velocity
- player step height
- jump velocity
- grounded friction
- air acceleration
- grounded acceleration
- collision tolerances
- sneak support thresholds
- default player collision dimensions
- controller-specific movement speeds

### Rules

Physics code must not embed unexplained literals such as:

- movement thresholds
- gravity values
- epsilon values
- step-up heights
- support offsets
- velocity clamps

Instead, such values must come from one of:

- shared physics constants
- entity/controller configuration
- world/gameplay tuning data
- block or material properties where applicable

### Separation of Concerns

The physics engine should distinguish between:

- **engine-level constants**, which define stable simulation behavior
- **controller tuning values**, which define how a specific actor feels
- **block/material-driven values**, which define terrain-dependent behavior

For example:

- collision epsilon is an engine-level constant
- player walk speed is a controller tuning value
- bounce coefficient for a special block is block-driven behavior

### Goals

This rule exists to ensure:

- consistent behavior across server and client prediction
- easier balancing and tuning
- easier testing
- easier modding and gameplay customization
- less risk of subtle desync caused by duplicated literals

---

## Liquid Movement and Swimming Physics

Player movement in liquids uses a dedicated swimming movement mode instead of
treating water as ordinary air movement with extra drag. The goal is to support
Minecraft Aquatic Update-style swimming while keeping the implementation simple,
predictable, and compatible with the existing AABB physics model.

### Liquid Contact State

The physics system should distinguish between these player liquid states:

- `NotInLiquid`
- `PartiallyInLiquid`
- `SwimmingAtSurface`
- `SwimmingUnderwater`

A player is considered in liquid when their movement/collision AABB overlaps a
liquid block volume. A player is considered underwater when the camera/head
sample point is below the liquid surface.

The exact sample points should be centralized in the physics/controller code
rather than scattered across systems.

### Surface Swimming

When the player is in water and their head is above the water surface, they
should float at the surface by default.

While surface swimming:

- The player should not naturally sink.
- The player should not naturally rise past the configured surface float depth;
  by default this keeps the feet about one block below the liquid surface.
- Breath should remain full and should not decrease.
- Gravity should be heavily reduced or disabled while the player is supported by
  the liquid surface.
- Normal movement should feel slower and more buoyant than land movement.
- Jump input swims upward.
- Sneak input intentionally descends into the water.

The player should only transition from surface swimming to underwater swimming
when they intentionally descend, usually by holding the sneak key or otherwise
moving below the surface.

### Underwater Swimming

When the player is underwater, movement should become full 3D swimming movement.

While underwater:

- Jump input swims upward.
- Sneak input swims downward.
- Horizontal movement should work relative to the player view direction.
- Gravity should be replaced by liquid buoyancy and drag.
- Breath should decrease only while the player is underwater.
- Leaving water or returning to the surface should restore breath according to
  the normal breath/air rules.

Underwater movement should still use fixed-tick physics and should not depend on
render frame rate.

### Sprint Swimming / Fast Swim

Holding sprint while swimming enables fast swim mode.

Fast swim behavior depends on whether the player is at the surface or underwater:

- If the player is at the surface and sprinting, they remain at the surface and
  move faster across the water.
- If the player is underwater and sprinting, they move faster through the water
  in the direction they are swimming.
- Sprinting at the surface does not consume breath because the player remains
  above water.
- Sprinting underwater does not by itself restore breath; normal underwater
  breath rules still apply.

Fast swim should be a movement mode modifier, not a separate physics system.

### Swimming AABB

Fast swim uses a different player collision shape to represent the player being
horizontal in the water.

The engine should not rotate the AABB. Instead, it should swap to a wider and
shorter swimming AABB.

For example:

```cpp
enum class PlayerCollisionPose {
    Standing,
    Sneaking,
    Swimming,
};
```

The swimming pose should:

* Reduce player height.
* Increase horizontal length or width enough to represent the player being
  stretched forward.
* Keep collision resolution axis-aligned.
* Be validated before entering the pose, similar to standing/sneaking pose
  transitions.

The player should only enter the swimming collision pose while in liquid and
fast swimming. When leaving fast swim or exiting liquid, the controller should
attempt to restore the normal standing or sneaking pose if there is enough space.

If there is not enough space to restore the taller pose, the player should remain
in the smaller collision pose until the space is clear.

### Pose Transition Safety

Changing the player collision pose must never push the player into solid blocks.

Before changing to a larger or taller AABB, the physics system must check whether
the new AABB would collide with solid terrain. If the target pose is blocked, the
pose change is delayed.

This applies to:

* Sneaking to standing.
* Swimming to standing.
* Swimming to sneaking.
* Any future crawl/prone-style poses.

### Liquid Movement Constants

Liquid movement constants should be centralized with the rest of the player
physics constants.

Suggested constants include:

```cpp
struct PlayerLiquidMovementConstants {
    float surface_swim_speed;
    float underwater_swim_speed;
    float fast_surface_swim_speed;
    float fast_underwater_swim_speed;

    float upward_swim_acceleration;
    float downward_swim_acceleration;

    float liquid_drag;
    float surface_buoyancy;
    float underwater_buoyancy;

    float breath_seconds;
};
```

The exact values should be tuned through gameplay testing, but the behavior rules
above are part of the physics contract.

These constants are configured through the top-level `physics` section of
`engine.yaml`, alongside the existing land movement, collision shape, eye height,
gravity, and reach values:

```yaml
physics:
  walk_speed: 4.5
  sprint_speed: 6.0
  body_height: 1.8
  swimming_body_width: 0.9
  swimming_body_height: 0.6
  surface_swim_speed: 2.5
  underwater_swim_speed: 2.0
  fast_surface_swim_speed: 5.6
  fast_underwater_swim_speed: 5.0
  liquid_drag: 0.8
  surface_float_depth: 1.0
  breath_seconds: 15.0
```

---

## Testing Expectations

Physics and collision should be validated with targeted tests for cases such as:

- falling onto flat ground
- walking into a wall
- sliding along a wall
- jumping into a ceiling
- stepping onto a one-block-like obstacle within step height
- failing to step onto an obstacle above step height
- sneaking toward a ledge without falling
- walking normally off a ledge
- corner traversal near partial blocks
- movement across chunk boundaries
- behavior near unloaded terrain
- standing stability over many ticks without drift

This system is too important to rely only on ad hoc playtesting.

---

## Summary

Arvane physics begins with a simple, explicit, game-first model:

- fixed 60 Hz simulation
- upright AABB entities
- voxel-state-driven terrain collision
- per-axis movement resolution
- gravity and grounded logic
- explicit step-up behavior
- explicit sneak edge prevention

This provides the foundation for a movement system that feels intentional, stable, and suitable for a real voxel game.
