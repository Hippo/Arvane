# Rideables And Vehicles

This document describes the V1 rideable mount and vehicle model.

V1 supports:

- `arvane:horse`
- `arvane:oak_boat`
- `arvane:buggy`

V1 deliberately does not include aircraft, submarines, fuel, vehicle damage,
vehicle inventory, constructed vehicles, mesh collision, OBB collision, mounted
weapons, or a generic rigid-body solver.

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
seat per built-in vehicle.

`turn_rate` and `authority.max_angular_speed` are authored in degrees per
second for readability. The loader converts them to radians per second for
runtime prediction and validation.

The built-in buggy uses `arvane:animation/automobile`; the client and accepted
server state select its `driving` clip when moving along the buggy's visual
front and its `reverse` clip when moving backward.

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
- validates required ground or water support
- writes accepted vehicle transform and velocity
- updates passenger attachment transforms
- sends `ClientboundVehicleCorrection` when rejecting a move
- broadcasts accepted `ClientboundVehicleState`

The server does not run full vehicle physics for every vehicle in V1.
For the local driver, ordinary `ClientboundVehicleState` and entity snapshots do
not rewind the controlled vehicle. They are observer/passenger replication for
that vehicle. `ClientboundVehicleCorrection` is the explicit guardrail snap.

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
- Surface-water V1 vehicles are water-bound; dry-land boat driving is rejected
  instead of simulated with land drag.
- V1 passenger state is not saved across server restarts.
- Vehicle item placement, crafting, audio, VFX, and animation beyond the buggy's
  minimal forward/reverse wheel clips are out of scope for this pass.
