# Rideables And Vehicles

This document describes the V1 rideable mount and vehicle model.

V1 supports:

- `arvane:horse`
- `arvane:oak_boat`
- `arvane:buggy`
- `arvane:plane`
- `arvane:submarine`

V1 deliberately does not include fuel, vehicle damage, vehicle inventory,
constructed vehicles, mesh collision, OBB collision, mounted weapons,
submarine interiors, aircraft interiors, or a generic rigid-body solver.

---

## Runtime Model

Rideables are normal world entities with additional ECS state:

- `RideableComponent`
- `AttachedToComponent`
- `VehiclePhysicsComponent`

`RideableComponent` owns the seat list. Each seat has an id, local transform,
role, camera mode, occupant, and a `controls_vehicle` flag.

`AttachedToComponent` is placed on a mounted passenger entity. The passenger
world transform is derived from the rideable transform plus the seat local
transform. Passengers do not move independently while attached.

`VehiclePhysicsComponent` stores the V1 controller state that is needed for
replication and validation: physics kind, definition id, linear velocity,
angular velocity, and basic support flags. It is not a rigid body.

Important invariants:

- a passenger can occupy at most one seat
- a seat can have at most one occupant
- only the controlling seat occupant can drive the vehicle
- destroyed rideables clear passenger attachments
- mounted passenger transforms are derived from the seat transform
- pressing the `Mount` action mounts the targeted rideable when unmounted and
  requests a dismount when already mounted
- mounted living mounts check terrain support and fall under gravity when they
  leave the ground
- mounted living mounts use their authored `movement.step_height` to step onto
  small supported obstacles, including the built-in horse's one-block step

---

## Controls

Default desktop bindings are `F` for `Mount`, `X` for `VehicleRollLeft`, and
`C` for `VehicleRollRight`.

Plane controls:

- `MoveForward` accelerates.
- `MoveBackward` decelerates.
- `Jump` pitches up.
- `Sneak` pitches down.
- `MoveLeft` and `MoveRight` yaw left/right.
- `VehicleRollLeft` and `VehicleRollRight` roll left/right.

Submarine controls:

- `MoveForward` and `MoveBackward` control throttle.
- `MoveLeft` and `MoveRight` steer.
- `Jump` ascends.
- `Sneak` descends.
- secondary interaction still acts as a brake.

---

## Authoring

Entity definitions remain in:

```text
data/<namespace>/entities.json
```

Vehicle and mount tuning lives in:

```text
data/<namespace>/vehicles.json
```

The built-in pack id is `arvane_base`, but its declared content namespace is
`arvane`, so first-party V1 ids use `arvane:*`.

Vehicle definitions are JSON and use `name` as the stable merge key:

```json
{
  "vehicles": [
    {
      "name": "arvane:oak_boat",
      "kind": "vehicle",
      "physics": "surface_water_vehicle",
      "collision": {
        "type": "aabb",
        "size": [1.4, 0.55, 2.2]
      },
      "seats": [
        {
          "id": "driver",
          "role": "driver",
          "local_position": [0.0, 0.35, 0.0],
          "local_rotation": [0.0, 0.0, 0.0, 1.0],
          "controls_vehicle": true,
          "camera_mode": "third_person_follow"
        }
      ],
      "authority": {
        "max_speed": 5.0,
        "max_acceleration": 10.0,
        "max_deceleration": 12.0,
        "max_angular_speed": 130.0,
        "max_vertical_speed": 3.0,
        "max_step_height": 0.2,
        "requires_ground": false,
        "requires_water": true,
        "min_water_support_samples": 2
      },
      "movement": {
        "acceleration": 6.0,
        "max_speed": 5.0,
        "turn_rate": 90.0,
        "water_drag": 0.88,
        "land_drag": 0.35,
        "float_height_offset": 0.05,
        "buoyancy_snap_speed": 4.0
      }
    }
  ]
}
```

The schema supports multiple seats. V1 content only requires one controlling
seat per built-in vehicle. The accepted camera modes are
`mounted_third_person`, `third_person_follow`, and `vehicle_chase`. The first
plane/submarine pass treats `vehicle_chase` as the existing usable mounted
third-person follow camera.

