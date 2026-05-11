# Networking

Arvane uses a server-authoritative networking model.

Networking must support:

- integrated singleplayer server
- dedicated multiplayer server
- multiple worlds/dimensions
- chunk streaming
- entity replication
- client-authored player movement with server guardrails
- direct movement correction when guardrails reject a packet
- direct local velocity impulses when authoritative gameplay applies knockback
- future gameplay systems without redesigning the protocol

The networking layer exists to synchronize client-visible state. It does not own world simulation, entity simulation, chunk storage, persistence, or gameplay rules.

---

## Goals

The networking system should provide:

- a clear authority model
- reliable player input delivery
- bounded world replication
- efficient chunk streaming
- predictable entity synchronization
- separation between protocol, transport, and gameplay systems
- support for both local and remote clients

Networking code must avoid becoming a second simulation layer.

The server simulates shared world state.
The client renders, runs local player locomotion, and displays replicated state.

---

## Authority Model

Arvane is server-authoritative.

The server owns:

- world state
- chunk data
- block mutations
- lighting propagation and authoritative light data
- entity simulation
- inventory state
- player profiles and sessions
- latest accepted player position
- authoritative interaction results
- persistence decisions

The client owns:

- input collection
- camera presentation
- local render state
- rendering received chunk/block/light/mesh data
- local locomotion for the controlled player
- interpolation buffers
- UI state
- temporary visual effects

The client may request actions, but it does not directly mutate authoritative world state.

The client must not generate authoritative lighting updates after block edits. In integrated singleplayer, the integrated server/world still owns lighting; the client consumes replicated chunk/light data and performs local rendering work only.

Examples:

- client requests block break
- server validates reach, target, tool, and permissions
- server mutates world
- server replicates result

The client may predict some effects for responsiveness, but server state always wins.

---

## Integrated Server Model

Singleplayer uses the same logical networking model as multiplayer.

A singleplayer session runs:

- a client
- an integrated server
- local transport or in-process message bridge

The client should not directly access server-owned world state merely because the server is local.

This keeps singleplayer and multiplayer behavior aligned.

Allowed:

- optimized local message transport
- shared process memory for transport internals
- debug tools that inspect both sides explicitly

Forbidden:

- client directly mutating server chunks
- renderer reading mutable server chunk storage
- gameplay code bypassing server validation because the session is singleplayer

---

## Transport Layer

The transport layer is responsible for moving messages between peers.

It should not understand gameplay rules.

The default dedicated-server port is `42069`. Client-entered addresses may omit
the port; a bare host such as `localhost` resolves to `localhost:42069`.
The desktop client's saved server entries are persisted in
`arvane_home/config/server_list.json`.

Transport responsibilities:

- connection management
- packet send/receive
- channels
- reliability policy
- ordering policy
- disconnect handling
- basic bandwidth statistics

Gameplay networking responsibilities:

- encoding protocol messages
- replication decisions
- chunk streaming
- entity synchronization
- input commands
- movement validation and correction data

The transport backend should be replaceable without rewriting gameplay networking.

---

## Transport Pump Ownership

Transport backends use explicit owner-thread pumping.

The owner thread calls `poll_events()` and owns backend state mutation. For GameNetworkingSockets this includes socket handles, listen handles, poll groups, connection maps, connection state, callback dispatch, and queued inbound `NetworkEvent`s. GameNetworkingSockets callbacks are routed during `RunCallbacks()` on the owner thread and must not update gameplay systems directly.

`send()` may be called from outside the owner thread, but it only enqueues an outbound send command. The owner thread drains outbound commands, resolves engine `ConnectionID` values to backend handles, applies the selected reliability flags, receives poll-group messages, and returns engine events for the client or server runtime to process at its normal tick point.

The in-process transport uses bounded single-producer/single-consumer queues. The client writes the client-to-server queue and the server reads it. The server writes the server-to-client queue and the client reads it. Queue-full conditions fail the send/connect operation instead of blocking.

---

## Protocol Design

Protocol messages should be explicit, versioned, and direction-aware.

Message categories:

- connection/session messages
- player input messages
- world/chunk messages
- block update messages
- entity replication messages
- inventory and interaction messages
- time/tick synchronization messages
- debug/development messages

Messages should avoid exposing internal engine pointers, raw memory layouts, or unstable runtime-only structures.

