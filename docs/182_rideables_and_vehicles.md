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

The built-in buggy uses `arvane:animation/automobile`; the client and accepted
server state select its `driving` clip when moving along the buggy's visual
front and its `reverse` clip when moving backward.

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

The client runs simplified arcade flight: forward accelerates, backward
decelerates, jump/sneak pitch up/down, strafe yaws, vehicle roll actions roll,
and secondary applies air brake. Ground takeoff clamps vertical nose influence
until the aircraft has enough speed, then gives nose-up input a small lift-off
assist. Lift remains speed based and stall adds extra downward acceleration
below `stall_speed`.

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
- validates required ground support when a ground vehicle claims `OnGround`
- validates required water support
- validates submersible water occupancy from server world data
- writes accepted vehicle transform and velocity
- updates passenger attachment transforms
- sends `ClientboundVehicleCorrection` when rejecting a move
- broadcasts accepted `ClientboundVehicleState`

The server does not run full vehicle physics for every vehicle in V1.
For the local driver, ordinary `ClientboundVehicleState` and entity snapshots do
not rewind the controlled vehicle. They are observer/passenger replication for
that vehicle. `ClientboundVehicleCorrection` is the explicit guardrail snap.
When a vehicle has no mounted occupants, the server owns the remaining simple
motion and damps stale velocity through the same AABB collision path so an
unmounted buggy coasts briefly and settles instead of running forever.

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

---

## Dismounting

Dismount requests run on the server.

The server searches side, front, rear, and top candidate positions around the
rideable. A candidate is accepted only if the player AABB does not overlap
solid blocks and either has support below or is in fluid.

If no safe candidate exists, V1 keeps the passenger mounted.

---

## Persistence

Vehicles use existing persistent entity records when an entity is marked
persistent. The saved record restores entity type, transform, velocity,
persistent UUID, and authored components.

Rideable and vehicle runtime components are reconstructed from the entity type's
authored vehicle definition during restore.

Seat occupancy and controller state are runtime/session state and are not
persisted in V1.

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
- V1 passenger state is not saved across server restarts.
- Vehicle item placement, crafting, audio, VFX, and animation beyond the buggy's
  minimal forward/reverse wheel clips are out of scope for this pass.