`turn_rate` and `authority.max_angular_speed` are authored in degrees per
second for readability. The loader converts them to radians per second for
runtime prediction and validation.

Living mounts may author `movement.step_height`; client prediction uses this
height when a grounded mount is ridden into a supported obstacle. The built-in
horse uses `1.0` so it can climb one full block while mounted, with
`authority.max_step_height` allowing a small validation tolerance.

The built-in buggy uses `arvane:animation/automobile`; the client and accepted
server state select its `driving` clip when moving along the buggy's visual
front and its `reverse` clip when moving backward.

### Crash Tuning

Vehicles may opt into mounted crash handling with an optional `crash` object:

```json
{
  "crash": {
    "bump_threshold": 3.0,
    "hard_impact_threshold": 8.0,
    "ejection_threshold": 15.0,
    "destruction_threshold": 20.0,
    "explosion_threshold": 20.0,
    "min_direct_impact_dot": 0.45,
    "event_cooldown_ticks": 30,
    "velocity_retention": 0.08,
    "ejection_velocity_scale": 0.55,
    "ejection_upward_velocity": 5.0,
    "ejection_min_horizontal_speed": 4.0,
    "ejection_max_horizontal_speed": 16.0,
    "ejection_max_vertical_speed": 9.0,
    "remount_cooldown_ticks": 50,
    "damage": {
      "hard_impact": 2.0,
      "ejection": 4.0,
      "destruction": 12.0
    },
    "destroy_on_destruction": true,
    "explode_on_destruction": true
  }
}
```

The server classifies contacts as `None`, `Bump`, `HardImpact`, `Ejection`, or
`Destruction`. Severity uses the server-derived contact normal and previous
accepted vehicle velocity:

```text
normal_impact = max(0, dot(-preImpactVelocity, collisionNormal))
```

Raw speed alone does not crash a vehicle. Shallow scrapes below
`min_direct_impact_dot` are capped at bump severity, so a fast tangential wall
slide does not become an ejection or destruction event. Low vertical-speed
aircraft landings also stay tolerant because terrain landing severity is based
on vertical impact into the upward contact normal, not total airspeed.

`event_cooldown_ticks` suppresses repeated crash consequences from the same
vehicle. `velocity_retention` damps accepted vehicle velocity after hard impacts
or worse. Ejection places occupants at the normal safe dismount position or the
airborne aircraft top position, then applies clamped launch velocity from the
previous vehicle velocity plus upward impulse and starts a short remount
cooldown. Destruction ejects occupants first, then despawns the vehicle when
`destroy_on_destruction` is true. If `explode_on_destruction` is enabled,
destruction creates a server-authoritative explosion with the `arvane:vehicle`
profile. Clients receive `ClientboundExplosionEvent` and expand it into local
`arvane:explosion` particles; there is no separate crash packet in V1.

The built-in buggy uses bump, hard-impact, rare high-speed ejection, and
extreme-speed destruction/explosion thresholds. The built-in plane uses higher
thresholds so rough low-speed landings are tolerated while severe wall or
terrain impacts can eject the pilot or destroy the aircraft. The boat and
submarine use bump/hard-impact tuning only.

The desktop debug menu has a default-on `Vehicle debug overlay` toggle in the
Player tab. While controlling a vehicle, the overlay shows local predicted
speed, velocity, acceleration, collision normal impact, authored thresholds, and
the server's latest crash assessment. This is useful when a wall hit only looks
like a hard stop: hard impacts can apply damping/damage without ejection, while
ejection/destruction require the server-computed normal-impact speed to cross
their thresholds.

### Aircraft Definitions

Aircraft use:

```json
{
  "name": "arvane:plane",
  "kind": "vehicle",
  "physics": "aircraft",
  "movement": {
    "min_speed": 0.0,
    "max_speed": 42.0,
    "acceleration": 18.0,
    "brake_deceleration": 24.0,
    "drag": 0.985,
    "pitch_rate": 75.0,
    "yaw_rate": 55.0,
    "roll_rate": 110.0,
    "roll_auto_level_rate": 60.0,
    "lift_strength": 24.0,
    "stall_speed": 9.0,
    "stall_gravity_scale": 1.8,
    "gravity_scale": 0.65,
    "max_climb_rate": 18.0,
    "max_descent_rate": 28.0,
    "ground_takeoff_speed": 10.0,
    "ground_drag": 0.92
  },
  "authority": {
    "requires_swept_collision": true,
    "max_unloaded_chunk_entry_distance": 0.0
  }
}
```