Protocol data should be serialized through dedicated packet schemas.

---

## Versioning and Compatibility

The network protocol must have an explicit protocol version.

Connection setup should validate:

- protocol version
- game version
- required content/registry compatibility
- authentication/session identity
- optional feature flags

A client with incompatible protocol or content definitions must be rejected before entering the world.

The protocol should not assume that all clients have identical runtime IDs unless a registry synchronization/remapping step has established that compatibility.

---

## Tick Model

The server advances simulation using the fixed simulation tick.

Networking should be tick-aware.

Important tick values:

- server tick
- client movement tick
- movement packet sequence
- replicated entity state tick
- chunk data version or generation number

Messages that describe simulation state should include enough timing information for the receiver to place that state in the correct timeline.

---

## Client Input Flow

Clients send player movement packets and gameplay requests to the server.

`ServerboundPlayerMove` contains:

- client tick or sequence number
- proposed position and velocity
- yaw and pitch
- movement flags
- action flags
- selected hotbar slot

The client computes local locomotion immediately from input and sends the
result at a bounded movement cadence. Hotbar and interaction flags may force an
immediate movement packet so gameplay requests are not delayed unnecessarily.
The client defaults to a 20 Hz movement send rate and does not need local server
configuration to start moving. If a server uses a different movement cadence, it
sends `ClientboundSetMovementHz` reliably during world join.

The server validates movement packets against last accepted state, permissions,
world bounds, loaded chunks, collision, and configured speed/distance limits.
The server does not acknowledge ordinary movement packets. It only sends
`ClientboundPlayerMoveCorrection` when it rejects or forcibly adjusts a move.

Authoritative gameplay may still change the local player's velocity without a
teleport. Combat knockback sends `ClientboundPlayerVelocity` reliably to the
owning client with the server-computed velocity and knockback control
suppression ticks. The client applies the velocity to local locomotion; remote
observers continue to see accepted player state through entity snapshots.

Interaction and inventory results remain server-authoritative. Even when
actions are carried on a movement packet, block edits, combat, item use, and
inventory mutations are validated against the current accepted server state.

---

## Client-Authored Movement

The controlled local player moves immediately on the client. This path is not a
rewind/replay system. The local controller owns the current player transform,
velocity, yaw, pitch, and movement flags used for presentation and outgoing
movement packets.

`ServerboundPlayerMove` is unreliable latest-wins and includes a monotonically
increasing sequence number. The sequence lets the server ignore stale movement
packets. The `client_tick` field is diagnostic and ordering context only; the
server uses its own receive time for speed validation.

The server keeps a small accepted movement state per connected player:

- position
- velocity derived from accepted movement when needed
- yaw and pitch
- movement flags
- last accepted sequence
- server tick and receive time

The accepted position is used for replication, interaction reach checks, combat,
persistence, and world-interest decisions. Other clients see that accepted
transform through entity snapshot replication.

When a packet is accepted, the server writes the accepted transform to player
state. When a packet is rejected, the server sends
`ClientboundPlayerMoveCorrection` reliably with the last accepted transform and
a rejection reason. The client applies the correction directly. It does not
rewind, replay, or wait for per-packet acknowledgements.

Recent server-applied knockback is accounted for by movement validation using
the server velocity and the remaining control-suppression window. This keeps
normal speed guardrails active while allowing the first post-knockback movement
packet to include the impulse the server just assigned.

Movement validation should stay lightweight. It checks final state and derived
motion; it must not run the full player controller for every remote player.

Shared movement constants should come from configuration where practical.

Do not scatter movement constants across client and server code.

Guardrails are configured under `network.movement`, including send cadence,
speed limits, distance tolerances, unloaded-chunk rejection, collision checks,
and correction cooldown.

Small local presentation smoothing may be used after a correction, but gameplay
state must use the corrected transform immediately.

Local movement depends on local collision data. When client terrain data needed
for local collision is unavailable, collision queries should use a conservative
fallback such as treating missing chunks as solid. The server still rejects
invalid movement, but conservative local movement avoids walking visually
through unknown chunks before a correction arrives.

---

## World Replication Boundaries

Clients only receive world data relevant to their current session.

Replication boundaries are based on:

- current world/dimension
- player position
- view distance
- simulation distance
- interest rules
- permissions
- debug subscriptions

