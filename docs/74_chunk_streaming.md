# Chunk Streaming

## Purpose

This document defines how chunks are selected, requested, sent, received, and unloaded between the server and client.

Chunk streaming must be event-driven and stateful. Chunks must not be resent every frame, and the client must not scan for new chunks every frame.

The goal is to clearly separate:

- the client deciding which chunks it wants visible
- the server deciding which chunks the player is allowed to receive
- the world/chunk system producing chunk data
- the network layer transporting chunk messages
- the client world applying received chunk data

## Core Rule

Chunk streaming is driven by player chunk movement, not render frames.

A client should only recompute its desired chunk set when the player's chunk position changes, or when a streaming setting changes, such as view distance.

The server should only send chunk data when a chunk enters the player's server-side streaming set, changes after being sent, or is explicitly requested again due to reconnect/resync.

## Terminology

### Player Chunk Position

The chunk coordinate containing the player.

Example:

```cpp
ChunkCoord player_chunk = world_to_chunk(player_position);
````

This value should be cached.

When the player moves, compare the new chunk coordinate against the previous one.

```cpp
if (new_player_chunk != previous_player_chunk) {
    previous_player_chunk = new_player_chunk;
    update_chunk_streaming();
}
```

### Desired Chunk Set

The set of chunk coordinates the client wants loaded based on:

* player chunk position
* client view distance
* dimension/world id
* optional client-side limits

The desired set is a client interest hint, not authority.

### Visible/Loaded Chunk Set

The chunks currently loaded on the client.

### Sent Chunk Set

The chunks the server believes it has already sent to a specific client.

This must be tracked per connected player.

### Server Streaming Set

The authoritative set of chunks the server decides should be available to a player.

This is based on:

* player position
* server view distance
* world/dimension
* permissions or game mode constraints
* world border / region restrictions
* chunk availability
* throttling limits

## Client Responsibilities

The client is responsible for detecting when its desired chunk area changes.

The client must not poll for new chunks every frame.

Instead, the client should:

1. Track the player's current chunk coordinate.
2. Detect when that coordinate changes.
3. Recompute the desired chunk set.
4. Send a chunk interest/update message to the server.
5. Keep rendering currently loaded chunks until replaced or unloaded.

Example flow:

```text
Player moves
  -> world position changes
  -> player chunk coordinate changes
  -> client recomputes desired chunk set
  -> client sends ChunkInterestUpdate
```

The client may also trigger a chunk interest update when:

* joining a world
* respawning
* teleporting
* changing dimension
* changing view distance
* reconnecting
* receiving a server request to resync chunk interest

The client should not directly decide that it owns or has authority over chunks. It only declares interest.

## Server Responsibilities

The server is responsible for deciding what chunks to send.

For each player, the server should maintain chunk streaming state:

```cpp
struct PlayerChunkStreamState {
    WorldId world_id;
    ChunkCoord center_chunk;

    std::unordered_set<ChunkCoord> desired_chunks;
    std::unordered_set<ChunkCoord> sent_chunks;
    std::unordered_set<ChunkCoord> pending_chunks;
};
```

The server receives client interest updates, validates them, and computes the authoritative streaming set.

The server should not blindly trust the client's requested chunk radius or coordinates.

Server-side validation should clamp or override:

* view distance
* world id
* dimension id
* maximum chunks per update
* invalid coordinates
* chunks outside world restrictions

## Boundary Between Networking and World

The networking layer does not generate chunks.

The world system does not directly write packets.

The responsibility split should be:

```text
Client
  -> sends ChunkInterestUpdate

Server networking/session layer
  -> receives interest update
  -> validates player/session
  -> asks chunk streaming system to update interest

Chunk streaming system
  -> computes chunks to load/send/unload
  -> asks world/chunk system for chunk snapshots

World/chunk system
  -> queues load/generation work when data is missing
  -> provides immutable chunk data snapshots only after data is available