The client runs simplified arcade atmospheric flight: forward accelerates,
backward decelerates, jump/sneak pitch up/down, strafe yaws, vehicle roll
actions roll, and secondary applies air brake. Ground takeoff clamps vertical
nose influence until the aircraft has enough speed, then gives nose-up input a
small lift-off assist. Lift remains speed based and stall adds extra downward
acceleration below `stall_speed`.

In space environments, aircraft that provide a `space_movement` profile use
rocket-style controls instead: forward accelerates, backward decelerates, and
mouse look steers the vehicle toward the camera direction. Releasing movement
input coasts at the current speed, so a rocket in deep space does not silently
brake or freeze just because there is no terrain collision query.

Aircraft always use the swept AABB collision path on the server. The built-in
plane also rejects movement into unloaded chunks instead of accepting blind
travel through terrain that the server cannot validate.

### Submersible Definitions

Submersibles use:

```json
{
  "name": "arvane:submarine",
  "kind": "vehicle",
  "physics": "submersible",
  "movement": {
    "max_forward_speed": 24.0,
    "max_reverse_speed": 8.0,
    "acceleration": 28.0,
    "brake_deceleration": 24.0,
    "turn_rate": 65.0,
    "pitch_rate": 35.0,
    "ascend_rate": 10.0,
    "descend_rate": 10.0,
    "water_drag": 0.97,
    "air_drag": 0.45,
    "gravity_scale_out_of_water": 1.0,
    "min_water_occupancy": 0.45,
    "preferred_water_occupancy": 0.75,
    "surface_float_offset": 0.15
  },
  "authority": {
    "min_water_occupancy": 0.35
  }
}
```

Water occupancy is sampled around the AABB hull, not only at the entity center.
The client uses loaded fluid cells for prediction and the server recomputes the
same style of hull occupancy from authoritative world data. Jump ascends, sneak
descends, strafe steers, forward/back controls throttle, and secondary brakes.

When a submarine is below its server authority `min_water_occupancy`, the
server rejects movement that claims water or moves horizontally/upward like an
underwater vehicle. Slow falling out of water remains accepted as the
out-of-water fallback.

---

## Movement Authority

V1 vehicle movement is client-owned with server guardrails.

The driver client:

- predicts a simple controller-based vehicle transform
- advances prediction every fixed tick, independent of movement packet send cadence
- resolves the predicted AABB against local terrain before sending movement
- cancels impacted velocity axes so wall hits settle instead of correction-looping
- keeps the local rider anchored to the predicted seat transform
- renders the locally controlled vehicle from predicted state
- sends `ServerboundVehicleMove`
- stops sending independent `ServerboundPlayerMove` while mounted

The server:

- checks the sender is mounted
- checks the sender occupies the controlling seat
- validates sequence ordering
- validates finite numbers
- validates speed, speed-up, non-impact braking, angular speed, and vertical speed
- validates actual rotation delta from the last accepted state
- rejects final AABB hulls that overlap solid terrain
- rejects required swept vehicle paths that cross solid terrain
- rejects aircraft movement into unloaded chunks when configured
- validates required ground support when a ground-bound vehicle claims
  `OnGround`
- validates required water support
- validates submersible water occupancy from server world data
- writes accepted vehicle transform and velocity
- updates passenger attachment transforms
- derives crash severity from accepted collision/contact evidence when crash
  tuning is enabled
- sends `ClientboundVehicleCorrection` when rejecting a move
- broadcasts accepted `ClientboundVehicleState`