A client in one world does not automatically receive chunks, entities, or events from another world.

Global systems may still send cross-world information, such as:

- `ClientboundChatMessage` rich text chat messages
- direct messages
- player list updates
- server notifications

World-local replication and global session replication must remain separate.

---

## Chunk Streaming

Chunk streaming sends world terrain data from server to client.

The server decides which chunks a client should know about.

Streaming priority should consider:

- player chunk position
- view direction
- distance from player
- whether the chunk is already loaded
- whether the chunk is generated
- whether neighboring chunks are needed for meshing
- bandwidth budget

The client may request chunks, but the server decides whether to send them.

---

## Chunk Stream States

A client-visible chunk may move through states such as:

- unknown
- requested
- queued
- loading/generating
- streaming
- received
- meshing
- visible
- stale
- unloaded

The exact implementation may vary, but the networking model should preserve these concepts.

Chunk data should be versioned.

If the server sends a chunk snapshot, later block updates must either:

- apply cleanly to that snapshot version
- be deferred until the base chunk arrives
- be replaced by a newer full chunk snapshot

Chunk lighting changes are authoritative server/world updates. They may be streamed as part of a newer section snapshot or a future light-delta message. Clients may remesh received sections using that data, but they do not decide lighting propagation.

Because server lighting is section-oriented and asynchronous, a streamed chunk may contain the best currently published lighting rather than final lighting for all pending continuations. Later authoritative section snapshots or future light deltas are allowed to refine visible lighting. Clients must treat those updates as remesh triggers, not as permission to run authoritative propagation locally.

---

## Chunk Data Messages

Chunk data messages should include:

- world identifier
- chunk coordinate
- block/geometry version
- light version
- serialized section data
- lighting data
- height or occupancy metadata if needed
- compression information
- registry/remapping context if needed

Chunk payloads should be compressible.

Large chunk payloads may be split into fragments by the transport or protocol layer.

Chunk data should not contain raw engine memory dumps.

---

## Block Updates

Small world changes should replicate as block update messages instead of full chunk retransmits.

A block update should include:

- world identifier
- block position
- new block state
- update version or tick
- optional cause/category

Block updates may be batched.

Examples of block update causes:

- player placement
- player breaking
- fluid update
- lighting-related change
- world generation fixup
- scripted/gameplay event

The client applies block updates only if the relevant chunk is present and the update is not stale.

---

## Chunk Unloading

The server may instruct the client to unload chunks that are no longer relevant.

Chunk unloads should be explicit enough to avoid stale world rendering.

A client may also locally evict old chunks from render memory, but it must not pretend those chunks are still authoritative.

When a player changes world/dimension, the client should clear or transition world-local replicated state according to the world switch protocol.

---

## Entity Synchronization

Entities are replicated based on client interest.

Interest is usually determined by:

- current world
- distance from player
- visibility/relevance rules
- entity type
- ownership
- gameplay importance

The server owns entity creation, destruction, and authoritative state.

The client maintains replicated entity mirrors for rendering and local presentation.

---

## Entity Identity

Networked entities need stable network identifiers for the lifetime of their replication.

Entity network IDs are not raw ECS entity handles.

The server maps runtime ECS entities to network entity IDs.

The client maps network entity IDs to local presentation entities.

Network entity IDs only need to be stable for the active session unless persistence explicitly requires otherwise.

---

## Entity Spawn and Despawn

Entity spawn messages should include:

- network entity ID
- entity type
- initial position
- rotation/orientation
- initial velocity if relevant
- initial replicated components
- owning player if relevant

Entity despawn messages should include:

- network entity ID
- despawn tick or reason if useful

Clients must handle despawn of unknown or already-despawned entities safely.

---

## Entity State Replication

Entity state should be replicated as component-oriented snapshots or deltas.

Common replicated state:

- transform
- velocity
- animation state
- health/status
- held item
- equipment
- relevant gameplay flags

Not every component is networked.

Components should opt into replication explicitly.

Replication should avoid sending unchanged state every tick unless required.

---

## Entity Interpolation

Remote entities should usually be interpolated, not locally simulated.

The client keeps presentation interpolation state for each replicated entity.
When a new authoritative transform arrives, rendering eases from the current
rendered transform toward the new target over the elapsed server-tick interval.
Dropped-item mirrors use the same presentation smoothing as other entities.