Chunk streaming system
  -> builds ChunkData messages
  -> queues messages for network send

Networking layer
  -> sends messages to client
```

The world should expose a chunk data access API, not a packet API.

Good:

```cpp
std::optional<ChunkSnapshot> World::get_chunk_snapshot(ChunkCoord coord);
void World::request_chunk_load(ChunkCoord coord);
```

Bad:

```cpp
void World::send_chunk_to_client(ClientId client, ChunkCoord coord);
```

The world owns simulation and chunk storage. The networking/session layer owns connected clients. The streaming layer bridges the two.

## Chunk Send Rules

The server should send a chunk when:

* the chunk enters the player's streaming set
* the chunk was not previously sent to that player
* the chunk data changed and the client needs an update
* the client reconnects or requests a resync
* the player changes world/dimension

The server should not send a chunk when:

* it was already sent and has not changed
* it is outside the player's authoritative streaming set
* it is not loaded/generated yet
* sending would exceed the per-tick/per-frame network budget

## Chunk Load vs Chunk Send

Loading a chunk and sending a chunk are separate actions.

A chunk may be:

* not loaded
* loading/generating
* loaded but not sent
* sent to one player
* sent to many players
* dirty and requiring update messages

The server should be able to request chunk loads ahead of time without immediately sending them.

```text
Player approaches area
  -> server marks chunks as wanted
  -> streamer requests missing chunks
  -> server/world scheduler runs load/generation jobs asynchronously
  -> world thread publishes completed chunks
  -> chunk snapshots become available
  -> streaming system sends chunks within budget
```

Chunk streaming must never run terrain generation synchronously while preparing a packet. If a requested chunk is missing, the streamer keeps it pending, marks/request-loads the chunk, and returns to the server tick. A later tick sends the chunk after the authoritative world publishes the worker result.

## Sending Budget

Chunk sending should be budgeted.

A player moving quickly can cause many chunks to enter range at once. The server should not send all of them immediately if doing so would cause a spike.

Recommended controls:

* maximum chunks sent per player per tick
* maximum bytes sent per player per tick
* global chunk packet budget
* priority based on distance from player
* higher priority for chunks in front of the player if facing direction is available

Integrated clients should apply received chunk snapshots through a separate
render-frame budget. Server send limits prevent network bursts, but the client
still needs its own cap so deserialized chunk data does not all mutate the
client world and request mesh rescans on one frame.

Example priority:

```text
1. current player chunk
2. chunks closest to player
3. chunks near view direction
4. outer ring chunks
```

## Chunk Unload Rules

When chunks leave the player's streaming set, the server should send a chunk unload message.

The client should unload chunks when:

* receiving a ChunkUnload message
* changing world/dimension
* disconnecting
* performing a full chunk resync

The client may keep recently unloaded chunks in a short-lived cache, but it must treat the server as authoritative.

## Message Types

### ChunkInterestUpdate

Sent from client to server when the player's chunk interest changes.

Contains:

```text
world_id
center_chunk
client_view_distance
optional previous_center_chunk
optional sequence_id
```

The server validates and clamps this.

### ChunkData

Sent from server to client when a chunk should be loaded or replaced.

Contains:

```text
world_id
chunk_coord
chunk_version
block/state data
lighting data if available
biome/worldgen metadata if needed
optional compressed payload
```

### ChunkUnload

Sent from server to client when a chunk should be removed.

Contains:

```text
world_id
chunk_coord
```

### ChunkDelta / ChunkBlockUpdate

Sent from server to client when a previously sent chunk changes.

Contains:

```text
world_id
chunk_coord
chunk_version or update_sequence
changed blocks/states
optional lighting updates
```

Full chunk data should not be used for small block changes unless the chunk needs a full resync.

## Client Receive Flow

When the client receives `ChunkData`:

1. Validate the world id matches the active world.
2. Decode/decompress the payload.
3. Store the chunk in the client world.
4. Mark the chunk as requiring mesh generation.
5. Schedule meshing using immutable chunk data and neighbor boundaries.
6. Publish mesh only when the source chunk version is still valid.

The client should not mesh directly in the network callback.

Network receive should enqueue work onto the appropriate client/world thread.

## Dirty Chunk Updates

When a chunk changes on the server after being sent, the server should mark it dirty for clients that have received it.

For block edits, prefer small delta/update packets.

For large or complex changes, the server may send a full chunk replacement.

The server should track enough per-player state to know which clients need updates.

```text
Block changed in chunk
  -> world marks chunk version dirty
  -> streaming system finds players with this chunk sent
  -> queue ChunkDelta or ChunkData replacement
