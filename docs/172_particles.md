# Particle System

## Overview

Particles are client-owned visual effects. They are not entities, are not saved,
and are not individually replicated over the network.

The server may trigger a burst with `ClientboundSpawnParticles`, but the client
chooses the actual particle instances, simulates their lifetime locally, and
submits billboard draw items in the render frame packet.

## Data Flow

```text
Pack YAML -> ParticleRegistry + particle atlas -> ParticleSystem -> FramePacket.particles -> transparent render pass
```

`DesktopLoadedAssets` loads particle definitions from installed packs and builds a
dedicated texture-array atlas. Static particles resolve one texture into one
atlas frame. Animated particles resolve every authored frame into atlas page and
UV data during asset loading. Block particle textures authored through block
definitions are also packed into this particle atlas so block dust can use block
art without render-time texture binding.

Rendering never binds a separate texture per animation frame. Live particles
only carry pre-resolved frame page and pixel UV values.

## Pack Format

Particle files live under:

```text
data/<namespace>/particles/*.yaml
```

Static texture example:

```yaml
id: arvane:block_dust
texture:
  mode: static
  image: arvane:texture/particle/critical_hit
count: [4, 8]
lifetime: [0.28, 0.46]
```

The base pack includes `arvane:critical_hit`, a static additive particle used by
server-confirmed critical entity attacks.

Animated sequence example:

```yaml
id: arvane:explosion
texture:
  mode: sequence
  frames:
    - arvane:texture/particle/explosion_0
    - arvane:texture/particle/explosion_1
animation:
  sample: lifetime_scaled
  playback: once
```

The loader preserves `frames` order. `arvane:explosion` in the base pack uses
`explosion_0` through `explosion_15`.

## Runtime Behavior

`ParticleSystem` owns live particles and counters. Spawns are rejected or scaled
by budget and distance before particles are allocated. The current V1 counters
track live/rendered particles plus total spawns, expirations, unknown-type
rejections, budget rejections, distance rejections, and invalid requests.

Block-hit dust is a local client effect. Block-break dust is server-triggered
when a confirmed replicated block update sets a block to air.

Block dust uses `arvane:block_dust` plus an optional texture override. For block
definitions with object-form `textures`, the `particle` entry selects the dust
texture. For string-form `textures`, that string is also the particle texture.
Sprint-on-ground dust and block mining/break dust use this resolved block
particle texture.

## Networking

`ClientboundSpawnParticles` contains:

- world id
- namespaced particle type
- optional texture asset override
- position
- normal
- extents and radius
- count
- seed
- spawn shape

The packet is `ServerToClient`, `UnreliableEvent`, and on the `Gameplay` lane.
There is no `ServerboundSpawnParticles`.

Server-authoritative explosions use `ClientboundExplosionEvent`. The client
handles that event by spawning local `arvane:explosion` particles, while block
mutations, entity damage, and drops remain regular authoritative replication.