Gameplay queries continue to use authoritative mirror transforms, not smoothed
render transforms. This keeps visuals smoother without feeding render
interpolation back into simulation or interaction validation.

Important entities may use higher update rates or special replication rules.

Examples:

- players
- projectiles
- vehicles
- physics-driven objects
- dropped items

---

## Locally Controlled Player Entity

The local player is special.

For the local player:

- movement is computed client-side and sent as `ServerboundPlayerMove`
- rejected movement is corrected by `ClientboundPlayerMoveCorrection`
- camera follows local or corrected player state
- remote clients receive replicated authoritative state

The local client should not treat local state as authoritative world truth.

Interactions still require server validation.

---

## Inventory and Interaction Replication

Inventory state is server-authoritative.

Clients may optimistically display simple interaction feedback, but inventory mutations are confirmed by the server.

Examples:

- selected hotbar slot may be sent as input
- item use request is sent to server
- server validates item, cooldown, target, and permissions
- server sends inventory update and world/entity result

Inventory replication should support:

- full inventory snapshots
- slot updates
- carried item updates
- equipment updates
- transaction acknowledgements

---

## World Events

World events are transient replicated messages.

Examples:

- block break particles
- sound effects
- entity hurt animation
- explosion effect
- door open sound
- item pickup effect

Events should not replace authoritative state replication.

A particle event may be dropped.  
A block update may not be silently lost if it changes authoritative world state.

---

## Reliability Policy

Different message types require different delivery guarantees.

Reliable ordered:

- connection setup
- login/session acceptance
- registry negotiation/sync
- world join/leave
- chunk data and chunk unloads
- block update batches
- inventory transactions and revisioned state
- entity spawn/despawn
- gameplay-affecting requests such as item use or inventory actions
- `ServerboundChatMessage` and `ClientboundChatMessage`
- commands

Reliable immediate:

- `ClientboundKickPlayer`, because user-facing disconnect reasons should be
  queued and flushed promptly before the server closes the connection

Unreliable latest-wins:

- `ServerboundPlayerMove` where newer movement supersedes older movement
- entity transform snapshots
- transient player block-break progress
- transient physics, look, and movement state

Unreliable event:

- `ClientboundKeepAlive`
- `ServerboundKeepAlive`
- cosmetic particles
- sound cues
- temporary visual hints

Movement corrections are reliable ordered because they snap the local player
back to accepted server state.

The protocol should not force all traffic through one reliability mode.

---

## Bandwidth Budgeting

The server should budget outgoing traffic per client.

High-priority data:

- session critical messages
- nearby chunks
- local player movement corrections
- nearby entity state
- inventory confirmations

Lower-priority data:

- far chunks
- distant entities
- cosmetic events
- debug data

Chunk streaming must not starve gameplay-critical replication.

---

## Compression

Large payloads should support compression.

Good compression candidates:

- chunk data
- batched block updates
- registry or content metadata
- large entity snapshots

Small high-frequency messages should avoid compression overhead unless batching makes it worthwhile.

Compression should be part of the protocol framing or message metadata, not hidden inside gameplay systems.

---

## Security and Validation

The server must validate client requests.

Clients must not be trusted for:

- position
- velocity
- inventory contents
- block reach
- block target validity
- item ownership
- world permissions
- entity damage
- resource counts

Validation examples:

- movement cannot exceed allowed rules
- block interaction must be within reach
- player must be in the correct world
- chunk request must be within allowed range
- item use must match server inventory state

Invalid messages may be ignored, corrected, rate-limited, or treated as a protocol violation.

---

## Anti-Cheat Boundaries

The networking model should make cheating harder by design.

The server should avoid sending unnecessary information.

Examples:

- do not send chunks outside relevance range
- do not send hidden entities unless needed
- do not trust client-reported hits
- do not trust client-selected block results without ray validation
- do not trust inventory transactions generated only by the client

Anti-cheat should be layered on top of server authority, not used as a replacement for it.

---

## Multiple Worlds and Dimensions

Each world/dimension has separate replicated state.

A client session may be attached to one active world at a time unless explicitly designed otherwise.

World-local replicated state includes:

- chunks
- block updates
- world entities
- local weather/environment state
- world time if world-specific

Global replicated state includes:

- player profile/session information
- chat routing and server-authored rich text delivery
- direct messages
- server-wide notifications
- permissions
- connection state

Changing worlds should use an explicit world transition flow.

---

## World Transition Flow

When a player changes worlds, the server should:

1. validate the transfer
2. remove or suspend old world-local replication
3. send world transition message
4. send new world identity and spawn state
5. begin chunk streaming for the new world
6. spawn relevant entities
7. resume normal replication

The client should not mix chunks or entities from different worlds.

---

## Threading Rules

Networking may run on its own IO thread or worker threads, but decoded gameplay messages must be handed off safely.

Forbidden:

- network thread directly mutating world chunks
- network thread directly mutating ECS state
- worker thread directly sending mutable world references to clients
- client render thread directly consuming partially decoded network state

Required pattern:

- network receives packet
- packet is decoded into a command/message
- message is queued for the correct owner
- simulation thread applies authoritative changes
- replication system produces outbound messages
- transport sends serialized packets

World mutation remains owned by the simulation side.

Chunk generation follows the same rule. A networking or streaming path may request missing chunk data, but it must not synchronously generate terrain while handling a packet or preparing a send. The authoritative server/world scheduler runs generation on workers, then the simulation thread publishes completed chunks before replication serializes them.

---

## Replication Snapshots

Replication should operate on stable snapshots or explicitly owned state.

For chunks:

- use serialized chunk snapshots
- include version information
- do not stream mutable chunk storage directly

For entities:

- gather replicated component state during a safe simulation phase
- serialize from that gathered state
- avoid reading ECS data concurrently with mutation

For player input:

- queue input commands
- process during simulation tick
- acknowledge processed input

---

## Error Handling

Networking code must handle malformed, stale, duplicate, and out-of-order messages.

Expected cases:

- chunk update arrives before chunk snapshot
- entity delta arrives before spawn
- duplicate chunk fragment arrives
- stale entity transform arrives
- client requests chunk after changing worlds
- player disconnects during transfer
- packet references unknown entity

These cases should not crash the engine.

---

## Disconnect Handling

Server-initiated disconnects use one kick path: send `ClientboundKickPlayer`
with a human-readable reason, flush/close the transport connection without
blocking indefinitely, and run normal session cleanup. This path is used for
authentication rejection, malformed protocol rejection, duplicate profile
rejection, keep-alive timeout, orderly shutdown, and future administrative
kicks. Transport-failed disconnects may skip the packet because the peer is
already unreachable.

When a client disconnects, the server should:

- remove the session from active networking
- detach or despawn world-local player entity according to gameplay rules
- persist necessary player data
- clean replication state
- notify relevant systems

The global player profile may outlive the connection.

The world-local player entity may not.

Remote clients show a connecting screen after transport connection startup and
before world join. While that screen is open, networking and timeout ticks
continue, but the pending session is not rendered as a world. `Cancel` closes
the pending connection and returns to the server list.

Clients store the first clear disconnect reason they receive. `ClientboundKickPlayer`,
transport failure, connection-attempt timeout, and client-side keep-alive timeout
all leave multiplayer world state and show the reusable disconnect screen. That
screen displays the reason and offers `Reconnect` to the last selected server or
`Server List` to return to saved servers.

## Keep Alive

Arvane uses application-level keep alive packets in addition to transport-level
connection state.

Server behavior:

- send `ClientboundKeepAlive { id }` periodically to authenticated sessions
- accept `ServerboundKeepAlive { id }` only when it echoes the most recent id
- kick the client with `Timed out.` when no valid reply arrives before the
  configured timeout

Client behavior:

- record the time of the latest `ClientboundKeepAlive`
- immediately echo it with `ServerboundKeepAlive`
- locally disconnect with `Lost connection to the server.` if no server keep
  alive arrives before the configured timeout

Configuration:

```yaml
network:
  keep_alive:
    interval_seconds: 5
    client_timeout_seconds: 30
multiplayer:
  keep_alive:
    server_timeout_seconds: 30
  connection_timeout_seconds: 10
```

The server timeout must be greater than the send interval. The client
connection timeout covers offline or unreachable server attempts before gameplay
state can be established.

---

## GameNetworkingSockets Callback and Poll-Group Routing