```

## Threading Rules

Chunk streaming must respect the engine threading model.

* World mutation happens on the simulation thread.
* Chunk snapshots may be read by streaming jobs if immutable.
* Network callbacks should not mutate world state directly.
* Client chunk application should happen on the client world/main simulation path.
* Chunk loading/generation should happen through asynchronous jobs with world-thread publication.
* Mesh generation should happen off-thread from immutable inputs.
* GPU upload should happen on the render thread.

## Required Invariants

* Chunks are not sent every frame.
* Client chunk interest is updated only when player chunk position or streaming settings change.
* Server chunk sending is stateful per player.
* World systems do not know about packet serialization.
* Networking systems do not generate or mutate chunks directly.
* Chunk sends are budgeted.
* Already-sent chunks are not resent unless changed or resynced.
* Client-side rendering is decoupled from chunk receiving.
* Server remains authoritative over what chunks the client receives.

## Common Mistakes to Avoid

Do not do this:

```text
Every frame:
  client checks all nearby chunks
  client requests missing chunks
  server sends all chunks again
```

Do this instead:

```text
When player enters a new chunk:
  client sends updated interest
  server computes diff
  server sends only newly needed chunks
```

Do not do this:

```cpp
world.send_chunk_to_client(client, coord);
```

Do this instead:

```cpp
auto snapshot = world.get_chunk_snapshot(coord);
chunk_streamer.queue_chunk_send(client, snapshot);
```

Do not do this:

```text
Network callback receives ChunkData
  -> immediately builds OpenGL mesh
```

Do this instead:

```text
Network callback receives ChunkData
  -> enqueue client world update
  -> client world stores chunk
  -> meshing job runs later
  -> render thread uploads mesh
```

## Recommended Implementation Shape

```cpp
class ClientChunkInterest {
public:
    void update_player_position(Vec3 position);
    bool consume_interest_changed();
    ChunkInterestUpdate build_interest_update() const;

private:
    ChunkCoord current_chunk_;
    ChunkCoord previous_chunk_;
    int view_distance_;
    bool dirty_;
};
```

```cpp
class ServerChunkStreamer {
public:
    void update_player_interest(ClientId client, ChunkInterestUpdate interest);
    void tick_streaming_budget();
    void notify_chunk_changed(WorldId world, ChunkCoord coord, ChunkVersion version);

private:
    void compute_streaming_diff(ClientId client);
    void queue_chunk_loads(ClientId client);
    void queue_chunk_sends(ClientId client);
    void queue_chunk_unloads(ClientId client);
};
```

```cpp
class WorldChunkProvider {
public:
    void request_chunk_load(WorldId world, ChunkCoord coord);
    std::optional<ChunkSnapshot> get_chunk_snapshot(WorldId world, ChunkCoord coord);
};
```

The exact class names may change, but the separation should remain.

## Summary

Chunk streaming is not a render-frame operation.

The client declares chunk interest only when its chunk position or settings change. The server computes the authoritative streaming set, diffs it against what was already sent, asks the world for chunk snapshots, and sends only necessary chunk data within a budget.

The world provides chunk data. The streaming system decides what should be sent. The networking system transports messages. The client queues chunk data, applies it with a per-frame budget, and schedules rendering work separately.