The server does not run full vehicle physics for every vehicle in V1.
For the local driver, ordinary `ClientboundVehicleState` and entity snapshots do
not rewind the controlled vehicle. They are observer/passenger replication for
that vehicle. `ClientboundVehicleCorrection` is the explicit guardrail snap.
When a vehicle has no mounted occupants, the server owns the remaining simple
motion and damps stale velocity through the same AABB collision path so an
unmounted buggy coasts briefly and settles instead of running forever.
Mounted crash consequences are server-authoritative and derive from the last
accepted state plus server collision/contact resolution. There is no
`ServerboundVehicleCrash` message, and correction snaps without safe contact
evidence do not trigger crash consequences.

---

## Replication

Reliable ride lifecycle packets keep seat state coherent:

- `ServerboundRideEntityRequest`
- `ServerboundDismountRequest`
- `ClientboundRideStarted`
- `ClientboundRideEnded`
- `ClientboundEntityPassengersSet`

If a rideable despawns while occupied, the world cleanup records ride-ended
events and the server broadcasts them when the rideable/passenger had network
ids. Clients also clear local mounted state when the mounted rideable despawns.

Continuous vehicle motion uses movement-lane packets:

- `ServerboundVehicleMove`
- `ClientboundVehicleState`
- `ClientboundVehicleCorrection`

Observers use the existing replicated entity mirror interpolation path for
vehicle transform smoothing. Passenger relationships are stored on client
mirrors so rendering and UI can identify mounted entities.

Crash destruction uses existing entity despawn replication. Crash explosion VFX
uses `ClientboundExplosionEvent`; clients expand the event into the built-in
`arvane:explosion` particle type on the gameplay lane.

---

## Dismounting

Dismount requests run on the server.

The server searches side, front, rear, and top candidate positions around the
rideable. A candidate is accepted only if the player AABB does not overlap
solid blocks and either has support below or is in fluid.

If no safe candidate exists, the server still completes the dismount by placing
the passenger above the rideable. Dismount is always allowed once the passenger
is mounted.

---

## Persistence

Vehicles use existing persistent entity records when an entity is marked
persistent. The saved record restores entity type, transform, velocity,
persistent UUID, and authored components.

Rideable and vehicle runtime components are reconstructed from the entity type's
authored vehicle definition during restore.

Seat occupancy and controller state are runtime/session state and are not
persisted in V1.

Vehicle crash tuning is authored data and requires no save-format change.
Damaged-but-not-destroyed vehicle health uses the ordinary runtime health
component; V1 does not persist changed vehicle health separately from the
existing persistent entity record.

---

## Manual Crash Smoke

When tuning crash behavior, smoke test these cases in a local world:

- buggy into a wall/tree at low speed: bump or ordinary correction only
- buggy into a wall/tree at medium speed: hard stop without occupant ejection
- buggy into a wall/tree at high speed: occupant ejection, safe placement, short
  remount cooldown
- buggy into a wall/tree at extreme speed: occupants eject first, then the
  vehicle despawns and explosion particles play
- plane rough low-vertical-speed landing: tolerated landing or bump, not ejection
- plane high-speed wall/terrain impact: pilot ejection when safe, or destruction
  at the extreme threshold

---

## V1 Limitations

- Collision hulls are AABB only.
- Movement is controller based.
- Vehicle behavior is tuned per definition, not by a generic solver.
- The built-in client shell is intentionally conservative.
- Ground vehicles carry momentum over ledges and fall under simple gravity while
  airborne; they regain `OnGround` only after support/collision resolution.
- Surface-water V1 vehicles are water-bound; dry-land boat driving is rejected
  instead of simulated with land drag.
- Aircraft are arcade vehicles, not aerodynamic rigid bodies.
- Submarines sample water occupancy and drag, not realistic buoyancy.
- Aircraft chunk lookahead is currently conservative: mounted chunk interest
  follows the vehicle position, and server authority rejects configured
  unloaded-chunk entries instead of expanding client interest ahead of velocity.
- Observer interpolation uses the existing entity mirror path; there is no
  aircraft-specific snapshot rate or interpolation buffer yet.
- Crash consequences are evaluated for mounted, client-owned vehicle movement.
  Unmounted coasting vehicles still use server collision damping but do not emit
  crash ejection/destruction events in V1.
- V1 passenger state is not saved across server restarts.
- Vehicle item placement, crafting, audio, VFX, and animation beyond the buggy's
  minimal forward/reverse wheel clips are out of scope for this pass.