Arvane uses GameNetworkingSockets as the transport layer, but engine systems should not depend directly on raw transport callbacks.

Transport events should be routed through a small networking backend layer that converts GameNetworkingSockets events into engine-level connection events, packet events, and lifecycle events.

This keeps protocol, session, and gameplay systems independent from GameNetworkingSockets-specific details.

## Routing Goals

The routing layer should:

* isolate GameNetworkingSockets API usage
* centralize callback polling
* associate transport connections with server sessions
* associate client connections with remote server state
* route packets to the protocol decoder
* route connection lifecycle events to session management
* avoid calling gameplay logic directly from transport callbacks
* provide clear shutdown behavior

The transport backend should be replaceable in principle, even if GameNetworkingSockets is the only supported backend.

## Polling Model

The networking system should use explicit polling rather than allowing arbitrary callback-driven gameplay mutation.

At a high level:

```text
network backend polls GameNetworkingSockets
        ↓
backend converts transport events into engine network events
        ↓
events are queued
        ↓
server/client networking layer drains events at a defined point
        ↓
protocol/session/game systems process decoded messages
```

Transport callbacks must not directly mutate worlds, chunks, ECS registries, inventories, or renderer state.

## Poll Groups

GameNetworkingSockets poll groups should be used to organize related connections.

On a dedicated server, a poll group may contain all active client connections for that server instance.

Possible poll-group layout:

```text
server_poll_group
    client_connection_a
    client_connection_b
    client_connection_c
```

Future versions may use multiple poll groups for scaling, sharding, or world-specific routing, but early milestones should prefer one clear server poll group unless there is a strong reason to split.

On the client, the connection to the server may be managed directly or through a small client-side poll group.

## Connection Mapping

The networking backend should maintain a mapping from transport connection handles to engine connection records.

Conceptually:

```text
GNS connection handle
        ↓
NetworkConnection
        ↓
ServerSession or ClientRemoteServer
```

A server connection record may include:

```text
NetworkConnection
    transport_handle
    connection_state
    remote_address
    session_id
    poll_group_id
    last_receive_time
    last_send_time
    pending_disconnect_reason
```

The transport handle should not be used as the long-term player identity.

The server session ID and player ID should be assigned by server/session logic after handshake.

## Transport Event Queue

The backend should convert transport events into an internal event queue.

Example engine-level events:

```text
NetworkEvent::ConnectionRequested
NetworkEvent::ConnectionAccepted
NetworkEvent::ConnectionClosed
NetworkEvent::ConnectionFailed
NetworkEvent::MessageReceived
NetworkEvent::MessageSendFailed
```

Each event should include enough information for the networking layer to route it safely.

Example:

```text
MessageReceived
    connection_id
    channel_or_reliability_info
    payload_bytes
    receive_time
```

The protocol layer should decode payload bytes after routing, not inside the raw GameNetworkingSockets callback.

## Packet Routing

Inbound packet routing should follow this flow:

```text
GameNetworkingSockets message
        ↓
network backend message event
        ↓
protocol decoder
        ↓
connection/session state machine
        ↓
gameplay command or replication handler
```

The protocol decoder should reject malformed packets before they reach gameplay systems.

Connection/session state should determine which messages are valid at each phase.

For example:

```text
connecting
    accepts ServerboundHello only

handshaking
    accepts handshake follow-up messages

syncing
    accepts registry/asset/session sync messages

gameplay
    accepts movement, interaction, chat, and replication acknowledgements
```

Messages received in the wrong state should be rejected or ignored according to protocol policy.

## Callback Safety Rules

GameNetworkingSockets callbacks or polling results must not:

* mutate world state directly
* tick simulation directly
* create or destroy entities directly
* load or unload chunks directly
* create OpenGL resources
* block on asset loading
* block on disk saves
* call plugin gameplay hooks directly

They may:

* enqueue network events
* update transport-owned connection state
* copy packet bytes into owned buffers
* mark connections as closing
* record diagnostics/statistics

The current GameNetworkingSockets backend assumes one active callback owner per process. This is asserted in the transport implementation. Supporting simultaneous independent GameNetworkingSockets transports in one process requires replacing that singleton callback route with an explicit per-instance dispatch mechanism.

## Send Path

Outgoing packets should flow through a similar abstraction:

