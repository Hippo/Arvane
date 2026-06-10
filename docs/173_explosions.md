# Explosion System

Explosions are server-authoritative gameplay events. The server resolves an
explosion profile, simulates deterministic block/entity effects from the request
seed, mutates blocks through a batched world mutation path, applies entity damage
and impulse, then sends a visual event to nearby clients.

## Profiles

Built-in profile IDs:

* `arvane:tnt`
* `arvane:small`
* `arvane:large`
* `arvane:shockwave`
* `arvane:vehicle`
* `arvane:debug`

Profiles define radius, power, damage toggles, drop behavior, ray and block
caps, entity impulse, and future sound metadata. Runtime requests may override
power, radius, block damage, entity damage, drops, fire intent, visual sound
intent, max destroyed blocks, max rays, and max drops.

## Block Behavior

Blocks can author a `blast` object:

```json
{
  "blast": {
    "resistance": 4.5,
    "destroyable_by_explosions": true,
    "absorbs_blast": false,
    "flammable": false,
    "drops_items": true
  }
}
```

`resistance` slows ray power. `destroyable_by_explosions` protects blocks from
being replaced with air. `absorbs_blast` stops a ray after the block is hit.
`drops_items` controls whether destroyed blocks are eligible for normal block
drops.

## Networking

Authoritative state still uses ordinary block updates, entity snapshots, entity
damage/despawn behavior, and dropped-item replication. `ClientboundExplosionEvent`
is a cosmetic event used by clients to spawn local `arvane:explosion` particles
and, later, sound. It is `ServerToClient`, `UnreliableEvent`, and on the
`Gameplay` lane.

## Commands

The built-in server command is:

```text
/explode <x y z> [profile] [power] [radius] [flags...]
```

Alias: `/boom`

Flags include `--no-block-damage`, `--no-entity-damage`, `--no-drops`,
`--fire`, `--no-fire`, `--no-sound`, `--seed`, `--max-blocks`, `--max-rays`,
and `--max-drops`. The command validates radius, power, and cap overrides before
handing the request to the authoritative server explosion service.

## Vehicles

Vehicle destruction uses the shared explosion system with the
`arvane:vehicle` profile. Crash handling still owns ejection, cooldowns, and
vehicle despawn; the explosion system owns terrain mutation, entity effects,
drops, and the client visual event.