```text
gameplay/session/protocol system
        ↓
protocol encoder
        ↓
network backend send queue
        ↓
GameNetworkingSockets send call
```

The gameplay layer should not directly call GameNetworkingSockets send functions.

The send API should accept engine connection/session handles and encoded protocol messages.

The backend maps those handles to transport connection handles.

## Reliability and Channels

GameNetworkingSockets supports different send modes. Arvane protocol messages should explicitly choose reliability behavior based on message type.

Arvane currently maps message lanes as:

| Lane | Reliability            | Message types                                 |
| ---: | ---------------------- | --------------------------------------------- |
|  `0` | reliable/control       | handshake, kick, keep alive, movement cadence setup, world join/leave |
|  `1` | reliable               | gameplay-critical blocks, inventory, entity lifecycle |
|  `2` | mixed movement         | player movement, movement snapshots, corrections, local velocity impulses |
|  `3` | reliable bulk          | chunk streaming                               |
|  `4` | mixed/debug            | ping/pong, diagnostics                        |

Chunk streaming must not block lane `2` input/movement traffic. Reliable chunk state
is confirmed only after the transport accepts the send; otherwise the chunk remains
pending and can be retried.

The exact transport flags should be hidden behind protocol send policies.

Gameplay systems should choose semantic send policies, not raw GameNetworkingSockets flags.

## Profile Metadata And Skin Bytes

Player profile metadata is session-scoped replicated data. Servers send
`ClientboundPlayerProfileInfo` for authenticated, relevant players. The packet contains
`player_id`, `profile_id`, display name, and a `SkinDescriptor`; the client uses
`player_id` to associate metadata with `ClientboundEntitySpawn.owning_player`. The
descriptor includes the slim player model flag as presentation metadata.

Custom skin image bytes are bulk data. They are never broadcast automatically
with entity replication. Clients request missing advertised hashes on demand,
and upload/download image data uses the reliable ordered bulk lane shared with
chunk streaming. Clients render fallback/default skins until a requested skin is
validated and cached.

Skin transfers must not become an unbounded side channel on the session lane or
bulk lane. Servers keep per-connection skin request rate limits, download byte
budgets, in-flight request sets, and advertised-hash allow lists. Uploads keep a
single pending buffer per connection by default, enforce a global active upload
cap, time out stale or idle upload ids, and reject chunks that exceed configured
chunk size or expected total bytes. The normal gameplay lanes remain available
because skin image data stays on the bulk lane and request/upload negotiation is
small, bounded, and rate-limited.

## Shutdown Routing

During shutdown, the networking layer should:

1. Stop accepting new gameplay messages.
2. Send `ClientboundKickPlayer` with `Server is shutting down.` to connected clients.
3. Flush or discard outgoing queues according to shutdown mode.
4. Close transport connections.
5. Destroy poll groups.
6. Destroy the GameNetworkingSockets interface after all connections are closed or abandoned.

Shutdown must avoid delivering late packets into destroyed worlds, sessions, or protocol handlers.

---

## Debugging and Instrumentation

Networking should expose diagnostics for:

- ping
- packet loss
- bandwidth in/out
- chunk queue size
- pending reliable messages
- entity replication counts
- correction frequency
- movement rejection/correction rate
- dropped/stale messages
- compression ratio

Debug tools should help distinguish:

- server simulation bugs
- client local-movement bugs
- transport issues
- replication interest bugs
- chunk streaming stalls

---

## Forbidden Patterns

Do not:

- accept client position without movement guardrails
- mutate world state directly from packet handlers
- expose raw ECS entity IDs as network IDs
- serialize raw pointers or raw engine memory
- make singleplayer bypass networking semantics
- send every entity to every client
- stream all chunks without relevance limits
- let chunk streaming starve player movement or corrections
- mix global session state with world-local replicated state
- hard-code protocol behavior into rendering code

---

## Design Summary

Arvane networking is server-authoritative, tick-aware, and interest-based.

The server owns shared simulation and gameplay authority.
The client owns presentation and local player locomotion.

Chunks are streamed within explicit world replication boundaries.  
Entities are replicated by interest, with interpolation for remote entities and client-authored guarded movement for the local player.
Client actions are requests, not mutations.  
Authoritative state flows from the server back to the client.

This structure keeps singleplayer, multiplayer, dedicated servers, and future world systems aligned under one networking model.

---
